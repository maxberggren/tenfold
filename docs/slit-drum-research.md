# Slit drums and tongue drums: hard numbers for a physical synthesis model

Compiled 2026-11-23. Bracketed tags such as [TEP19] refer to the source list at the bottom.

How each value is marked:
- **pub**: stated in the source.
- **fig**: read off a published figure.
- **calc**: my arithmetic on published numbers (scripts: Euler–Bernoulli FE / analytic beam, Hertz impact; method stated inline).
- **inf**: my inference or estimate. Nothing has been published on it.

Caveat on the literature. No peer-reviewed modal study of a modern wooden tongue drum exists that I could find. The real measurements are:
- **[TEP19]**: three Aztec teponaztli, with f0, bandwidth and T60 of both tongues.
- **[CAM12]**: one Nigerian slit gong (JASA). I only saw the abstract and search snippets; the full text sits behind a Cloudflare wall.
- **[THO15]**: an Ohio University MS thesis with tongue lengths and f1 for two small wooden tongue drums, plus box-mode and Helmholtz estimates.
- **Bar data**: marimba/xylophone bar data [FR], [HILL11], [MCA04], and kalimba tine overtone data [CHA06].

Nobody has published the following, so everything about them below is calc or inf:
- measured B2/B1 or torsion/B1 ratios on a wooden tongue
- T60 per tongue on a tongue drum
- tongue-drum mallet contact time
- the Helmholtz frequency of a commercial tongue drum

Several key wood-damping papers (Brémaud et al., Obataya et al.) were blocked (HAL Anubis, Springer). Their values below come from abstracts or open re-publications and are flagged.

---

## 0. Numbers for the model (summary table)

| Quantity | Value to use | Range / notes | Tag | Source |
|---|---|---|---|---|
| Tongue model | Cantilever beam (clamped-free), Euler–Bernoulli | Plate effects matter for L/w < ~2 | — | [FR] §2.17, [THO15] |
| Bending ratios, ideal clamp | **1 : 6.267 : 17.55 : 34.39** | βL = 1.8751, 4.6941, 7.8548, 10.9955. [FR] prints the 4th ratio as "35.39"; that is a misprint. | pub/calc | [FR] eq. 2.62 |
| f1 formula | f1 = **0.1615 · (h/L²) · √(E_L/ρ)** | h = thickness, L = free length. Width-independent for a beam. | calc | [FR] |
| B2 node | **0.783 L from root** (not 0.774) | B3 nodes 0.504 and 0.868 L; B4 nodes 0.358, 0.644 and 0.906 L | calc | — |
| Mode amplitude vs strike point (tip = 1) | B1: 0.10 / 0.34 / 0.66 / 0.86 at x/L = 0.25 / 0.5 / 0.75 / 0.9. B2: −0.42 / −0.71 / −0.14 / +0.52. B3: +0.72 / 0.02 / −0.58 / +0.23 | | calc | — |
| Volume-velocity factor (mean w / tip w) | B1 **0.39**, B2 −0.22, B3 0.13, B4 −0.09 | Monopole radiation weight per mode | calc | — |
| Root compliance (rotational spring κ = kL/EI) | κ = 30: f1 × 0.94, ratios 1:6.31:17.7. κ = 10: f1 × 0.84, 1:6.52:18.7. κ = 3: f1 × 0.65, 1:7.61:22.9. κ = 1: f1 × 0.44, 1:10.4:32.7 | **A soft root raises f2/f1.** Exactly dual to a tip mass μ = 1/κ. | calc | — |
| Tip mass μ (× tongue mass) | μ = 0.1: f1 × 0.84, f2/f1 6.52. μ = 0.25: f1 × 0.70, 7.21. μ = 0.5: f1 × 0.57, 8.38. μ = 1: f1 × 0.44, 10.43 | | calc | — |
| Root thinned (undercut near root) | h × 0.7 over the first 30% of L: f1 × 0.64, ratios 1:8.5:24 | Lowers pitch and **stretches** the partials | calc | — |
| Tip thinned | h × 0.7 over the last 30% of L: f1 × 1.14, ratios 1:5.4:13.5. Linear taper 1→0.5: f1 × 1.09, 1:4.8:12.4 | Raises pitch and **compresses** the partials | calc | — |
| Kalimba tines (fulcrum + bar root), measured | f1/f0 = **5.6–5.9** | Ratio falls as the a/b mounting ratio falls. Mbira: strongest overtones ≈ 5× and 14× f0. | pub | [CHA06], [MB] |
| First torsion / first bending (thin strip) | **f_T1/f_B1 ≈ 3.10 · (L/w) · √(G/E_L)** | Wood G_LT/E_L = 0.06–0.10 gives **≈ (0.76–0.98)·L/w**, so 1.5–3.9 × f1 for L/w = 2–4. Steel (G/E = 0.385) gives 1.92·L/w. | calc | [FR] §2.20, [WH10] |
| Torsion overtones | Clamped-free: f_Tm = m·c_T/(4L), m = 1, 3, 5 (odd harmonics of T1) | c_T ≈ (2h/w)·√(G/ρ) for w ≫ h | pub | [FR] §2.20, Fig. 2.23 |
| Shear modulus governing torsion | Flat-sawn top (width ∥ T): **G_LT**. Quarter-sawn (width ∥ R): **G_LR**. | | inf (plate theory) | — |
| Wood: padauk (*P. soyauxii*) | ρ **745 kg/m³**, E_L **13.1 GPa** (static, so ~14.4 dynamic) | Very low tan δ_L because of extractives (removing them raises tan δ by up to ×2). Interlocked grain varies E_L/ρ and tan δ_L by ×2. | pub | [WDB], [BRE10], [BRE10b] |
| Wood: bubinga / purpleheart | 890 kg/m³, 17.1–18.4 GPa / 905 kg/m³, 15.7–20.3 GPa | | pub | [WDB], [WH10] T5-4a |
| Wood: walnut / hard maple / cherry / Honduras mahogany | 610 / 705 / 560 / 590 kg/m³; E 11.6 / 12.6 / 10.3 / 10.1–10.3 GPa | WH E is static bending; add ~10% for E_L | pub | [WDB], [WH10] T5-3a |
| Wood: redwood / western redcedar (box tops) | ~400 / ~350 kg/m³ (SG 0.40 / 0.32); E 9.2 / 7.7 GPa | | pub | [WH10] |
| Elastic ratios (12% MC) | Walnut: E_T/E_L .056, E_R/E_L .106, G_LR/E_L .085, G_LT/E_L .062. Cherry: .086 / .197 / .147 / .097. Sugar maple: .065 / .132 / .111 / .063. African mahogany: .050 / .111 / .088 / .059 | Full table in §3 | pub | [WH10] T5-1 |
| Poisson (walnut) | ν_LR .495, ν_LT .632, ν_RT .718, ν_TR .367, ν_RL .052, ν_TL .036 | Others in §3 | pub | [WH10] T5-2 |
| tan δ_L (along grain) | **Use 0.005 for padauk/rosewood class, 0.008 for maple/walnut/cherry** | Rosewood marimba bars 0.0036–0.0068 (mean 0.0045); spruce 0.006–0.010; maple ~0.010–0.011; 445-species survey 0.0037–0.020 | pub (padauk value inf) | [HILL11], [CAR18], [HU20], [BRE-S] |
| tan δ, shear / cross-grain | tan δ_GLR ≈ **0.018–0.021**; tan δ_R ≈ **0.02–0.03**; shear/L ratio **≈1.4–2.6** | Spruce .018/.022, maple .021/.02 (CAR18). Torsion: maple .0153, spruce .0139 at ~50–60 Hz (HU20). | pub | [CAR18], [HU20] |
| tan δ vs frequency | **Flat ~100–2000 Hz**, then rising (up to ×2 above 2 kHz) | Marimba bars: η constant within error from C2 to C7 | pub | [BKB10] (snippet), [HILL11] |
| Effective loss of whole tongue (with root/body/radiation) | **η_eff ≈ 0.007–0.021** (teponaztli) vs 0.0045 for a marimba bar | η = 2.2/(f·T60) | calc | [TEP19], [FR] |
| Teponaztli f0 and T60 | 110–480 Hz; T60 **0.39–2.0 s**; Δf/f (−3 dB) 1.0–4.2% | Full table in §1.3 | pub | [TEP19] |
| Teponaztli two-tongue interval | **+359 to +772 cents** (closest to minor 3rd, ~minor 6th, ~5th+) | Closing the underside cavity shifts f by <1 Hz; tones get shorter and louder | pub | [TEP19] |
| Rosewood marimba T60 | E3: **3.2 s** bare bar, 1.5 s with resonator. E6: 0.5 s / 0.4 s | T60 ∝ 1/f (constant η) | pub | [FR] §19.4, [HILL11] |
| Tongue drum T60 (fundamental) | **~0.4–1.2 s at 300 Hz**, scaling ~1/f | Builders: wood = "short to medium sustain"; most tongues "muted quickly" [THO15] | inf (from η_eff) | — |
| Box air modes | f_lmn = (c/2)·√((l/Lx)² + (m/Ly)² + (n/Lz)²) | 60 × 20 × 18 cm commercial box (interior ~57 × 17 × 15): f100 ≈ **301 Hz**, f010 ≈ 1.0 kHz, f001 ≈ 1.14 kHz | pub (formula) / calc | [THO15], [FR] |
| Box mode ↔ tongue coupling | A tongue at a box mode rings longer. Driving a tube-extended box gave max sustain at L_box = c/(2f): 0.42 m for 409 Hz, 0.61 m for 280 Hz (calc 0.419/0.613) | | pub | [THO15] |
| Helmholtz f_H | f_H = (c/2π)·√(A/(V·ℓ_eff)) | Ohio large drum gap 2.45 × 7.45 cm, top 1.4 cm: thesis gives **237 Hz** without end correction; with flanged end corrections ≈ **120 Hz** | pub / calc | [THO15] |
| Kerf-port Helmholtz (typical commercial box) | **≈150–350 Hz, Q ≈ 7–20** | 3 mm kerfs, 15 mm top, ~1–3 m of kerf, 14 L box. Viscous Q ≈ a/(2δ_v) | calc/inf | — |
| Slit gong (Nigerian) | Two lowest mechanical modes tuned to ≈ two cavity acoustic resonances. Tabs measured **384 / 439 Hz** (predicted 383 / 428); calculated Helmholtz **349 Hz** | Beating = interference of mechanical and acoustic resonances, not energy exchange | pub (abstract) / pub-snippet (numbers, unverified) | [CAM12], [SHM06] |
| Air-spring stiffening of a tongue | k_air = ρc²·S_eff²/V with S_eff = 0.39·w·L | 120 × 40 × 10 mm padauk tongue in 7.5 L: k_air ≈ 67 N/m vs tongue modal k ≈ 7×10⁴ N/m, so **Δf ≈ +0.05% (negligible)** | calc | — |
| Hertz law | F = K·δ^1.5, K = (4/3)·E*·√R, 1/E* = Σ(1−ν²)/E | | pub (standard) | [CD97], [MCA04] |
| Xylophone mallet in Chaigne-type model | m = **12 g**, R = 12 mm, **K = 4.2×10⁸ N/m^1.5** | Bar E = 9.29 GPa; fluid damping γ_B = 8 s⁻¹ | pub | [MCA04] Tab. II |
| Other published mallet K | K = 10⁷ (soft) and 10⁹ (hard) N/m^1.5, M = 20 g, V = 1 m/s | Based on Bork's data | pub (snippet) | [ANT] |
| Contact time, rigid target (Hertz) | **Rubber superball (E ≈ 3–10 MPa, R 12.5 mm, 12–15 g): 1.5–3.4 ms**. Hard plastic/acetal: 0.3–0.5 ms. Chaigne mallet: 0.16–0.25 ms (v = 0.3–3 m/s) | τ = 2.943·δ_max/v, δ_max = (5mv²/4K)^0.4; τ ∝ v^(−1/5) | calc | — |
| Spectral peak of a strike | f_max ≈ **0.85/τ** (sin² pulse) | Rubber ball: f_max ≈ 250–580 Hz; hard plastic ≈ 2–3 kHz | pub / calc | [BORK83] via [FR] |
| Mallet head speeds | **≈0.5–3.5 m/s** (pp to ff) | Bork's 10 mallets | fig | [FR] Fig. 19.12 |
| Hunt–Crossley damping | F = K·δ^α·(1 + λ·δ̇), λ ≈ 3(1−e)/(2·v_in) | Superball e ≈ 0.85–0.9 gives λ ≈ 0.15–0.2 s/m at v = 1 m/s | pub (recalled, not rechecked) / inf (e) | [HC75] |
| Fingertip strike | τ ≈ **3–8 ms** | No published fingertip-on-tongue data. Hand slap on drum ~40 ms is a multi-contact palm+finger frame estimate. | inf | [KRZ] |
| Optimal mallet mass | ≈ dynamic mass of the vibrator (~30% of a marimba bar's mass in mode 1) | Cantilever tip-referenced modal mass = **0.25 × tongue mass** | pub / calc | [FR] §19.7 |

---

## 1. Instruments

### 1.1 Traditional slit drums (log gongs)
- **Acoustics (Montagu).** Slit drums function acoustically as struck Helmholtz resonators [MON]:
  - Pitch is set mainly by the volume of the hollow, modified by the open area of the slit.
  - The thickness of the slit's lips modifies it further: one lip pitched higher than the other. Leaving an internal lump on one lip (New Guinea) does the same.
  - Field anecdote from Malekula, Vanuatu: a new standing drum hollowed from the largest log, with a full-length slit, came out among the *highest* pitched, because its open area was the largest. Occluding the slit with a hand lowers the pitch (pub, qualitative).
  - Sizes run from palm-sized to logs 2–3 m long and ≥1 m wide [MON].
- **Nigerian slit gong (Igbo ekwe/ikoro type).** [CAM12] used ESPI interferometry plus FE modelling.
  - The carver tunes the **two lowest mechanical resonances of the lips to near two acoustic resonances of the internal cavity**.
  - They identify **four tuning parameters** (abstract) [CAM12].
  - Search snippets of the full text give tab resonances measured at 384 and 439 Hz (predicted 383 and 428 Hz, <10% error) and a calculated Helmholtz frequency of 349 Hz. Tab frequencies drop faster than f_H as parameters change. These numbers are unverified; I could not open the PDF.
  - The earlier APS abstract [SHM06]: two mechanical resonances plus one Helmholtz resonance. Beating comes from **interference** between the mechanical and acoustic frequencies, with no significant two-way energy exchange (pub).
- **Lokele/lokole (Congo).** The two sides of the slit are carved to different wall thicknesses to give two pitches (museum text, [WIKI-SD], drummuseum.com). No frequencies are published.
- **Garamut (PNG).** Museum records [MUS]:
  - Sepik example ≈ 3 m long, *Vitex cofassus* wood.
  - Others: 2.4 m × 0.61 × 0.39 m; 1.31 m × 0.47 × 0.71 m; small 0.90 m × 0.18 × 0.16 m.
  - No acoustic measurements found.
- **Vanuatu tamtam, Fijian lali.** Nothing quantitative found (Wikipedia lists them, no dimensions or pitches).
- **Livika (New Ireland).** Three tongues give four pitches, the fourth from the solid end. It is *rubbed* with moistened hands, not struck [MON] (pub).

### 1.2 Teponaztli (Aztec, H-cut)
- **Construction.** Hollowed from the underside, which is then closed with a plate. The top carries an H-shaped slit giving **two tongues**, one from each end. Different tongue length and/or thickness gives two pitches [MON], [WIKI-SD].
- **[TEP19] (Madera y Bosques 2019).**
  - Three original instruments (Cipactli, Malinalco, Tlaxcala), all *Dalbergia* (rosewood; close to *D. palo-escrito* / *D. granadillo*, literature SG 0.84–1.25).
  - Typical body 15–25 cm in diameter, 60–90 cm long.
  - Mallets 125 g and 70 g.
  - Recorded at 80 cm, 48 kHz.
  - No tongue dimensions are given.
- [UCT] (UC thesis) built a two-tongue PVC-tube model of a teponaztli. I could not open it (403/size).

### 1.3 Teponaztli measured data [TEP19] (pub; T60 is the decay of the fundamental)

| Instrument | Cavity | Low f0 (Hz) | Low Δf/f | Low T60 (s) | High f0 (Hz) | High Δf/f | High T60 (s) | Interval (c) |
|---|---|---|---|---|---|---|---|---|
| Cipactli | closed | 142.41 | 4.17% | 0.739 | 222.22 | 1.37% | 1.443 | +772 |
| Cipactli | open | 141.84 | 3.91% | 0.793 | 221.59 | 1.01% | 1.971 | +770 |
| Malinalco | closed | 109.92 | 2.43% | 1.644 | 167.00 | 1.73% | 1.517 | +722 |
| Malinalco | open | 110.18 | 1.98% | 2.009 | 167.20 | 1.51% | 1.743 | +724 |
| Tlaxcala | closed | 389.62 | 2.89% | 0.391 | 479.58 | 1.39% | 0.660 | +359 |
| Tlaxcala | open | 389.55 | 2.36% | 0.477 | 479.41 | 1.35% | 0.677 | +360 |

Notes on the table:
- **Effective loss factor (calc).** η = 2.2/(f·T60) gives:
  - Cipactli: 0.021 (low), 0.0069 (high)
  - Malinalco: 0.0122, 0.0087
  - Tlaxcala: 0.0145, 0.0069
  
  The higher tongue is usually the less damped one. η_eff is 1.5–5× rosewood bar material damping (0.0045), so root, body and radiation losses dominate.
- **T60 per cycle (calc).** f0·T60 ≈ 105–320 cycles. A rosewood marimba bar runs ≈ 500 cycles.
- **Bandwidth inconsistency (calc).** The −3 dB bandwidth implies shorter decays than the measured T60 (e.g. Cipactli high: 2.2/Δf = 0.72 s vs 1.44 s measured). The bandwidth probably includes window/attack broadening. Use T60.
- **Cavity open vs closed.** Open/closed changes f0 by <0.6 Hz. Closed gives slightly shorter, stronger tones. Striking one tongue sounds the other, and the sides radiate faintly at **700–1000 Hz** [TEP19] (pub).
- **Literature intervals.** Earlier claims cited in [TEP19]: Tlaxcala a minor third (walnut attribution), Cipactli/Malinalco perfect fifths. The measurements show +359 c and +722 to +772 c, i.e. not exact Western intervals.

### 1.4 Modern wooden tongue drum (box)
- **Origins.** I found no source for a "Jim Ralston" origin. Wikipedia mentions "Gato" (20th-c. US brand name, later generic) [WIKI-SD]. Steel tongue drums (Doble 1990, Havlena 2005) are a separate lineage.
- **Commercial (Schlagwerk 60C80/60P82/60D85)** [SG]:
  - 8 tongues, **60 × 20 × 18 cm**, ~5.6 kg.
  - African padauk tongues; one model has an alder box.
  - 60C80 range **E3–A4 (165–440 Hz)**: e g a c1 d1 e1 g1 a1.
  - Others: 6-note 40 × 16 × 17 cm (pine/robinia); 4-note 32 × 12 cm; 3-tongue children's 16 × 8.5 cm.
- **DIY / maker practice** [DIY]:
  - Top 12–20 mm thick: 5/8–3/4 in (16–19 mm) for drums with ≥8 tongues, 1/2 in (12.7 mm) cherry reported good. "Less than [20 mm] and the tongues vibrate against each other."
  - Box walls 12–20 mm. Example boxes: 450 × 170 × 130 mm (20 mm boards); 60 × 20 × 18 cm.
  - Tongue lengths in one build 3–7.5 in (76–190 mm) in ½-in steps. Gap between tongues ~2 cm; 45° edge cuts claimed to ring longer.
  - Woods: padauk (most recommended), maple, purpleheart, walnut, mahogany, rosewood, cherry, jarrah, Tasmanian blackwood, red oak sides.
  - **Tuning rule** (several builders): thin the **tip** to raise pitch, thin **near the root** to lower it. This matches the calc in §2.3.
  - Mallets: **superballs glued on dowels**, or felt/yarn mallets, or fingers. Rubber feet "improve the sound dramatically."
  - Sound holes: no consistent spec. Some boxes are closed, others have one bottom/side port (inf).
- **Measured tongue frequencies on real wooden tongue drums [THO15]** (pub):
  - Small drum, 4 tongues; box 28.1 × 8.5 × 8.3 cm: L = 64 / 83 / 110 / 129 mm gives f1 = **710.3 / 495.3 / 323 / 236.9 Hz**.
  - Large drum, 2 tongues; box 57.2 × 8.8 × 13.7 cm; central gap 2.45 × 7.45 cm: L = 109 / 151 mm gives f1 = **409.1 / 279.9 Hz**.
  - Wood species, thickness and width are not stated.
  - The fitted exponent f ∝ L^−n is **n ≈ 1.56** (small) and **1.16** (large), not 2 (calc). Tongue width and thickness probably vary, and root compliance and tip-cut shapes flatten the L-dependence.
  - Most tongues were "muted quickly". The 280 Hz tongue, close to the box f100 = 300 Hz, rang longest (pub, qualitative).
  - The "second modes" at 559.7 and 818.3 Hz in that thesis are 2×f1: tube/box responses to the speaker, **not** tongue B2 (inf, from the method).
- **Dimension check (calc).** With f1 = 0.1615·h·√(E/ρ)/L², a padauk tongue (√(E/ρ) ≈ 4.2–4.4 km/s), h = 12 mm, clamped:
  - E3 (165 Hz) needs L ≈ 22 cm; A4 (440 Hz) needs L ≈ 13.6 cm. This fits a 60 cm box with 8 tongues.
  - [THO15]'s 129 mm → 237 Hz implies h ≈ 6 mm for an ideal clamp, or a soft root (κ ≈ 1–3) at h ≈ 10–12 mm.

---

## 2. The tongue as a cantilever

### 2.1 Bending (Euler–Bernoulli, clamped-free)
- f_n = (β_nL)²/(2πL²)·√(EI/ρA). β_nL = 1.8751, 4.6941, 7.8548, 10.9955. Ratios **1 : 6.267 : 17.547 : 34.386** [FR] (FR's printed "35.39 f1" is a typo; calc confirms 34.39).
- Rectangular section: **f1 = 0.1615·h·√(E_L/ρ)/L²**, independent of width (calc).
- Nodes and strike-point amplitudes are in the table above (calc). **Note: the B2 node is at 0.783 L from the root.** 0.774 is not a cantilever number; the free-free bar nodes are 0.224/0.776.
- Strike-point rules (calc):
  - Striking at ~0.78 L kills B2. Striking at 0.5 L kills B3 and keeps B2 at 0.71 of tip amplitude.
  - Tip strikes excite all modes at unit amplitude.
  - Near-root strikes (x < 0.3 L) give a weak B1 (≤0.1–0.15) and relatively strong B3/B4. The result is thin and "clicky", as builders report.
- Rotary inertia and shear (Timoshenko) lower the upper partials. For wood (E/G ≈ 10–20) at h/L ≈ 0.08–0.15 this is significant for B3 and above: roughly a few % to >10% (inf). The glockenspiel shows the same direction: "spaced a little closer together than predicted" [FR §19.1] (pub, qualitative).

### 2.2 Torsion
- Torsional wave speed c_T = √(K_T G/(ρ I_p)). For thin rectangular strips c_T ≈ **(2h/w)·√(G/ρ)** [FR Fig. 2.23]. Clamped-free: **f_Tm = m·c_T/(4L), m = 1, 3, 5…** [FR §2.20] (pub).
- Hence (calc): **f_T1/f_B1 = (h/(2wL))·√(G/ρ) / (0.1615·h/L²·√(E/ρ)) = 3.096·(L/w)·√(G/E_L)**.
- Wood, flat-sawn (G_LT/E_L from [WH10] T5-1):
  - Walnut .062 → √ = .249 → **f_T1 = 0.77·(L/w)·f_B1**
  - Sugar maple .063 → 0.78·L/w
  - African mahogany .059 → 0.75·L/w
  - Cherry .097 → 0.96·L/w
  - Honduras mahogany .086 → 0.91·L/w
  - Measured dynamic G/E is sometimes higher: wavy maple E/G = 4.9–6.4, spruce 6.6–10.5 [HU20]; Nozaki: spruce 17.8, maple 8.6 [HU20 citing]. So use G/E ≈ 1/16–1/6 → f_T1/f_B1 ≈ (0.77–1.26)·L/w.
- Typical tongues: L/w = 2–4. **The first torsion mode sits at ≈1.5–4 × f1, well below B2 (6.3 × f1).** For steel (G/E = 0.385) the same tongue gives 3.8–7.7 × f1. This is the main timbral difference from steel: in wood an inharmonic torsion partial sits in the first 1–2 octaves above the fundamental (calc).
- Wide tongues (L/w ≲ 1.5) are plates. Isotropic square cantilever plate (ν = 0.3) frequency parameters 3.494, 8.547, 21.44, 27.46, 31.17 give ratios 1 : 2.45 (1st torsion) : 6.14 (2nd bending) : 7.86 : 8.92 [LEI69] (pub; values recalled from Leissa, not re-checked this session). Wood's low G and E_T lower the torsion and cross-width modes further (inf).
- Excitation: torsion is excited ∝ off-axis distance × twist mode shape; T1 twist grows linearly root to tip (calc). Striking at the centreline gives none. Striking at the edge (offset w/2) near the tip gives B1 and T1 at comparable displacement (inf).
- Damping of torsion: use tan δ_shear ≈ 1.4–2.6 × tan δ_L (§3), so torsion decays faster per cycle than bending (pub ratio / inf application).

### 2.3 Elastic root, tip mass, undercut
Computed with a 200-element Hermite beam FE plus an analytic check (calc; h is local thickness, uniform width):
- **Rotational-spring root** (κ = kL/EI): κ = 100, 30, 10, 5, 3, 1 → f1 × 0.98, 0.94, 0.84, 0.74, 0.65, 0.44 and f2/f1 = 6.27, 6.31, 6.52, 6.97, 7.61, 10.43. A real tongue cut into a top plate has finite root stiffness (the plate bends locally around the root). An inf guess is κ ≈ 5–30: f1 down 6–26% and B2/B1 ≈ 6.3–7.
- **Tip mass** μ (fraction of tongue mass): 0.1, 0.25, 0.5, 1 → f1 × 0.84, 0.70, 0.57, 0.44 and f2/f1 = 6.52, 7.21, 8.38, 10.43 (identical to root spring κ = 1/μ: dual problem).
- **Root undercut** (h × 0.8 / 0.7 / 0.6 over 30 / 30 / 20% of L at the root): f1 × 0.76 / 0.64 / 0.56 and ratios 1:7.6:21, 1:8.5:24, 1:9.4:27. A taper from 0.6 h at the root to h at the tip: f1 × 0.57, 1:7.9:23.6.
- **Tip thinning** (h × 0.7 over the outer 30%): f1 × 1.14, ratios 1:5.4:13.5. Linear taper from h to 0.5 h at the tip: f1 × 1.09, 1:4.8:12.4.
- **Measured analogue:** kalimba tines (clamped + fulcrum, short overhang between fulcrum and bridge) give f1/f0 = **5.86, 5.63, 5.76, 5.61** for a/b = 3.9, 3.8, 3.3, 3.2 [CHA06]. Mbira keys give overtones at ≈5 and ≈14 × f0, and FE shows the far field is dominated by transverse modes [MB] (pub).
- **Consequence for the model (inf).** B2/B1 on a real wooden tongue drum is probably **5–8**, depending on how it was tuned: tip-thinned below 6.27, root-thinned or soft-rooted above. A randomised per-tongue B2/B1 in [5.5, 7.5] with B3/B1 ≈ 2.8–3.0 × (B2/B1) is defensible. Nothing measured exists for wooden tongue drums.

---

## 3. Wood properties

### 3.1 Density and E_L (12% MC)
| Species | ρ (kg/m³) | E (GPa) | Source |
|---|---|---|---|
| African padauk *Pterocarpus soyauxii* | 745 | 13.07 | [WDB] (pub) |
| Bubinga *Guibourtia* spp. | 890 | 18.41 (WH: 17.1, SG 0.71 basic) | [WDB], [WH10] T5-4a |
| Purpleheart *Peltogyne* spp. | 905 | 20.26 (WH: 15.7, SG 0.67 basic) | [WDB], [WH10] |
| Black walnut | 610 | 11.59 (WH: 11.6, SG 0.55) | [WDB], [WH10] T5-3a |
| Hard (sugar) maple | 705 | 12.62 (WH: 12.6, SG 0.63) | [WDB], [WH10] |
| Red maple | — (SG 0.54) | 11.3 | [WH10] |
| Black cherry | 560 | 10.3 (SG 0.50) | [WDB], [WH10] |
| Honduras/true mahogany | 590 | 10.06–10.3 | [WDB], [WH10] |
| African mahogany *Khaya* | — (SG 0.42 green) | 9.7 | [WH10] |
| Redwood, old/young growth | ~400 / ~350 (SG 0.40 / 0.35) | 9.2 / 7.6 | [WH10] |
| Western redcedar | ~350 (SG 0.32) | 7.7 | [WH10] |
| Brazilian rosewood | 830 | E∥ 16 GPa, E⊥ 2.8 GPa; c∥ 4400, c⊥ 1800 m/s | [FR] T19.1 (Haines 1979) |
| Indian rosewood / African mahogany / European maple / redwood / Sitka spruce | 740 / 550 / 640 / 380 / 470 | E∥ 12 / 12 / 10 / 9.5 / 13 GPa; E⊥ 1.7 / 1.2 / 2.2 / 0.96 / 1.3 GPa | [FR] T19.1 |

- WH "SG" is specific gravity based on oven-dry weight and *green* volume (T5-4a) or 12% volume (T5-3a). WH MOE is static bending; WH says to add ~10% to get E_L [WH10] (pub).
- √(E/ρ) (calc): padauk 4.19 km/s, bubinga 4.55, purpleheart 4.73, walnut 4.36, maple 4.23, cherry 4.29, mahogany 4.13. Dynamic values run ~5% higher (inf).

### 3.2 Elastic ratios (WH Table 5-1, ~12% MC; pub)
| Species | E_T/E_L | E_R/E_L | G_LR/E_L | G_LT/E_L | G_RT/E_L |
|---|---|---|---|---|---|
| Walnut, black | .056 | .106 | .085 | .062 | .021 |
| Cherry, black | .086 | .197 | .147 | .097 | — |
| Maple, sugar | .065 | .132 | .111 | .063 | — |
| Maple, red | .067 | .140 | .133 | .074 | — |
| Mahogany, African | .050 | .111 | .088 | .059 | .021 |
| Mahogany, Honduras | .064 | .107 | .066 | .086 | .028 |
| Yellow-poplar | .043 | .092 | .075 | .069 | .011 |
| Redwood | .089 | .087 | .066 | .077 | .011 |
| Cedar, western red | .055 | .081 | .087 | .086 | .005 |
| Spruce, Sitka | .043 | .078 | .064 | .061 | .003 |

Poisson ratios (WH Table 5-2; pub), in the order ν_LR, ν_LT, ν_RT, ν_TR, ν_RL, ν_TL:
- Walnut: .495, .632, .718, .367, .052, .036
- Cherry: .392, .428, .695, .282, .086, .048
- Sugar maple: .424, .476, .774, .349, .065, .037
- African mahogany: .297, .641, .604, .264, .033, .032
- Honduras mahogany: .314, .533, .600, .326, .033, .034
- Redwood: .360, .346, .373, .400, —, —
- Western redcedar: .378, .296, .484, .403, —, —

No tropical (padauk/bubinga/purpleheart) elastic ratios were found. Padauk has "somewhat lower E′/ρ and elastic anisotropy (E′/G′) than typical hardwoods" because of a wide microfibril angle [BRE10] (pub, abstract). Use G_LT/E_L ≈ 0.08–0.10 and E_T/E_L ≈ 0.06–0.08 for padauk (inf).

### 3.3 Damping (loss factor tan δ = η = 1/Q)
- **Along grain, measured:**
  - Rosewood marimba bars (Yamaha YM-5100A, C2–C7, f1/f4/f10/f20 partials): **η = 0.0036–0.0068, mean 0.0045 ± 0.0009**, constant with frequency within error. τ_n ∝ 1/f_n [HILL11] (pub).
  - Spruce: tan δ_L 0.006–0.010 (avg 0.007). Maple: ~0.01 [CAR18] (pub).
  - Maple (wavy) 0.0109, spruce 0.0085 at ~300–350 Hz bending [HU20] (pub).
- **Survey range:** Brémaud's 445-species survey gives tan δ **0.0037–0.020**; pernambuco averages 0.0043 [BRE-S] (pub via abstract snippet). Wood in general: tan δ ≈ 0.01–0.1 in broad materials literature (too high for musical hardwoods).
- **Padauk:** "exceptionally low" tan δ_L, caused by extractives; extraction raises tan δ by up to ×2 with little change in E′/ρ. Interlocked grain (0–25°) varies tan δ_L and E′_L/ρ by ×2 [BRE10], [BRE10b] (pub, abstract). No number was accessible. **Use 0.004–0.006** (inf: same class as rosewood and vène *P. erinaceus*, the favourite xylophone wood).
- **Cross-grain and shear (pub)** [CAR18]:
  - Spruce: tan δ_R 0.017–0.036 (avg 0.022); tan δ_GLR 0.016–0.020 (avg 0.018).
  - Maple: tan δ_R ~0.02 (0.02–0.03); tan δ_GLR 0.017–0.028 (avg 0.021).
  - Torsion (G_LT) at ~50–60 Hz: maple 0.0153, spruce 0.0139 [HU20]. The ratio tan δ_G/tan δ_L = 1.4–1.6 there, "in the lower range" of literature.
  - Obataya et al. defined the anisotropy index β = (E_L/G)·(tan δ_S/tan δ_L) and found it correlated with E/G over 101 species. Values were not accessible [OBA00].
  - **Use shear/L ratio ≈ 2 (1.4–2.6).**
- **Frequency dependence:**
  - "virtually frequency independent" from ~100–2000 Hz, rising above 2 kHz by up to ×2 [BKB10] (pub, snippet).
  - Spruce flat over ~10–2000 Hz (Rohloff; Krüger & Rohloff, cited in search results).
  - Bending can show apparent rising loss at higher modes because of the growing shear contribution [HU20 intro].
  - Chaigne & Doutaut model the bars with α(f) = γ_B/2 + 2π²·η_v·f² (s⁻¹) (fluid + Kelvin–Voigt) [MCA04 eq. 5] (pub).
  - **Model choice (inf):** constant η_L per mode up to 2 kHz, then η·(1 + (f−2000)/4000) up to ×2 at 6 kHz. For torsion and other shear-dominated modes use η_S ≈ 2·η_L.
- **Moisture:** tan δ_R is ×2–3 more sensitive to moisture than L properties [search, Brémaud & Gril 2020]. Extractives can shift tan δ by a factor of 2 [BRE10b].

### 3.4 Q / T60 of struck wooden bars (for calibration)
- Rosewood marimba (F&R's measurement):
  - E3: T60 **1.5 s with resonator, 3.2 s without**. That gives η = 2.2/(164.8·3.2) = 0.0042 (calc).
  - E6: 0.4 / 0.5 s, η ≈ 0.0033 (calc) [FR §19.4] (pub).
  - Synthetic bars are "somewhat" longer [FR].
- The resonator shortens T60 of the fundamental (it raises radiation efficiency) [FR], [HILL11] (pub).
- Marimba fundamental is visible ~5 ms after the blow; higher modes decline over the first tens of ms; the 3rd partial can dominate the first ~160 ms on one bar [lafavre] (pub, web). Low-weight source.
- Teponaztli whole-instrument η_eff 0.007–0.021 (§1.3, calc).

---

## 4. The cavity

### 4.1 Formulas (pub [FR] §1.6, §8.3)
- Helmholtz: **f_H = (c/2π)·√(A/(V·ℓ_eff))**.
  - ℓ_eff = t + end corrections.
  - For a circular hole the flanged end correction is 0.85·a per side [FR], unflanged 0.61·a.
  - For a slit of width a ≪ length, the per-side end correction is ~ (a/π)·[ln(slit length/a) + O(1)] (2-D radiation, logarithmic). It is only a few mm for 2–5 mm kerfs (inf/approx).
- Box (rigid-wall) modes: **f_lmn = (c/2)·√((l/Lx)² + (m/Ly)² + (n/Lz)²)** [THO15], [FR].
- Viscous boundary layer δ_v = √(2ν/ω) (ν = 1.5×10⁻⁵ m²/s): **0.22 / 0.155 / 0.11 / 0.077 mm at 100 / 200 / 400 / 800 Hz** (calc).
  - For a slit port of width a, viscous Q ≈ a/(2δ_v) (inf, boundary-layer approximation).
  - a = 3 mm: **Q ≈ 7 / 10 / 14 / 19**. a = 5 mm: 11 / 16 / 23 / 32 at those frequencies (calc).
  - Kerf "ports" are therefore lossy, broad resonators. A round hole of 4–5 cm has viscous Q ≫ 50; its Q is set by radiation (inf). Clay-vessel Helmholtz Q = 57 measured at 138 Hz [DAGA21] (pub, analogue).

### 4.2 Numbers
- **Ohio large drum** [THO15] (pub): gap A = 18.25 cm² (2.45 × 7.45 cm), neck 1.40 cm, box 57.2 × 8.8 × 13.7 cm.
  - The thesis computes **f_H = 237 Hz** with no end correction.
  - Adding flanged end corrections (r_eq = 2.41 cm, ℓ_eff ≈ 1.4 + 2·0.85·2.41 = 5.5 cm) gives **≈120 Hz** (calc).
  - Box f100 = 300 Hz measured as the strongest coupling: the 280 Hz tongue sustained longest. With a variable-length tube, sustain peaked at L_box = 0.42 m (409 Hz) and 0.61 m (280 Hz), matching c/(2f) to <1 cm (pub).
- **Small drum** [THO15]: 28.1 × 8.5 × 8.3 cm; f100 = 610 Hz, f010 ≈ 2018 Hz, f001 ≈ 2066 Hz. "No significantly large air gaps"; no tongue matched a box mode (pub).
- **Commercial 60 × 20 × 18 cm box, 15 mm walls** (interior ≈ 57 × 17 × 15 cm, V ≈ 14.5 L) (calc):
  - f100 ≈ 301 Hz, f200 ≈ 602 Hz, f010 ≈ 1009 Hz, f001 ≈ 1143 Hz.
  - Helmholtz with one 50 mm hole, 15 mm wall: ℓ_eff ≈ 15 + 2·0.85·25 = 57.5 mm, so **f_H ≈ 84 Hz** (below the tongue range).
  - With kerfs counted as ports (~3 m of 3 mm kerf, A ≈ 96 cm², ℓ_eff ≈ 20 mm), the kerfs dominate: **f_H ≈ 300 Hz, Q ≈ 10** (calc/inf). The kerfs are partly blocked by the moving tongues and the edge flow, so treat it as a broad 150–350 Hz bump (inf).
- **Nigerian slit gong** (snippet): f_H ≈ 349 Hz, close to the tab modes 384/439 Hz. The maker deliberately aligns mechanical and cavity frequencies [CAM12].
- **Teponaztli:** cavity open vs closed changed f0 by <0.6 Hz, so coupling is weak. Closing it shortened T60 by 7–27% and raised level (pub [TEP19], %: calc).

### 4.3 Coupling physics (calc/inf)
- **Stiffness loading is negligible.** k_air = ρc²·S_eff²/V with S_eff = 0.39·w·L.
  - Example: 120 × 40 × 10 mm padauk tongue, V = 7.5 L → k_air ≈ 67 N/m.
  - Tongue modal stiffness (tip-referenced, m_modal = 0.25·m_tongue ≈ 9 g at 449 Hz) ≈ 7×10⁴ N/m.
  - Δf/f ≈ +0.05%. Tongue pitch is set by wood, not air. This is consistent with teponaztli f0 barely changing when the cavity is opened.
- **Resonant coupling matters.** When a tongue's f1 lies within ~1/Q of the Helmholtz or a box mode, the air mode is driven, radiates efficiently, and the two beat (interference, [SHM06]). Sustain changes too: [THO15] saw longer ring at match. Teponaztli and Malekula anecdotes show the air mode can be the dominant pitch of a log drum.
- **Model suggestion (inf):**
  - One or two lossy air resonators (Helmholtz plus f100), each fed by the tongue's volume velocity (0.39·w·L·ẇ_tip for B1, −0.22 for B2).
  - Add a direct radiation path. Air Q ≈ 8–20 for kerf ports, 20–50 for a round port.
  - Feedback onto the tongue is optional: weak, Δf ≈ 0.
- **Radiation.** A tongue over a closed box radiates its front face like a baffled piston (monopole at low frequency). Narrow kerfs limit the front/back acoustic short circuit. Hence the advice to keep kerfs thin, and the "closed cavity louder" teponaztli result (inf; consistent with [TEP19], [FR] on resonators raising radiation efficiency).

---

## 5. Mallets and strike

### 5.1 Contact law and published parameters
- Hertz: F = K·δ^(3/2), K = (4/3)·E*·√R, 1/E* = (1−ν₁²)/E₁ + (1−ν₂²)/E₂ (pub, standard; used by [CD97]).
- Chaigne & Doutaut xylophone model (as re-used in [MCA04] Table II; pub): mallet **m = 0.012 kg, R = 0.012 m, K = 4.2×10⁸ N/m^1.5**. Bar E = 9.29 GPa, h = 20 mm, b = 40 mm, L = 0.20 m. Fluid damping γ_B = 8 s⁻¹. Kelvin–Voigt η_v swept 0.1–200 ns for the psychoacoustic stimuli.
- [ANT] (snippet): K = 10⁹ and 10⁷ N/m^1.5, M = 20 g, V = 1 m/s, "based on experimental data by Bork" (pub, unverified).
- Bork (1983b) via [FR] Fig. 19.12 (fig):
  - Frequency of maximum excitation vs mallet head speed (0.5–3.5 m/s, pp–ff) for 10 mallets: roughly 200 Hz–5 kHz.
  - All-rubber mallet: little velocity dependence. Rubber ring on wood core: strongly velocity dependent until it bottoms out. Hard-core yarn: >2-octave playing range.
  - For a sin² pulse of duration τ, **f_max = 0.85/τ** (FR prints "0.85τ"; the dimension says 0.85/τ).
- Mallet mass ≈ the vibrator's dynamic mass transfers maximum energy. Heavier mallets dwell longer and damp the high partials [FR §19.7] (pub). Tongue (cantilever B1, tip-referenced) dynamic mass = **0.25 × tongue mass** (calc). For a 120 × 40 × 12 mm padauk tongue (43 g) that is ≈11 g, matching a superball mallet head.

### 5.2 Contact durations (calc; rigid target; τ = 2.943·δ_max/v; wood transverse E ≈ 1.2 GPa included in E*)

| Mallet | K (N/m^1.5) | v = 0.3 m/s | 1 m/s | 3 m/s | f_max (0.85/τ) at 1 m/s |
|---|---|---|---|---|---|
| Superball rubber, E ≈ 3 MPa, R 12.5 mm, 12 g | 5.9×10⁵ | 3.4 ms | 2.7 ms | 2.2 ms | 310 Hz |
| Rubber, E ≈ 10 MPa, R 12.5 mm, 15 g | 1.9×10⁶ | 2.3 ms | 1.8 ms | 1.5 ms | 460 Hz |
| Hard rubber / soft plastic, E ≈ 100 MPa, 15 g | 1.7×10⁷ | 0.98 ms | 0.77 ms | 0.62 ms | 1.1 kHz |
| Hard plastic/acetal, E ≈ 3 GPa, R 12 mm, 20 g | 1.5×10⁸ | 0.46 ms | 0.36 ms | 0.29 ms | 2.3 kHz |
| Chaigne/McAdams mallet, 12 g | 4.2×10⁸ | 0.25 ms | 0.19 ms | 0.16 ms | 4.4 kHz |
| [ANT] soft / hard, 20 g | 10⁷ / 10⁹ | 1.36 / 0.22 ms | 1.07 / 0.17 ms | 0.86 / 0.14 ms | 0.8 / 5.0 kHz |

Notes on the table:
- Peak indentation for the superball at 1 m/s is ≈0.9 mm, peak force ≈16 N.
- τ ∝ m^(2/5)·K^(−2/5)·v^(−1/5). Rubber modulus values are inf: superball polybutadiene ~2–5 MPa, gum rubber ~1–10 MPa.
- A compliant tongue lengthens τ. When tongue modal mass ≲ mallet mass the mallet can stay on, or double-strike, and damp B1 (inf; same physics as piano hammer, [FR §12]).
- **Yarn/felt mallets:** strongly hardening (felt follows F ∝ δ^p with p ≈ 2–3.5 in Stulov-type models). Contact ~1–4 ms at playing speeds (inf).
- **Fingertip:** soft pulp, ~5–15 g effective (inf). τ ≈ 3–8 ms (inf). Nothing published for tongue drums.
- **Hunt–Crossley:** F = K·δ^α·(1 + λ·δ̇). With α = 1.5, the energy loss gives λ ≈ 3(1−e)/(2·v_in) [HC75] (pub, recalled). e(superball) ≈ 0.85–0.9 (inf), e(felt) ≈ 0.5–0.7 (inf).

### 5.3 Strike-position effects (calc from §2)
- Tip (x = L): all bending modes at full amplitude; brightest.
- x ≈ 0.78 L: B2 suppressed (node at 0.783 L); B1 at 0.70, B3 −0.50. Probably the "sweet spot" makers mark (inf).
- x ≈ 0.5 L: B1 0.34, B2 −0.71, B3 ≈ 0. Mid-tongue strikes favour B2.
- Off-centreline: T1 excitation ∝ lateral offset y·(x/L) (approximately linear twist shape, calc). Edge strikes at the tip put substantial energy into T1 at ≈1.5–4 × f1. Centre strikes give none.
- Box strikes (sides): wall plate modes plus air modes; the teponaztli body radiates around 700–1000 Hz [TEP19].

---

## 6. Published decay, spectra, recordings of slit and tongue drums
- **Teponaztli** [TEP19]: the full f0/T60/Δf table is in §1.3. Effective η 0.007–0.021, T60 0.39–2.0 s. No higher-partial data were published.
- **Nigerian slit gong** [CAM12], [SHM06]: two mechanical modes near two cavity modes; audible beating from interference. Numbers at snippet level only.
- **Tongue drums** [THO15]: f1 per tongue and qualitative short decay. The tongue near box f100 rings longest.
- **Kalimba** [CHA06]: dominant f0 plus a single overtone at 5.3–5.9 × f0, "sharp attack, rapid decay". It synthesises well as two exponentially damped sinusoids (pub). Ludwigsen: harmonic (integer) peaks beyond Euler–Bernoulli modes, from the root boundary nonlinearity/contact [KAL-L] (pub, abstract).
- **Nothing published** on per-tongue T60, B2 level re B1, or attack duration for wooden tongue drums.
- Inferred defaults:
  - **T60(B1) ≈ 0.6 s at 300 Hz** (η_eff ≈ 0.012), scaling 1/f (inf, from the teponaztli median).
  - **T60(B2) ≈ T60(B1)/(6·k)** with k = 1–1.5 for the rising η above 2 kHz (inf).
  - B2 level re B1 at a tip strike with a superball: the Hertz spectrum falls past f_max ≈ 300–450 Hz. B2 sits at 6 × f1 ≈ 1.8 kHz for a 300 Hz tongue, so the pulse spectrum there is past the first spectral zero (≈2/τ ≈ 740 Hz) and in the sidelobes, ~ −20 to −40 dB re its DC value (sin² pulse envelope, calc). A harder mallet (τ ≈ 0.4 ms) keeps B2 within ~−3 dB (calc).
  - Attack ≈ contact time, 2–3 ms (calc).

---

## Sources
- [TEP19] Anon. (authors per journal), "Wood identification and acoustic analysis of three original Aztec teponaztli musical instruments", *Madera y Bosques* 25(1) e2511690 (2019). Tables 4–6. https://www.scielo.org.mx/scielo.php?script=sci_arttext&pid=S1405-04712019000100219
- [MON] J. Montagu, "Slit Drums" (2019). https://www.jeremymontagu.co.uk/SlitDrums.pdf
- [CAM12] A.E. Cannaday, B.C. August, T.R. Moore, "Tuning the Nigerian slit gong", *JASA* 131(2):1566–1573 (2012), doi:10.1121/1.3675940. Abstract read: https://scholarship.rollins.edu/stud_fac/12/ ; numbers via search snippets of https://www.researchgate.net/publication/221849543 (not verified).
- [SHM06] Skubal, Hackett, Moore, APS March Meeting 2006 abstract B42.2 (Nigerian slit log gong). Via search snippet; https://archive.aps.org/mar/2006/b42/2 now 404.
- [THO15] S.C. Thompson, "Resonance in Tongue Drums", MS thesis, Ohio University (2015). Tables 1–8. https://etd.ohiolink.edu/acprod/odb_etd/ws/send_file/send?accession=ohiou1428423607&disposition=inline
- [FR] N.H. Fletcher & T.D. Rossing, *The Physics of Musical Instruments* (Springer; 1991/1998). §1.6 Helmholtz, §2.17 bar ratios, §2.20 torsion and Fig. 2.23, §19.1–19.4 (Table 19.1 wood data after Haines 1979; marimba T60), §19.7 mallets (Fig. 19.12 after Bork 1983b). doi:10.1007/978-1-4612-2980-3
- [WH10] USDA FPL, *Wood Handbook*, FPL-GTR-190 (2010), ch. 5, Tables 5-1, 5-2, 5-3a, 5-4a. https://www.fpl.fs.usda.gov/documnts/fplgtr/fpl_gtr190.pdf (copy read: https://ww2.arb.ca.gov/sites/default/files/barcu/regact/2014/capandtradeprf14/usfswoodhandbook2010.pdf)
- [WDB] E. Meier, The Wood Database. https://www.wood-database.com/african-padauk/ , /bubinga/ , /purpleheart/ , /black-walnut/ , /hard-maple/ , /honduran-mahogany/ , /black-cherry/
- [CAR18] C. Carlier, A. Alkadri, J. Gril, I. Brémaud, "Revisiting the notion of 'resonance wood' choice…", in *Wooden Musical Instruments* (WoodMusICK, 2018). arXiv:1902.10977. Table of spruce/maple tan δ_L, tan δ_R, tan δ_GLR, G_LR. https://arxiv.org/pdf/1902.10977
- [HU20] H.-T. Hu, O. Arnould, C. Carlier, G. Camp, S. Lotte, P. Langbour, I. Brémaud, "Vibrational measurement of shear modulus and damping of wood: Vybris-Torsion", GDR 3544 Sciences du bois, poster C07 (2020). https://gdr-sciences-du-bois.hub.inrae.fr/content/download/7472/73874?version=1
- [BRE10] I. Brémaud et al., "Identification of anisotropic vibrational properties of Padauk wood with interlocked grain", *Wood Sci Technol* 44:355–367 (2010) (abstract and erratum snippets only). https://link.springer.com/article/10.1007/s00226-010-0348-0 ; https://hal.science/hal-00544382
- [BRE10b] I. Brémaud et al., "Effect of extractives on vibrational properties of African Padauk (*Pterocarpus soyauxii*)", *Wood Sci Technol* (2010) (abstract only). https://link.springer.com/article/10.1007/s00226-010-0337-3
- [BRE-S] I. Brémaud et al., "Mechanical damping of wood as related to species classification: a preliminary survey" (tan δ 3.7–20‰, 445 species), via search snippet. https://www.researchgate.net/publication/264889346 ; see also Brémaud et al. 2012, *Ann For Sci* 69, doi:10.1007/s13595-011-0166-z, and Brémaud 2012, *JASA* 131:807, doi:10.1121/1.3651233 (not accessible).
- [BKB10] L. Brancheriau, C. Kouchade, I. Brémaud, "Internal friction measurement of tropical species by various acoustic methods", *J Wood Sci* 56 (2010), doi:10.1007/s10086-010-1111-8 (frequency-dependence statement via search snippet only).
- [OBA00] E. Obataya, T. Ono, M. Norimoto, "Vibrational properties of wood along the grain", *J Mater Sci* 35:2993–3001 (2000) (abstract only). https://link.springer.com/article/10.1023/A:1004782827844
- [HILL11] H. Hill, "Exponential Time Decay Constants of Marimba Bars", UIUC NSF REU report (2011). https://courses.physics.illinois.edu/phys406/sp2017/NSF_REU_Reports/2011_reu/Heather_Hill/Hhill1_FinalPaper.pdf
- [MCA04] S. McAdams, A. Chaigne, V. Roussarie, "The psychomechanics of simulated sound sources: Material properties of impacted bars", *JASA* 115(3) (2004). Table II, eq. 5. https://www.mcgill.ca/mpcl/files/mpcl/mcadams_2004_jasa_0.pdf
- [CD97] A. Chaigne, V. Doutaut, "Numerical simulations of xylophones. I. Time-domain modeling of the vibrating bars", *JASA* 101(1):539–557 (1997), doi:10.1121/1.418117 (abstract only).
- [ANT] Henrique & Antunes, "Optimal design and physical modelling of mallet percussion instruments" (K = 10⁷/10⁹ N/m^1.5, M = 20 g, V = 1 m/s; via search snippet). http://projects.itn.pt/OptiRen/File4.pdf (TLS error, not read).
- [BORK83] I. Bork, mallet shock spectra (1983b), via [FR] Fig. 19.12. Also I. Bork, "Practical tuning of xylophone bars and resonators", *Appl. Acoust.* 46:103–127 (1995).
- [CHA06] D.M.F. Chapman, "Investigating the sound of an African thumb piano (kalimba)", *Canadian Acoustics* 34(3) (2006). Table 1. https://jcaa.caa-aca.ca/index.php/jcaa/article/download/1805/1552/1942
- [MB] Mbira acoustic spectrum and FE study (overtones ≈5× and 14×), abstract via search. https://core.ac.uk/download/482072956.pdf
- [KAL-L] Ludwigsen et al., "Video analysis and modeling of the kalimba tine" (JASA abstract, doi:10.1121/1.3654995), and "Free vibration of a kalimba tine model beam with offset boundary condition" (doi:10.1121/1.5147345). Abstracts only.
- [LEI69] A.W. Leissa, *Vibration of Plates*, NASA SP-160 (1969), cantilever square plate (values recalled, not re-read).
- [HC75] K.H. Hunt, F.R.E. Crossley, "Coefficient of restitution interpreted as damping in vibroimpact", *J. Appl. Mech.* 42:440 (1975) (relation recalled).
- [SG] Schlagwerk 60C80/60P82/60D85 retailer specs. https://www.bax-shop.co.uk/melodic-percussion-instruments/schlagwerk-60c80-8-note-c-pentatonic-slit-drum ; https://bax-shop.it/percussioni-melodiche/schlagwerk-60p82-slitdrum-8-tonig-f-pentatonisch
- [DIY] Builder notes: https://pdgood.us/handdrum/tongue.html ; https://www.lamusiquesismique.fr/en/how-to-make-a-wooden-tongue-drum/ ; https://www.lumberjocks.com/threads/what-i-learned-making-tongue-drums.346372/ ; https://sawmillcreek.org/threads/tongue-drums-pics.20769/ ; http://www.thecarmichaelworkshop.com/2013/11/tongue-drums.html ; https://drummercafe.com/education/articles-commentary/how-to-build-your-own-slit-drum.html
- [MUS] Garamut museum records: https://www.famsf.org/artworks/slit-gong-garamut ; https://emuseum.desmoinesartcenter.org/objects/38153/garamut-slitgong-or-horizontal-drum ; https://www.metmuseum.org/art/collection/search/311262 ; https://collections.bowers.org/objects/136150/slit-gong-garamut
- [WIKI-SD] Wikipedia, "Slit drum". https://en.wikipedia.org/wiki/Slit_drum
- [UCT] UC thesis on teponaztli with a PVC two-tongue model (not read). https://escholarship.org/content/qt8vv4k7nb/qt8vv4k7nb.pdf
- [DAGA21] Clay-vessel Helmholtz resonance 138 Hz, Q 57, DAGA 2021. https://pub.dega-akustik.de/DAGA_2021/data/articles/000569.pdf (via search snippet)
- [KRZ] M. Krzyzaniak, *Timbral Learning for Musical Robots*, ch. 3 (hand-slap video timing). https://michaelkrzyzaniak.com/Research/Dissertation/chapter_03.php
- [lafavre] R. La Favre, marimba bar FFT pages. https://www.lafavre.us/FFT-mallet-position.htm
