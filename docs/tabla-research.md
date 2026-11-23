# Tabla (dayan + bayan) acoustics: hard numbers for a physical synthesis model

Compiled 2026-11-23 from the published literature. Bracketed tags such as [TG17] refer to the source list at the bottom.

How each value is marked:
- **pub**: stated in the source.
- **fig**: read off a published figure by me, so about ±5 dB, ±10% in time, ±10 Hz.
- **calc**: my arithmetic on published numbers.
- **inf**: my inference or estimate. Nothing has been published on it.

Caveat on the literature: the tabla is well studied as an eigenvalue problem and badly studied as a sound source. Raman (1920, 1934) settled the mode-to-harmonic picture by sand figures, with no Hz values. Ramakrishna & Sondhi (1954), Sathej & Adhikari (2009) and Tiwari & Gupta (2017) give frequency *ratios* and model parameters. Only Tiwari & Gupta give a full SI parameter set, and only as an optimum of their model. Measured Hz values for single strokes come from four short papers: Fleischer (2004), Courtney (1991), Patel & Iversen (2003) and Narang & Rao (2017). Nobody has published per-mode T60 from measurement, finger contact times, the syahi thickness profile in mm, the shell cavity volume, or a measured size of the bayan pitch glide in Hz. Rossing & Sykes (1982, *Percussive Notes*) and Fletcher & Rossing §18 were not available to me in full; their content is used here only where Rossing restates it in *Physics Today* 1992 [PT92]. Fleischer's long 2003 laser-vibrometry report [FL03] was offline. Those gaps are labelled **inf** below.

Mode labels: (m,n) = m nodal diameters, n nodal circles counting the rim, so (0,1) is the fundamental. This is Rossing's and Tiwari & Gupta's convention. Malu & Siddharthan [MS00] count interior circles only; their "(0,1)" is (0,2) here. "Sa" means the perceived tonic of the dayan, which is half the (1,1) frequency.

---

## 0. Numbers for the model (summary table)

| Quantity | Value to use | Range / notes | Tag | Source |
|---|---|---|---|---|
| Dayan head diameter (2b) | **14 cm** | 12.7–15.2 cm (5–6 in); makers' outer radius 5–8 cm | pub | [PI03], [PAT15], [CPURI], [TG17] |
| Syahi radius ratio k = a/b | **0.50** | 0.45–0.55 "actually used"; optimum 0.492 (smooth model), 0.506 (with air), 0.40 [RS54], 0.38 [GGL06] | pub | [SA09], [TG17], [RS54] |
| Skin (maidan) areal density σ₂ | **0.25 kg/m²** | 0.20–0.30 kg/m² (0.02–0.03 g/cm²); model optimum 0.245 | pub | [PT92] citing [RS54], [DE78]; [TG17] |
| Syahi / skin areal density ratio | **≈4.2 with a step + air cavity; ≈6.6 peak for a smooth taper; ≈10 for a step in vacuo** | Rossing: "approximately ten times"; [SA09] text: "approximately eight times"; makers' range σ = 2.5–4 → ratio 6–16 | pub/calc | [TG17], [SA09], [RS54], [PT92] |
| Syahi total mass | **9–15 g** | Implies mean syahi areal density ≈ 2.3–3.9 kg/m² on a 7 cm patch (calc) | pub | [PT92] |
| Dry paste density | **≈ 2800 kg/m³** | Printed as "2.8 g/cm²"; must be g/cm³. Mridanga patch: 3 mm thick at centre, 29 g, 0.8 g/cm² | pub | [PT92] |
| Syahi thickness | **≈ 1 mm mean, ≈ 2 mm at centre** | Thickest in the centre, stepped down in 3, 5 or 7 stages | inf (calc from mass) / pub (profile) | [RAM34], [PT92] |
| Kinar (chanti) width | **15 mm** | 13–19 mm (½–¾ in); loose contact, acts as a damper not a load | pub | [CPURI], [RAM34] |
| Maidan width (head edge to syahi edge, minus kinar) | **≈ 20 mm** | 15–25 mm | calc | — |
| Skin thickness | **≈ 0.2 mm** | 0.15–0.25 mm, from σ₂ and ρ ≈ 1100–1300 kg/m³ | calc/inf | — |
| Dayan tension T | **≈ 2.7 kN/m** (14 cm head, Sa = C#4) | Model optimum 1.82 kN/m for a 10 cm head at f(0,1) = 357 Hz; scales to 2.0–3.0 kN/m for 12.7–15 cm heads at Sa = C4–D4 | pub / calc | [TG17] |
| Dayan shell | Solid wood (Indian rosewood), ~0.2–0.25 m tall, bored to about half its height; closed bottom, sealed | | pub | [TG17], [PI03], [BRIT] |
| Dayan cavity | **depth L ≈ 0.07–0.10 m, V ≈ 0.6–0.8 L** | L ≥ 0.06 m does not affect harmonicity; first axial air mode ≈ 1.7 kHz at L = 0.1 m | pub (L) / calc (V, f) | [TG17] |
| Dayan Sa (= f(1,1)/2) | **260–295 Hz** typical | Measured 246, 257, 261, 294 Hz; mean of 5 tablas 259 ± 54 Hz; working range 220–440 Hz | pub/calc | [FL04], [RR18], [C91], [NR17], [PAT15] |
| Dayan (0,1) ("tun" pitch) | **1.12 × Sa** (+200 c) | 1.10–1.16 × Sa measured (+170 … +250 c); "more than ten percent" high; 1.03–1.075 in older data | pub | [TG17], [FL04], [NR17], [C91], [SA09] |
| Dayan mode → harmonic map | (0,1)→1 (sharp); (1,1)→2; (2,1)+(0,2)→3; (3,1)+(1,2)→4; (4,1)+(2,2)+(0,3)→5 | Raman's nine modes; higher modes suppressed by the kinar | pub | [RAM34], [PT92], [TG17] |
| Harmonic deviations | **3rd: −10…0 c; 4th: +11…+16 c; 5th: −9…+52 c** | Measured on one tabla at four tunings (§2) | calc | [TG17] |
| Decay, radiation only (model) | (0,1) **1.3–1.8 s**; (1,1) 15–27 s; (0,2) 0.14–0.21 s; (1,2) 0.45–0.8 s; (2,2) 1.0–2.4 s; (0,3) 0.03 s | m≥1 values far too long: internal and kinar losses dominate them | pub | [TG17] |
| Decay, measured (fig) | tun (0,1) T60 ≈ **1.5 s**; na 3rd harmonic T60 ≈ **0.5–0.7 s**; na (1,1) audible > 1 s at low level; other tun partials die in ≈ 0.3 s | Read off one recording each | fig | [FL04] |
| Damped strokes | ke/kat half-amplitude time **≈ 8 ms**; ghe ≈ **67 ms** | Envelope −6 dB times; ≈ 80 ms and ≈ 0.7 s as T60-equivalents | fig/calc | [PI03] |
| Attack time | **≈ 12 ms** mean (SD 2 ms) | 9 strokes × 5 tablas | pub | [PAT15] |
| Bayan head diameter | **21 cm** | 20–23 cm; height ≈ 25 cm | pub | [TG17], [PI03], [BRIT] |
| Bayan syahi | radius ratio **0.28**, centre offset **0.18 b** (a = 2.86 cm, b = 10.16 cm, d = 1.9 cm) | Courtney's base circle 10–11.4 cm is the first, widest layer | pub | [RAM57] via [TG17], [SA09]; [CPURI] |
| Bayan shell | Copper, brass, steel, aluminium or clay bowl; closed | Cavity ≈ 4–8 L (8.1 L as a cylinder of b = 0.10 m, L = 0.25 m) | pub / calc | [TG17], [PAT15] |
| Bayan f0 | **≈ 90 Hz** | 80–100 Hz; 80 Hz and 125 Hz on two measured sets | pub | [RR18], [RR21], [NR17] |
| Bayan glide | **up to ≈ +12 semitones** with heel pressure/slide | "≈ an octave"; no published Hz trace. Onset glide in dha lasts **213 ms** (se 7.8) | pub (range, coarse) / pub (time) | [RR18], [PI03] |
| Bayan mode ratios (re higher (1,1)) | (0,1) 0.54; (1,1) 0.95/1.00; (2,1) 1.52/1.54; (0,2) 1.75; (3,1) 2.06/2.10; (1,2) 2.32/2.36 | Measured; the eccentric syahi splits every m ≥ 1 pair by 1–5 % | pub | [RAM57] via [SA09] |
| Enclosed air effect | Raises axisymmetric modes only: dayan (0,1) **+2.7 % (+47 c)**; m ≥ 1 modes unchanged | Free air lowers all modes; the cavity pushes (0,n) back up | pub/calc | [TG17], [B91], [PT92] |
| Na spectrum | (0,1) absent; (1,1) weak but long; **3rd harmonic strongest**; 4th and 5th present | 492 / 744 / ~1000 / 1230 Hz on one tabla | pub | [FL04], [C91], [C99] |
| Tun spectrum | (0,1) dominant and longest; (1,1) about 500 Hz masked; (0,2) 748, (1,2) 984, (2,2) 1248 Hz short | Spectral centroid ≈ 295 Hz vs tin ≈ 560 Hz | pub/fig | [FL04], [PI03] |

---

## 1. Construction

### Dayan (right drum)
- **Shell.** "Carved out of a single cylindrical block of wood (generally Indian Rosewood), around 0.2 m in height and 0.05 m in radius, hollowed on one end to almost half of the total height" [TG17]. Both drums "are closed at the bottom" [TG17]. Patel & Iversen call both drums "sealed membranophones" [PI03]. **No hole** is described anywhere (inf: some air will leak through the lacing at the head's edge; no data).
- **Head size.** "Diameter at the membrane may run from just under five inches to over six inches" [CPURI]. Measured sets: 5, 5, 5, 5.5 and 6 in [PAT15]; "dayan ~14 cm" [PI03]. Makers' outer radius ranges "around 0.05 m to … 0.08 m" [TG17].
- **Head layers.** Three skin levels joined by a woven buffalo-hide ring (gajara): an under-layer, the full vibrating skin (maidan) and an outer annulus (chat, kinar or chanti) [CPURI], [PT92]. The vibrating skin is goat hide [TG17], [PAT15]. Rossing adds that "small straws or strings are placed around the edge between the outer and middle head" [PT92]. 16 straps tension the head; Raman stresses that the number is always exactly 16 so the tension can be equalised in all directions [RAM34]. Gajara slits: 48 on the dayan, 64 on the bayan [CPURI].
- **Kinar width.** The chat is "trimmed to about 1/2 to 3/4 of an inch" (13–19 mm) [CPURI].
- **Syahi.** A paste of soot or charcoal, iron filings or iron oxide, and rice or wheat flour [SA09], [PT92], [TG17]. Built up in many thin layers, each dried and rubbed with a stone until fine cracks appear [PT92], [SA09]. "The thickness of the layer is greatest in the centre and shades down towards the margin. In some cases … the thickness is stepped down by three, five or seven stages" [RAM34]. Its surface ends up "slightly convex" [PT92]. Courtney gives the base (first, widest) circle as **about 3½ in (8.9 cm)** on the dayan; it is kept at full size for four or five layers, then the layers shrink [CPURI].
- **Masses** [PT92], citing Ramakrishna & Sondhi and De:
  - unloaded skin "typically around 0.02–0.03 g/cm²" = **0.2–0.3 kg/m²**;
  - loaded part "approximately ten times as great";
  - "total mass of the loaded portion is in the range of 9–15 g for different tablas";
  - Rossing's own mridanga patch: about 3 mm thick at the centre, 29 g, 0.8 g/cm², dry paste density 2.8 (units printed as g/cm²; must be g/cm³).
- **Cross-check (calc).** A 7 cm syahi (k = 0.5 on a 14 cm head) has 38.5 cm² area. At ratio 10 on a 0.25 kg/m² skin it adds 8.7 g. The measured 9–15 g therefore means a mean syahi areal density of 2.3–3.9 kg/m², or a ratio of 10–16 if it all sits on 7 cm. Mean added thickness at 2800 kg/m³: about 0.8–1.3 mm, so roughly 2 mm at the centre for a tapered profile (inf).
- **Fitted model values** (no paper measured a real syahi profile):

| Model | Density ratio σ₁/σ₂ | Radius ratio k | Notes | Source |
|---|---|---|---|---|
| Step (composite), in vacuo | 9.77 (σ = 3.125) | 0.40 | Original fit to Raman | [RS54] |
| Step, in vacuo, optimised | 8.41 (λ = 2.9) | 0.38 | Puts (0,3) at the 6th harmonic, which is wrong | [GGL06] via [TG17] |
| Smooth tanh taper, in vacuo | 6.60 peak (σ = 2.57) | 0.492 | Edge width ξ = 0.091 b | [SA09] |
| Step + baffle + cylindrical cavity | 4.19 (λ = 2.0467) | 0.5055 | σ₂ = 0.2451 kg/m², b = 0.0501 m, T = 1821.5 N/m, L = 0.1 m → f(0,1) = 356.7 Hz | [TG17] |

  [SA09] say makers' heads span σ = 2.5–4 (ratio 6.3–16) and k = 0.45–0.55. [TG17] find a "wide flat valley" of near-equal error: k is the sensitive parameter (k = 0.65 gives 9× the error), while b from 0.03 to 0.08 m, σ₂ from 0.25 to 0.35 and L > 0.06 m all work.

### Tuning and pitch
- **Tension (calc).** f scales as √(T/σ)/b. Scaling [TG17]'s optimum (Sa = 318 Hz on a 5.01 cm radius) gives:
  - 12.7 cm head at C4: 2.0 kN/m;
  - 14 cm head at C4 / C#4 / D4: 2.4 / 2.7 / 3.0 kN/m;
  - 15.2 cm head at B3: 2.5 kN/m.
- **Measured tonics (Sa):**
  - 246 Hz (Fleischer: (1,1) at 492 Hz) [FL04];
  - 257 Hz [RR18];
  - 261 Hz (Courtney: na's 3rd harmonic at 783 Hz on a drum tuned to C) [C91];
  - 294 Hz, D4 [NR17];
  - (1,1) tuned to A4, B♭4, B4 and C5 on one drum, i.e. Sa = A3…C4 [TG17];
  - five tablas: mean 259 Hz, SD 54 Hz [PAT15];
  - working range "220–440 Hz" [RR18], "200–400 Hz" [RR21].
- Players tune with na: the little finger along a diameter at the edge, strike 90° away, and listen to the (1,1) mode [TG17]. Courtney: an out-of-tune na shows a broad peak near 770 Hz that is "actually two peaks about a half step apart", i.e. a tension-split doublet [C91]. Raman: with unequal tension, beats are heard and the sand nodal line of the (1,1) mode rotates to and fro [RAM34].

### Bayan (left drum)
- **Shell.** "Clay or metal (brass, copper, steel, or aluminium); it is approximately 0.25 m high and has a drumhead of radius around 0.10 m" [TG17]. Polished copper, brass, bronze or clay [PAT15]. "Metal-shell" [PI03]. Britannica: about 25 cm high, face about 20 cm [BRIT]. "Bayan ~22 cm" [PI03].
- **Head model** (taken from a measured dugga) [RAM57] via [TG17]: b = 0.1016 m, syahi radius a = 0.0286 m (k = 0.28), offset d = 0.0191 m (0.19 b), air column 0.25 m. [SA09] use k = 0.29, ε = 0.18. Courtney's base circle on the bayan is 4–4½ in (10–11.4 cm), which is the first, widest layer [CPURI].
- **Cavity (calc/inf).** A cylinder of b = 0.10 m and L = 0.25 m holds 8.1 L. A real bowl is smaller, so use **≈ 5–6 L** (inf). First axial air mode of a 0.25 m column ≈ 690 Hz (calc).
- **Bayan tension (calc/inf).** [TG17]'s bayan fit (T = 3300 N/m, σ₁ = 0.2 kg/m²) puts f(0,1) near 480 Hz for a uniform head, far above the real 80–100 Hz, so treat it as a ratio-only fit. A uniform 21 cm head at σ = 0.3–0.5 kg/m² and 90 Hz needs only **≈ 180–300 N/m**; with the syahi mass, **≈ 300–700 N/m** (inf). The bayan head is about 5–10× slacker than the dayan's, which is why palm pressure can bend it so far.
- **Tuning relation.** The bayan sits "one octave below the right hand tabla" [TG17]; "a fifth or an octave below" [BRIT]. Measured pairs: 257 / 80 Hz (−20 semitones) [RR18]; 294 / 125 Hz (−15 semitones) [NR17] (calc). So "an octave below" is loose.

---

## 2. Dayan modes and harmonics

### Raman's picture [RAM34], [RK20]
- The head is "loaded and damped in such a manner that all the overtones above the ninth are suppressed and these nine are grouped in such a manner as to give a succession of five tones in harmonic sequence" [RAM34].
- **Mode groups** (Raman's sand figures; Rossing's (m,n) labels [PT92]; also [TG17]):

| Harmonic | Modes | Raman's nodal pattern |
|---|---|---|
| 1 | (0,1) | whole head moves |
| 2 | (1,1) | one nodal diameter |
| 3 | (2,1) + (0,2) | "two parallel nodal lines" (any mix of the two) |
| 4 | (3,1) + (1,2) | "three parallel nodal lines" |
| 5 | (4,1) + (2,2) + (0,3) | four lines; (0,3) found in 1919 on a large mridanga |

- Any mix of the degenerate modes in one group sounds at the same pitch; the sand shows ellipses, parallel lines or hyperbolae depending on the mix [RAM34].

### Measured frequency ratios
Ratios are normalised so that (1,1) = 2. Cents are deviations from the nearest integer, my calc.

**Tiwari & Gupta, one tabla tuned four ways** [TG17, Table III]:

| Mode | (1,1)=A4 | B♭4 | B4 | C5 | Cents (A4 / B♭4 / B4 / C5) |
|---|---|---|---|---|---|
| (0,1) | 1.156 | 1.128 | 1.122 | 1.122 | +251 / +209 / +199 / +199 |
| (1,1) | 2 | 2 | 2 | 2 | 0 |
| (0,2) | 2.985 | 2.988 | 2.990 | 3.000 | −9 / −7 / −6 / 0 |
| (2,1) | 2.982 | 2.988 | 2.988 | 3.000 | −10 / −7 / −7 / 0 |
| (1,2) | 3.969 | 4.03 | 4.027 | 4.025 | −13 / +13 / +12 / +11 |
| (3,1) | 3.963 | 4.034 | 4.037 | 4.025 | −16 / +15 / +16 / +11 |
| (0,3) | 5.073 | 5.037 | 4.975 | 5.153 | +25 / +13 / −9 / +52 |
| (2,2) | 5.063 | 5.068 | 5.012 | 5.148 | +22 / +23 / +4 / +51 |
| (4,1) | 5.077 | 5.07 | 4.977 | 5.142 | +26 / +24 / −8 / +48 |

**Fleischer, one tabla, strokes tun and na** [FL04]. With Sa = 492/2 = 246 Hz (calc):
- (0,1) 272 Hz = 1.106 (+174 c);
- (1,1) 492 Hz (na), "about 500 Hz" (tun) = 2.00;
- (2,1)+(0,2) 744 Hz (na), 748 Hz (tun) = 3.02–3.04 (+14 … +23 c);
- (1,2) 984 Hz = 4.00 (0 c);
- 1230 Hz (na, "a combination which includes … the 2-2 mode"), 1248 Hz (tun, (2,2)) = 5.00–5.07 (0 … +25 c).

**Older data:**
- Raman's values as tabulated by [SA09]: 1.03, 2, 3, 3, 4, 4, 5.04, 5.03, 5.08. The (0,1) is +51 c sharp (calc).
- Siddharthan, Chatterjee & Tripathi (1994), as quoted secondhand: 1.075 : 2 : 3 : 4.025 [SCT94 via MS00].
- [MS00]'s continuous-loading model: 1.07, 2.00, 2.98, 2.99, 4.00, 4.00, 5.01, 5.01, 5.02 (+117 c on (0,1)).

**Model ratios** (same (1,1) = 2 basis) [TG17, Table II]:

| Mode | Uniform | Step, in vacuo | Step, in air | Step, air + cavity |
|---|---|---|---|---|
| (0,1) | 1.26 | 1.12 | 1.09 | 1.12 |
| (0,2) | 3.02 | 3.08 | 3.11 | 3.10 |
| (2,1) | 2.69 | 2.93 | 2.95 | 2.95 |
| (1,2) | 3.67 | 4.04 | 4.12 | 4.02 |
| (3,1) | 3.33 | 3.88 | 3.94 | 3.94 |
| (0,3) | 4.53 | 4.84 | 4.85 | 4.91 |
| (2,2) | 4.40 | 4.95 | 5.05 | 4.94 |
| (4,1) | 3.97 | 4.86 | 4.95 | 4.95 |

[SA09] Table I, with RS54's parameters (σ = 3.125, k = 0.4): step model (0,1) 1.031, (2,1) 3.041, (0,2) 3.155, (3,1) 4.093, (1,2) 4.227, (0,3) 4.979, (4,1) 5.113, (2,2) 5.309. Smooth model: 1.035, 3.039, 3.053, 4.009, 4.146, 4.778, 5.002, 5.249.

### What this means for the model
- **The (0,1) mode is deliberately sharp.** It sits about 2 semitones (1.10–1.16×) above Sa, not at Sa. Courtney: tun's fundamental "is normally about one step above the tuned pitch" [C91]. Narang & Rao measured tonic 294 Hz and tun 330 Hz [NR17]. Leaving it unmuted sounds a major second against the tonic, which players avoid; Sa is heard as the missing fundamental of harmonics 2–5 [C99], [TG17].
- **Harmonics 2–4 are tight** (within ±16 c in [TG17]). The 5th-harmonic triplet spreads ±50 c and changes with tension.
- **Degenerate pairs are not exact.** Expect 1–5 Hz splits between (2,1)/(0,2) and (3,1)/(1,2), plus tension-induced splits of each (m≥1) cos/sin pair (Raman's rotating nodal line, Courtney's half-step doublet when out of tune). Use a 0.5–3 Hz split for a well-tuned drum (inf).
- **Modes above the 9th** exist but the kinar kills them (§4). Tiwari & Gupta also keep only nine [TG17].

---

## 3. Bayan modes

**Measured (Ramakrishna 1957), relative to the higher (1,1) member** [RAM57] via [SA09] Table II; the multiples of f(0,1) are my calc:

| Mode | Ratio | × f(0,1) |
|---|---|---|
| (0,1) | 0.54 | 1.00 |
| (1,1)a / b | 0.95 / 1.00 | 1.76 / 1.85 |
| (2,1)a / b | 1.52 / 1.54 | 2.81 / 2.85 |
| (0,2) | 1.75 | 3.24 |
| (3,1)a / b | 2.06 / 2.10 | 3.81 / 3.89 |
| (1,2)a / b | 2.32 / 2.36 | 4.30 / 4.37 |

- Relative to f(1,1)b / 2, the (m,1) family sits near 2, 3, 4 (1.90/2.00, 3.04/3.08, 4.12/4.20). The (0,1) is 8 % sharp of "1", and (0,2) at 3.5 and (1,2) at 4.6–4.7 are inharmonic (calc). The bayan is partly harmonic, not fully.
- The eccentric syahi lifts the degeneracy: the (1,1) pair is **5 % (≈ 90 c) apart**, the others 1–2 % [RAM57], [SA09]. In [SA09]'s model, spectra barely change for offsets ε < 0.1; above that the higher modes go inharmonic first.
- Air cavity, model only [TG17 Table V] (no air / column under the patch / column under the whole head): (0,1) 0.49 / 0.49 / 0.50; (2,1) 1.46 / 1.51 / 1.50; (0,2) 1.72 / 1.80 / 1.77; (3,1) 1.94 / 2.03 / 2.00; (1,2) 2.34 / 2.46 / 2.42. "Air loading … significantly improves the harmonicity in the left hand tabla" [TG17].
- Only the first few modes are excited by the player's action [BN91] via [SA09]. Ghe is "usually a single strong harmonic close to 100 Hz [that] persists after the onset" [RR21]. The bayan has "far fewer overtones than the dayan" [C99]. UBC student FFT of ghe: about 100 Hz, plus peaks near 290 and 450 Hz (low weight).
- **Which mode is the ghe pitch?** Not settled in print (inf). [TG17]'s radiation-only model gives the bayan (0,1) a decay time of only 0.017 s, versus 0.07 s for (1,1) and 1.2 s for (3,1). If that holds, the sustained 80–100 Hz tone is more likely the lowest (1,1)-type mode, with the heel of the hand fixing its nodal line. For the synth: put the strong, sustained "ghe" partial at f_ghe, and add a short, heavily radiation-damped (0,1) about an octave below the (1,1) group.

---

## 4. Damping and decay

### The kinar and other losses
- Raman: "the width of the marginal ring of leather … has a notable influence on the duration of tone. … With a broad ring, we obtain a muffled tone of short duration in which few overtones are present, while with a narrow ring, the tone is prolonged and is also brighter." High overtones have most amplitude near the margin, so the ring damps them without much effect on the low modes. "The contact between the ring and the drum-head is probably imperfect, and this should tend to make the ring act as a damper rather than as a load" [RAM34].
- Rossing: the "rather stiff annular leather flap in loose contact with the peripheral portion" confines most vibration to the loaded part of the head [PT92].
- Raman credits the long sustain to the heavy rigid shell (little energy lost to the support), the syahi mass, and "probably" the enclosed air [RAM34].
- **Model (inf):** an annulus of frictional (velocity-proportional) damping over the outer 13–19 mm, strong enough that modes with most energy there, i.e. high m and modes above the 5th harmonic, get T60 < 0.1 s. Add no mass for the kinar.

### Radiation-only decay times (model) [TG17, Table IV]
T60 in seconds for (1,1) tuned to A4 / B♭4 / B4 / C5:
- (0,1): 1.8 / 1.7 / 1.5 / 1.3
- (1,1): 26.5 / 22.9 / 18.2 / 14.5
- (0,2): 0.21 / 0.19 / 0.16 / 0.14
- (2,1): 348 / 280 / 199 / 142
- (1,2): 0.81 / 0.74 / 0.55 / 0.45
- (3,1): 5800 / 4318 / 2745 / 1756
- (0,3): 0.03 / 0.03 / 0.03 / 0.03
- (2,2): 2.4 / 1.9 / 1.4 / 1.0

The authors call some values "unreasonably high" because only radiation loss is modelled. The pattern is still useful:
- Axisymmetric (0,n) modes are monopoles and radiate fast. (0,1) loses about 1.5 s worth, (0,2) about 0.2 s, (0,3) about 30 ms.
- (m,1) modes with m ≥ 1 barely radiate. Their real decay is set by skin, syahi and kinar losses.
- Decay times fall as tension (pitch) rises.
- Without the cavity, all decay times are "much higher": the cavity adds radiation load.

Bayan, radiation only [TG17 Table V]: (0,1) **0.017 s**; (1,1) 0.066–0.068 s; (2,1) 0.33 s; (0,2) 0.35 s; (3,1) 1.22 s; (1,2) 0.18 s. "Much shorter than those for the right hand tabla."

### Measured decays
- **Fleischer** (one tabla, 1 s window, 40–90 dB display) [FL04]:
  - Tun: the 272 Hz (0,1) partial "with long sustain dominates"; its frequency track runs the whole 1 s. The other partials "are minor in magnitude and shorter in temporal duration". Read off the plot, (0,1) falls about 40 dB in 1 s, so T60 ≈ **1.5 s** (fig). The 748/984/1248 Hz partials vanish into the floor within ≈ 0.3 s, so T60 ≈ **0.3–0.4 s** (fig).
  - Na: the 744 Hz partial "decays quickly but starts with such a high level that it most probably will determine the musical pitch". It falls from about 88 dB to the 40 dB floor in ≈ 0.45 s, so T60 ≈ **0.5–0.7 s** (fig). Its audible track lasts ≈ 0.7 s. The 492 Hz (1,1) "sustains long but has only a low level"; its track runs past 1 s, so T60 ≥ **2 s** (fig/inf). The ≈ 1 kHz and 1230 Hz partials last ≈ 0.15–0.45 s.
- **Patel & Iversen** [PI03], half-amplitude (−6 dB) time of the envelope, mean of about 30 tokens:
  - kat (flat-hand damped bayan stroke): **≈ 8 ms** (fig);
  - ghe (open bayan stroke): **≈ 67 ms** (fig, se ≈ ±18 ms).
  - If the decay were exponential, those give T60-equivalents of ≈ 80 ms and ≈ 0.7 s (calc). Ghe is played with the heel resting on the head, so it is a partly damped stroke.
- **Patranabis et al.** (wavelet study, 9 strokes × 5 tablas): "a shorter decay for Re, Te, Tu and Na while longer decay for Ghe, Ge, Tee and Teen"; "higher energy bands are of shorter duration than the lower energy bands" [PAT16].
- **Rohit & Rao:** damped strokes have "no sustained harmonics"; resonant strokes have smooth band-wise decays at different rates [RR21].
- **Not published:** per-mode measured T60 or Q for any tabla, and Rossing & Sykes' decay data (not accessible to me).

### Values to use (inf, built on the above)

| Mode / group | T60 (open stroke) | Why |
|---|---|---|
| Dayan (0,1) | 1.2–1.8 s | [TG17] radiation + [FL04] fig |
| Dayan (1,1) | 2–3 s | Long and low in na [FL04]; needs internal loss, since radiation gives > 14 s |
| Dayan 3rd-harmonic pair | 0.6–1.0 s | na 744 Hz fig 0.5–0.7 s, partly cut by the resting finger |
| Dayan 4th-harmonic pair | 0.3–0.6 s | |
| Dayan 5th-harmonic triplet | 0.15–0.3 s | Raman: short "except in large and specially well-made instruments" |
| Dayan modes above the 9th | < 0.05 s | kinar |
| Bayan ghe partial | 0.5–1.0 s (heel resting) | [PI03] half-decay 67 ms |
| Bayan (0,1) | 20–50 ms | [TG17] radiation |

---

## 5. Strokes: excitation and damping

Descriptions differ by gharana and author. Below: where it is struck, what rests on the head, and what survives.

| Stroke | Drum | How it is played | Damping | What sounds | Source |
|---|---|---|---|---|---|
| **na / ta** | dayan | Index finger strikes near the rim (on or just inside the kinar). Ring finger rests lightly on the head at the syahi edge. | Resting ring finger, light | (0,1) absent; (1,1) weak, long; **3rd harmonic strongest**; 4th, 5th present. Sharp, bell-like, short. | [FL04], [TG17], [PI03], [NR17], [C91] |
| **tin / teen** | dayan | Index finger strikes the maidan and bounces off. Third finger rests on the head. | Resting finger, light | Like na but brighter: centroid ≈ 560 Hz | [PI03], [NR17], [PAT15] |
| **tun / thun** | dayan | Index finger (or 4 fingers together) strikes the syahi or its edge and lifts at once | None | (0,1) dominant and longest; other partials ≥ 15 dB down and short. Centroid ≈ 295 Hz. | [FL04], [PI03], [NR17], [PAT15] |
| **ti / te / re / ra / tit** | dayan | Fingers strike the syahi centre and stay on it. te: middle + ring; re: index. | Pressed fingers | Damped, broadband, short. "Damped strokes have higher brightness and spectral centroid than the free strokes." | [PAT15], [NR17] |
| **tra / tira** | dayan | Middle then index strike the patch in quick succession and stay | Pressed | Two impacts **≈ 19 ms** apart (fig) | [PI03] |
| **ge / ghe / ga** | bayan | Index (or index + middle) strikes the maidan "at far edge of patch". Heel of hand rests on the head. Ge: fingers released quickly. | Heel (variable pressure) | Single strong partial 80–125 Hz; pitch set and bent by the heel | [PI03], [PAT15], [NR17], [PB20] |
| **ke / ka / kat** | bayan | Flat hand across rim, head and patch, left pressed | Whole hand | Non-resonant; half-decay ≈ 8 ms | [PI03], [NR17] |
| **kra** | both | Bayan kat first, then a damped dayan stroke | Pressed | Impacts **≈ 36 ms** apart (fig) | [PI03] |
| **dha** | both | na/ta + ghe together | as parts | Low glide phase then steady (§6) | [PI03] |
| **dhin** | both | tin (or tun) + ghe | as parts | | [RR18] |

**Raman's isolation recipes** [RAM34]. These are the most direct published statement of how damping points select modes, and they suit a contact-damper model:
- **Harmonic 1 only:** bring the flat palm down "smartly on the centre … and then quickly removing it"; "the deep hum-tone obtained is quite free from overtones". A fingertip stroke adds overtones.
- **Harmonic 2:** strike with the edge of the palm while the little finger lies along a diameter. Or touch one finger "along a diameter near the margin" and strike on the perpendicular diameter.
- **Harmonic 3:** touch "two points near each other on the edge of the black central load" and strike "at a point removed 90° away".
- **Harmonic 4:** touch three points on the syahi edge and strike near the outer edge, 90° from the middle point.
- **Harmonic 5:** touch four points on the syahi edge and strike on the marginal ring.
- **(0,2) alone:** touch with a pencil tip "at some little distance from its centre", on the nodal circle, and tap the centre.

**Mechanism of na (Fleischer):** "The ring finger, which is in loose contact with the skin, enforces a nodal line at the contact region. … the finger resting on the skin damps the axi-symmetric 0-1 mode. Only anti-symmetric vibrations with at least one nodal diameter evolve" [FL04]. Tiwari & Gupta: the lightly pressed ring finger "keeps out the fundamental mode" [TG17].

**Model (inf):**
- A resting finger is a local dashpot plus a small mass at a point on or near the syahi edge (r ≈ 0.5 b).
- Each mode is damped in proportion to its squared amplitude there. All (0,n) modes have large amplitude at r ≈ 0.5 b and die. For each m ≥ 1 pair, the member with a nodal diameter through the finger survives. That is why the na spectrum is "harmonics 2–5, no 1".
- Strike position sets the start mix:
  - near the rim (na) favours high m;
  - the maidan (tin) is in between;
  - the syahi centre (tun) mostly excites (0,n).
- Pressed (closed) strokes: a stiff, lossy contact over 1–3 finger areas (≈ 1.5 cm each) held for the whole note.

**Contact time and force: not published** for any tabla stroke. Inference from the hang study (fingertip mass 5–20 g, pad stiffness 3–10 kN/m): **1–3 ms** for bounce strokes (na, tin, tun, ge), **5–15 ms** for a palm or flat-hand stroke. Measured attack time (envelope rise) is 12 ± 2 ms across strokes [PAT15]; that includes the membrane build-up, not just contact.

---

## 6. Bayan pitch glide

- **Mechanism.** The heel of the left hand rests on the maidan. Pressing harder, or sliding it toward the syahi, raises the pitch.
  - "The heel of the hand is moved back and forth along the diameter passing through the centers of the membrane and the eccentrically placed sihai to modulate the pitch" [SA09].
  - "Pressing into the drum with the palm heel changes the pitch of the attack. Sliding the heel over the surface … after a strike produces the tabla's distinctive glissando" [PB20].
  - Pitch "can be set by the pressure applied at the moment of striking, or changed afterward" (rising or falling).
  - Courtney shows a "Ga … played with the wrist sliding forward and the pressure increasing", calls it "a very common modulation", and notes the perceived pitch follows the frequency directly [C99].
- **Range.** "Pitch can be varied: ≈ an octave" for ghe/ghi/ga [RR18, Table 1]. A retail/educational source also says the bayan can be bent "up to around an octave". **No published Hz trace of a full glide exists.** Use **0 to +12 semitones**, with most musical bends +2 … +7 semitones (inf).
- **Time course.** In dha, "the fundamental of the low-pitched bayan rose and fell in frequency at the onset … before attaining a steady value"; this FM phase lasted **213 ms** (se 7.8 ms). During it, bayan f0 energy relative to the dayan's 2nd partial was ≈ **+19 dB**, against ≈ **+11 dB** after (fig) [PI03]. So the onset glide is a quick rise and fall over ≈ 0.2 s, and the bayan is louder during it.
- **Physics (calc/inf).** An octave needs 4× tension if tension alone does it, which a palm cannot add to a ~0.5 kN/m head without huge force. The heel must mostly act as a moving boundary: it shrinks the free part of the head and shifts the effective syahi offset. **Model:** a heel contact region (≈ 3–4 cm patch) at an adjustable radius on the far side from the syahi. Its stiffness and position rise with "pressure". Map pressure → f_ghe × 2^(s/12), s ∈ [0, 12], and also raise the contact damping with pressure.

---

## 7. Air cavity effects

- **Direction of the shifts:**
  - "The unconfined air mass above the membrane lowers the frequency of all modes … while the enclosed air in the cavity stiffens the membrane and increases the frequency of the axisymmetric modes" [TG17].
  - Bhat (1991) assumed uniform cavity pressure, so only axisymmetric modes shift [B91] via [TG17].
  - [SA09]: "Modes with nodal circles are strongly affected, the greatest being the fundamental. Modes with nodal diameters are not affected."
- **Size, dayan** [TG17 Table II]: (0,1) moves from 1.09 (in air) to 1.12 (with cavity), **+2.7 %, +47 c** (calc). (0,3) goes 4.85 → 4.91; (1,2) 4.12 → 4.02, since in this model it couples through the baffle and air mass. "The role of air loading in achieving harmonicity of Indian drums is minor compared to that in kettledrum." Cavity depth L > 0.06 m makes no difference to harmonicity.
- **Bayan:** air loading "significantly improves the harmonicity" (error 0.0116 → 0.0027) [TG17]. It pulls the m = 1, 2, 3 families onto multiples of 0.5 (Table V, §3).
- **Kettledrum comparison** [PT92]: air-mass loading is "mainly responsible" for timpani harmonicity. Smaller kettle volume raises (0,1), (0,2), (0,3) and slightly lowers (1,1)…(4,1). Kettle shape matters little; volume matters.
- **No Helmholtz mode.** Both shells are closed and sealed [TG17], [PI03]. The air acts as a spring on the net volume velocity of the head.
- **Model (calc/inf):** add an adiabatic air spring K_air = ρc²S²/V acting on each mode's net volume displacement. Here ρc² ≈ 1.42 × 10⁵ Pa and S is the head area. Dayan V ≈ 0.6–0.8 L; bayan V ≈ 5–6 L. Tune its strength so that dayan (0,1) rises ≈ 3 % and m ≥ 1 modes do not move. First standing-wave cavity modes are ≈ 1.7 kHz (dayan) and ≈ 0.7 kHz (bayan) (calc); not studied in print.
- **Radiation:** (0,1) of each head is a monopole and radiates fastest. (1,1) is a dipole [SA09], [TG17]. The cavity adds radiation load, so all decays shorten [TG17].

---

## 8. Spectra and levels of strokes

Levels in dB relative to the strongest partial; all fig, ±5 dB, from Fleischer's 3-D FFT plots of one player on one drum [FL04]:

| Stroke | (0,1) | (1,1) | 3rd (744 Hz) | 4th (≈ 1 kHz) | 5th (≈ 1.23 kHz) |
|---|---|---|---|---|---|
| tun | 0 dB (272 Hz) | ≈ −20 dB (≈ 500 Hz), masked | ≈ −15 dB | ≈ −20 dB | ≈ −20 dB |
| na | absent | ≈ −20 to −25 dB, long | 0 dB | ≈ −8 dB | ≈ −12 dB |

Other statements:
- Na "essentially has no fundamental but a very pronounced third harmonic" (783 Hz on a C-tuned drum); tun "has a very pronounced fundamental with lower amplitude harmonics" [C91].
- Tin, ta and na: "the strong second and third harmonic force the brain to extrapolate downward and assign a pitch to a nonexistent fundamental" [C99].
- Spectral centroid, mean of ~30 tokens from 6 players: tin ≈ **560 Hz**, tun ≈ **295 Hz** (fig) [PI03].
- Five-tabla timbre study [PAT15]: tristimulus T1 (fundamental share) ≈ 0.02, T2 (partials 2–4) ≈ 0.08, T3 (higher) ≈ 0.90, i.e. the fundamental is weak in most strokes. Odd and even harmonics are about equal (0.48 / 0.50). Most harmonic information lies in 0.9–2.5 kHz [PAT16]. Smaller heads give more harmonics [PAT16].
- Dayan musically useful range: harmonics at 200–2000 Hz [RR21]. Courtney counts the 5 Raman harmonics plus about 4 more in "ra", and judges "only the first seven harmonics" musically significant [C99].
- Dha: low-pass (bayan) to high-pass (dayan) level ≈ +19 dB in the first 0.2 s, ≈ +11 dB after [PI03] (fig).
- Rossing & Sykes measured mridanga spectra at 32 stages while building a patch. (0,2) and (2,1) "gradually approached the frequency of the third harmonic" [PT92]. Their numbers are not available to me.

**Durations (inf from the above).** Audible length in a quiet room:
- tun ≈ 1.5–2 s;
- na, tin ≈ 0.8–1.2 s (long, quiet (1,1) tail);
- ghe ≈ 0.5–1 s;
- te, ti, re, ke, kat < 0.1 s.
No MIR dataset publishes stroke durations. Patel & Iversen truncated all drum tokens at 550 ms for listening tests [PI03]; Rohit & Rao used dayan bols ≥ 100 ms [RR18].

---

## Source list

- **[RK20]** Raman, C.V., Kumar, S. (1920). "Musical drums with harmonic overtones." *Nature* 104, 500. https://ui.adsabs.harvard.edu/abs/1920Natur.104..500R/abstract (abstract only).
- **[RAM34]** Raman, C.V. (1934). "The Indian musical drums." *Proc. Indian Acad. Sci.* A1, 179–188 (received Sept. 1934; often cited as 1935). doi:10.1007/BF03035705. Full text read at https://ed.iitm.ac.in/~raman/1935PIAS.pdf
- **[RS54]** Ramakrishna, B.S., Sondhi, M.M. (1954). "Vibrations of Indian musical drums regarded as composite membranes." *J. Acoust. Soc. Am.* 26(4), 523–529. doi:10.1121/1.1907369. Abstract read (J. Indian Inst. Sci. abstracts, https://journal.iisc.ac.in/index.php/iisc/article/download/4244/4346); parameters via [SA09], [TG17].
- **[RAM57]** Ramakrishna, B.S. (1957). "Modes of vibration of the Indian drum dugga or left-hand thabala." *J. Acoust. Soc. Am.* 29, 234–238. Not read; values via [SA09], [TG17].
- **[SR58]** Sarojini, T., Rahman, A. (1958). "Variational method for the vibrations of the Indian drums." *J. Acoust. Soc. Am.* 30, 191–196. Via [SA09].
- **[DE78]** De, S. (1978). "Experimental study of the vibration characteristics of a loaded kettledrum." *Acustica* 40, 206–210. Via [PT92].
- **[RS82]** Rossing, T.D., Sykes, W.A. (1982). "Acoustics of Indian drums." *Percussive Notes* 19(3), 58–67. https://pas.org/publication-articles/acoustics-of-indian-drums (members only; not read).
- **[PT92]** Rossing, T.D. (1992). "Acoustics of drums." *Physics Today* 45(3), 40–47. https://aip.brightspotcdn.com/PTO.v45.i3.40_1.online.pdf
- **[FR98]** Fletcher, N.H., Rossing, T.D. (1998). *The Physics of Musical Instruments*, 2nd ed., Springer, ch. 18. Not read directly; its tabla content matches [PT92].
- **[B91]** Bhat, R.B. (1991). "Acoustics of a cavity-backed membrane: The Indian musical drum." *J. Acoust. Soc. Am.* 90, 1469–1474. Not read; summarised via [TG17], [SA09].
- **[BN91]** Banerjee, B.M., Nag, D. (1991). "The acoustical character of sounds from Indian twin drums." *Acustica* 75, 206–208. Via [SA09].
- **[C91]** Courtney, D. (1991). "Tuning the tabla: a psychoacoustic perspective." *Percussive Notes* 29(3), 59–61. https://chandrakantha.com/?p=16053
- **[C99]** Courtney, D. (1999). "Psychoacoustics of the musical pitch of tabla." *J. Sangeet Research Academy* 13, 9–13. https://chandrakantha.com/music-and-dance/instrumental-music/indian-instruments/tabla/psychoaoustics/
- **[CPURI]** Courtney, D. Tabla construction and overview pages, chandrakantha.com. https://chandrakantha.com/?p=16178 and https://chandrakantha.com/?p=15954 (maker-level detail, not peer-reviewed).
- **[SCT94]** Siddharthan, R., Chatterjee, P., Tripathi, V. (1994). *Physics Education* (India), Oct. 1994. Not read; ratios as quoted in [MS00] and https://simplycurious.blog/2018/06/24/the-indian-musical-drums/
- **[MS00]** Malu, S.S., Siddharthan, A. (2000). "Acoustics of the Indian drum." arXiv:math-ph/0001030. https://arxiv.org/abs/math-ph/0001030
- **[FL04]** Fleischer, H. (2004). "Vibration and sound of the Indian tabla." *Proc. CFA/DAGA'04*, Strasbourg, 1073–1074. http://pub.dega-akustik.de/DAGA_1999-2008/data/articles/001326.pdf
- **[FL03]** Fleischer, H. (2003). "Schwingungs- und Schalluntersuchungen an der indischen Tabla." *Beiträge zur Vibro- und Psychoakustik* 1/03, UniBw München. https://www.rz.unibw-muenchen.de/lrt4/veroeffentlichungen/2003_1_tabla.pdf (server offline when tried; not read).
- **[PI03]** Patel, A.D., Iversen, J.R. (2003). "Acoustic and perceptual comparison of speech and drum sounds in the North Indian tabla tradition: an empirical study of sound symbolism." *Proc. 15th ICPhS*, Barcelona, 925–928. https://www.internationalphoneticassociation.org/icphs-proceedings/ICPhS2003/papers/p15_0925.pdf
- **[GGL06]** Gaudet, S., Gauthier, C., Léger, S. (2006). "The evolution of harmonic Indian musical drums: a mathematical perspective." *J. Sound Vib.* 291, 388–394. Via [TG17].
- **[SA09]** Sathej, G., Adhikari, R. (2009). "The eigenspectra of Indian musical drums." *J. Acoust. Soc. Am.* 125(2), 831–838. arXiv:0809.1320, https://arxiv.org/abs/0809.1320
- **[PAT15]** Patranabis, A. et al. (2015). "Harmonic and timbre analysis of tabla strokes." arXiv:1510.04880. https://arxiv.org/abs/1510.04880
- **[PAT16]** Patranabis, A. et al. (2016). "Categorization of tablas by wavelet analysis." arXiv:1601.02489. https://arxiv.org/abs/1601.02489
- **[TG17]** Tiwari, S., Gupta, A. (2017). "Effects of air loading on the acoustics of an Indian musical drum." *J. Acoust. Soc. Am.* 141(4), 2611–2621. doi:10.1121/1.4979782. https://home.iitk.ac.in/~ag/papers/TiwariGupta17.pdf
- **[NR17]** Narang, K., Rao, P. (2017). "Acoustic features for determining goodness of tabla strokes." *Proc. ISMIR 2017*, 257–263. https://archives.ismir.net/ismir2017/paper/000030.pdf
- **[RR18]** Rohit, M.A., Rao, P. (2018). "Acoustic-prosodic features of tabla bol recitation and correspondence with the tabla imitation." *Proc. Interspeech 2018*, 1229–1233. https://www.isca-archive.org/interspeech_2018/ma18_interspeech.pdf
- **[RR21]** Rohit, M.A., Rao, P. (2021). "Automatic stroke classification of tabla accompaniment in Hindustani vocal concert audio." *J. Acoust. Soc. India* (April 2021); arXiv:2104.09064. https://arxiv.org/abs/2104.09064
- **[PB20]** Pardue, L. et al. (2020). "Demystifying tabla through the development of an electronic drum." *Proc. NIME 2020*, 596–599. https://zenodo.org/records/4813212/files/nime2020_paper116.pdf
- **[BRIT]** "Tabla", Encyclopaedia Britannica. https://www.britannica.com/art/tabla (low weight; dimensions only).
- Low-weight extras used once each: UBC PHYS 341 student wiki, https://wiki.ubc.ca/Course:Phys341_2020/Tabla (ghe FFT peaks); Chordia, solo tabla notes, https://ccrma.stanford.edu/~pchordia/soloTablaNotes.htm (ghe pitch set at strike or bent after).
- Not obtained: Rossing & Sykes 1982 full text (decay and spectral data); Fletcher & Rossing §18 (book); Fleischer 2003 report; Bhat 1991 full text; the Applied Acoustics 2023 paper "Analysis of /t̪a:/ and /t̪ʊn/ bols of the tabla from a musical standpoint" (doi via https://www.sciencedirect.com/science/article/abs/pii/S0003682X23004346, paywalled).
