# Khishba (خشبة) acoustics: hard numbers for a physical synthesis model

Compiled 2026-11-23 from the published literature, as the ground the model in `src/khishba.h` is built on (`docs/khishba.md` maps each of its constants to these numbers). Bracketed tags such as [GEI153] refer to the source list at the bottom.

How each value is marked:
- **pub**: stated in the source.
- **fig**: read off a published figure or notation image.
- **calc**: arithmetic on published numbers; the formulas are given in the text.
- **inf**: an estimate. Nothing has been published on it.

**State of the literature, bluntly.**
- There is **no acoustic measurement of a khishba** anywhere I could find: no modal frequencies, no spectra, no decay times.
- The instrument data are three museum catalogue entries [GEI153], [GEI154], [GEI155], one 1975 ethnomusicology paper with photos [KUB75], a Basra study of khashaba rhythms [NAJ], Wikipedia [WHA], a Grove/Oxford entry known only from its search snippet [GRO], and two journalistic pieces [AJZ], [GSP].
- For the drum physics, the closest measurement is a student near-field holography scan of a **doumbek** [SOR09], plus timpani air-loading numbers [RIE89]/[CHR84], a frame drum scan [TRO10], djembe values [ROS01]/[WDJ] and a goatskin stress measurement [LI22].
- So almost every khishba-specific acoustic number below is **calc** or **inf**, anchored to those sources.

**Naming trap (important).**
- "Khishba / khashaba / khashba / khisba" is (a) a **drum** (Kubica's "khashaba drum"; Geidai "khishba"), and (b) **al-khashaba**, the Basra percussion-and-maqam genre and its troupes [NAJ], [AJZ].
- The Basra genre today is played mainly on small **clay** goblet drums (*dinabik*, sing. *dunbuk*). The solo drum and its player are both called **kasur / kasura** [NAJ], [AJZ].
- [WHA] equates *khishba = zanboor = kasour*. [SUB] gives "Khishba, also known as the Zanbour" (Arabic for "wasp").
- The Iraqi **dunbug/dumbak** (also "tabla") of the Baghdad chalghi is a larger clay goblet drum [GEI151], [ELS]. It is a different instrument.

---

## 0. Numbers for the model (summary table)

| Quantity | Value to use | Range / notes | Tag | Source |
|---|---|---|---|---|
| Overall length | **35.5 cm** | 35.0, 35.5, 35.5 cm (three Geidai specimens) | pub | [GEI153–155] |
| Head (membrane) diameter | **9.5 cm** (a = 4.75 cm); small variant 8.0 cm | 8.0 / 8.0 / 9.5 cm. "The head of this drum is very small." | pub | [GEI153–155] |
| Shape | Long thin wooden tube, open at one end; "waisted handle drum", "excavated of one piece of wood"; Grove: "narrow hourglass" | Cup under the head → long narrow handle/neck → open foot | pub | [GEI153], [KUB75], [GRO] |
| Neck bore / foot opening | **bore ≈ 3.5 cm (r 1.75 cm); foot ≈ 4–6 cm** | No published value. From photos: the drum is hand-sized, the neck held in one hand. | inf | [KUB75] photos |
| Cup volume under head | **~0.2–0.45 L** | 9 cm head over a 4–7 cm deep cup | inf | — |
| Wall thickness | **4–8 mm** (wood) | Not published | inf | — |
| Body material | Wood, one piece (khishba); Basra successors in fired clay | Older Basra drums "made from wood", now "predominantly clay" | pub | [KUB75], [GEI153], [AJZ], [AZR24] |
| Head material | **Sheep skin** (all 3 museum pieces) or **fish skin** (Wikipedia; Basra dinabik "fish or goat skin"; kasura "sheep, goat or jirri fish") | Marsh-Arab mud drums: carp or pelican gullet preferred | pub | [GEI153], [WHA], [NAJ], [AJZ], [COI] |
| Head attachment | **Glued** to the body | Marsh drums: soaked skin + flour-and-water paste. The Iraqi clay *dumbak* is laced with cotton cord instead. | pub | [GEI153], [WHA], [COI], [GEI151] |
| Pre-playing treatment | Head **moistened** before playing (khishba) | Darbuka fish skins are tightened by **heat** (lamp inside, hair dryer) | pub | [WHA], [ARB] |
| Membrane surface density σ | **0.25 kg/m²** (sheep, thin); **0.15–0.25** fish | thin goat 0.15–0.30 mm thick; fish skin raw 0.17–0.30 mm; ρ_skin ≈ 1100–1300 kg/m³ (inf) | calc/inf | [MOT], [ZHA22], [LI22] |
| Tension T | **~1000–1500 N/m** (centre ~1200) | Range 500–2500. Bodhrán goatskin dry 1364 N/m, damp 472 N/m (calc from pub stress). Timpani Mylar 3990 N/m (pub). Fish skin breaks at ~4000–5000 N/m (calc). | calc/inf | [LI22], [RIE89], [ZHA22] |
| Membrane wave speed c_m = √(T/σ) | **~70 m/s** | 55–100 m/s | inf | — |
| (0,1) in vacuo | **~600 Hz** | 450–800 Hz. "High, dry sound" [GEI153]; the khashaba plays "two different tones in the interval of a perfect fifth" [KUB75]. | inf | — |
| (0,1) as heard (coupled) | **~570 Hz**, plus a tube-coupled partner at **~700–870 Hz** | Mode repulsion with the tube's ¾-wave resonance (~700 Hz) | calc (model) | §3.4 |
| Air-loading shift on higher modes | **(1,1) ×0.91, (2,1) ×0.94, (3,1) ×0.96** (σ = 0.25, a = 4.75 cm) | Timpani: ×0.66 / ×0.75 / ×0.79. Scales with μ = ρ_air·a/σ (0.23 here vs 1.50 on timpani). | calc | [RIE89], §2.2 |
| (1,1)/(0,1) ratio | **~1.5** (a fifth) | Ideal 1.593; doumbek measured 1.40; open frame drum 1.75 | calc/pub | [SOR09], [TRO10] |
| Low air mode (cup + neck "Helmholtz") | **~120–165 Hz**, weak, Q ~ 60–90 | Rigid-head Helmholtz 164–181 Hz. The compliant head lowers it. Nobody reports an audible bass on the khishba. | calc/inf | §3 |
| Tube resonances (rigid head, cylinder 35.5 cm) | **230 / 690 / 1150 Hz** (closed–open) | L_eff = L + 0.61 r | calc | §3.2 |
| T60 of "dum" (head-dominated modes) | **~100–200 ms** | Radiation-only Q ≈ 54 at 600 Hz → 200 ms; with skin loss Q ≈ 30 → 110–150 ms | calc/inf | §2.6, §3.4 |
| T60 of "tek" (high modes) | **~20–60 ms** | (2,1)…(4,1) at 0.9–1.8 kHz with Q 10–30 | inf | — |
| Skin internal loss | **Q_int ≈ 20–40** (η 0.025–0.05); fish ≈ 20, sheep/goat ≈ 30–40 | No published value for drum skin | inf | — |
| Finger contact time | **tek 0.7–2 ms; dum 3–8 ms; closed slap 3–5 ms** | Drumstick on a drum head 3–6 ms and 5–8 ms (pub). No hand-drum data exist. | inf | [WAG06], [DAH] |
| Strike velocity | **1–3 m/s** fingers; up to ~4 m/s for slaps | Sticks at *f* reach 4–6 m/s (pub) | inf/pub | [DAH] |
| Effective mass | **tek 10–20 g** (fingertip/finger); **dum 0.1–0.3 kg** (fingers + palm + wrist); slap 0.3–0.4 kg | Hand ≈ 0.6% of body mass ≈ 0.45 kg | inf | — |
| Hertz contact law F = K·δ^1.5 | **K ≈ 1–6 × 10⁶ N/m^1.5** | Lumped finger pad + local membrane; chosen to reproduce the contact times above | calc/inf | §4.3 |
| Pitch glide (tension modulation) | **+1–3% at 1 mm peak (0,1) amplitude**; ≈ +12% at 2 mm for a stiff dry skin | Bass drum: 6 mm amplitude → +10% (pub) | calc/pub | [ROS92], §4.5 |
| Fish vs goat/sheep | Fish: thinner and lighter (σ 0.15–0.2), lossier (Q_int ~ 20), more humidity-sensitive, "higher" and brighter; tears in very dry air. Hydration of goatskin drops T by 65% and f by ~57%. | | pub/inf | [ARB], [NAS], [LI22] |
| Rhythms | hacha'a 2/4 `D...T.T.`; jurjuna 10 `D.TT.D.T..`; maqsum `D.T...T.D...T...` | Full table in §6 | fig/pub | [MW], [KUB75], [BYM] |

---

## 1. The khishba itself

### 1.1 What it is (corrected premise)
- **Kubica 1975 [KUB75]** is the only first-hand organological description I found. It is based on 1965 field recordings near Ramadi. Its words on the drum:
  - "Khashaba is a waisted handle drum with a single membrane."
  - It is "excavated of one piece of wood".
  - **Playing position:** seated, the player places it "on his left knee", using both hands. Standing, it is "suspended on his neck and shoulders".
  - **Repertoire:** characteristic of Iraqi Roma (*kawliya*) music. It is used "not only in the Iraqi bedouin music but **sometimes** also by … Chalghi Baghdadi".
  - **Role:** besides plain accompaniment, it is known for "deafening ornamental passages".
  - **Ensemble:** a kawliya group must include at least one rebaba fiddle (often a tin-canister rebaba, for loudness in the open air), one khashaba drum, singers and dancers. Women sometimes add small metal castanets.
  - All pub.
- **Geidai (Tokyo University of the Arts) catalogue [GEI153]:**
  - Wooden body, long and thin, one end open. Sheep-skin head, "very small", **glued** to the body.
  - Slung from the shoulder on a cord and held at an angle across the body. Struck with the fingers of both hands.
  - Sound: "a high, dry tone". "Usually used today in drum ensembles."
  - All pub.
- **Wikipedia, Hacha'a [WHA]:**
  - The drum is called khishba, zanboor or kasour. "Narrow tube-shaped body made of wood", "fish-skin head glued on top".
  - The head is **wetted before playing**. It is struck rapidly with both hands, giving "a fast-paced roll characteristic of the style". The roll is "many beats faster than the main beat of the song".
  - All pub. Secondary source.
- **Grove via Oxford Reference [GRO]:** "single-headed narrow hourglass drum used by Roma people in Iraq". I have only the search snippet; the page itself returned 403.
- **Sublime Frequencies notes [SUB]:** "a unique nomadic hand drum called the Khishba", also "the Zanbour". It provides "fast, machine-gun-like rhythms" over the main tempo in *choubi* and *hecha* pop/folk. pub.
- **Basra *khashaba* genre [NAJ], [AJZ], [GSP], [AZR24]:**
  - The name is traced to shipyard carpenters (*khashashba*) who sang and clapped with pieces of wood. A minority view dates it to the Zanj revolt (255 AH) [NAJ].
  - The drums are **dinabik**: "small drums made of fired clay, the face covered from above with **fish skin or goat skin**" [NAJ, my translation].
  - The name *dinabik* is said to come from "dum" and "tak" [NAJ].
  - The kasura (solo drum) was **formerly wood, now pottery**, with skins from "sheep, goats, or the jirri fish" [AJZ]. *Jirri* (جري) is the Iraqi name for a scaleless catfish (my gloss).
  - [GSP] calls the khishba "explosive". It says that "khishba and kassur [are] now essential to Khashaba". A troupe (*shadda*) has **20–30 members** [GSP]. [NAJ] gives "about 20–30" (the PDF's digits are garbled).
  - Line-up: the maqam singer; about 8–10 *mawazim* (ensemble dunbuk players); **one kasur** (solo dunbuk who deliberately "breaks" the meter, *taksir*); clappers; dancer; leader (*jawish*) [NAJ].
  - Strings (oud, violin) were added in the late 1950s [NAJ]. pub.
- **Chalghi baghdadi** (the classical Iraqi maqam ensemble) per Amir ElSaffar [ELS]: santur, jowza, **dumbug** (goblet drum), riqq, naqqarat.
  - So the user's premise ("khishba alongside santur, jowza, riqq, naqqara") is only **sometimes** true [KUB75]. The standard chalghi goblet drum is the larger clay dumbug/tabla. pub.
- **Marsh Arab mud drums [COI]** (Ochsenschlager 2004):
  - Tall *tabol* built from 4–16 cm clay strips mixed with reed fluff.
  - Head leather: "**carp and pelican gullet** are considered preferable". Soaked overnight, stretched, fixed with **flour-and-water paste**.
  - Replaced by baked pottery drums. None were left by 1990. pub.

### 1.2 Dimensions (all catalogue values)

| Specimen | Body length | Head Ø | Body | Head | Attachment | Tag |
|---|---|---|---|---|---|---|
| Geidai 153 khishba | 35.5 cm | **9.5 cm** | wood | sheep | glued | pub [GEI153] |
| Geidai 154 khishba | 35.5 cm | 8.0 cm | wood | sheep | — | pub [GEI154] |
| Geidai 155 khishba | 35.0 cm | 8.0 cm | wood | sheep | — | pub [GEI155] |
| Geidai 151 Iraqi *tabla/dumbak* | 41.0 cm | 17.0 cm | terracotta | sheep | **laced, cotton cord** | pub [GEI151] |
| Geidai 141 Iraqi *naqqare* (pair) | depth 8.9 cm | 18.0 / 18.2 cm | terracotta | camel hide | — (tuned a M3 or P4 apart) | pub [GEI141] |
| Egyptian darbuka, Gawharet el Fan 17″ (metal) | 42.5 cm | 26.25 cm (outer) | metal | synthetic | mechanical | pub [DPL] |
| Meinl HE-215 / HE-315 (Turkish-style aluminium) | 39.4 / 40.6 cm | 20.1 / 21.0 cm | Al / Cu | synthetic | mechanical | pub [THO] |
| Persian tombak | ~46 cm | ~28 cm | mulberry/walnut | goat/camel, glued | glued | pub [WTO], retailers |
| Met Iranian/Arabian ceramic darabukka | ~36 cm | ~16.4 cm | ceramic | — | — | pub [MET] |
| Djembe | 58–63 cm | 30–38 cm outer; 31 cm playing surface | hardwood | goat | rope | pub [WDJ] |

**Derived proportions (calc).**
- Khishba length/head-diameter ≈ **3.7–4.4**. Darbuka ≈ 1.6–2.0. Tombak ≈ 1.6. Djembe ≈ 1.8.
- So the khishba is acoustically a **long tube with a tiny head**, not a bowl-plus-short-neck like a darbuka.
- Head area: π·(4.75 cm)² = **71 cm²** vs ~380 cm² for a 22 cm darbuka (**5.4× smaller**).

**Not published (inf):**
- Neck bore. From the [KUB75] photos the drum is held at the waist by one hand, which suggests an inner bore of ~3–4 cm and an outer diameter of ~5 cm.
- Foot opening ~4–6 cm.
- Cup depth under the head ~4–7 cm.
- Wall 4–8 mm.
- Wood species: unknown. Mulberry and apricot are common for carved goblet drums in the region (inf).

### 1.3 How it differs from its relatives
- **Egyptian darbuka/tabla.**
  - Clay (traditionally) or aluminium. Bowl ~2.5–3.5 L, 20–22 cm head, rounded edge.
  - Traditional head is fish skin (glued, heat-tuned) [NAS], [ARB]. About 90% of Egyptian darbukas now ship with transparent synthetic heads of 0.3/0.6 mm [ARB].
  - Strong Helmholtz "dum" (calc ~100–150 Hz, §3.3).
- **Turkish darbuka.** Metal, exposed sharp edge ("the Turkish style exposes the edge of the head"), which allows split-finger snaps but hinders rolls [WGD]. pub.
- **Persian zarb/tombak.**
  - One-piece wood, goat/camel skin glued on, ~28 cm head, ~46 cm long. Heads are "not very tight"; players prefer a lower pitch [NAS].
  - Played with fingers and a "floppy wrist" roll [NAS]. Pitch is lowered by wetting the head [NAS].
- **Iraqi dumbug/dumbak (tabla).** 41 cm × 17 cm head, terracotta, **sheep skin laced with cotton cord**, played across both knees. "Centre gives a low tone, edge a sharp dry sound" [GEI151].
- **Khishba.** Same family (single-headed, goblet/hourglass), but:
  - A head **~2× smaller in diameter** (a quarter of the area).
  - A **long narrow wooden handle-tube** instead of a bowl.
  - A glued thin head (sheep or fish).
  - Slung and played with fingers for rapid ornamental rolls.
  - Expected acoustics (inf): no real "dum" bass, a high (0,1) around 450–800 Hz, short decays, and a tube-coloured mid-range.

### 1.4 Recordings and players
- Kubica's 1965 recordings near Ramadi [KUB75]:
  - Singer Bernau Karim. Rebaba player Yusif Rashid ("Yusif Hacha").
  - Bint ar-Rif (Baghdad radio/TV singer) with ud and khashaba.
  - A swahli at **♩ = 164** (pub), where the khashaba player "accompanied this swahli with two different tones in the interval of a **perfect fifth**" using different touches. pub.
  - Kubica's Fig. 61 shows two khashaba players; the head is roughly palm-sized (fig).
- **Saad al-Yabis** (Basra), "legendary khisba player". He entered khashaba in the early 1970s, has given ~250 international concerts and makes his own kasuras [GSP], [AJZ].
- Other Basra kasurs: troupes named after Mulla Adnan, Rabee', Talib al-Saeed, Hadi al-Khashab, Khalaf al-Saeed [AJZ], [NAJ].
- **Sattar Al-Saadi**: track titled "Kasor [Khishba Solo]" (Last.fm listing; unverified).
- **Sublime Frequencies**, *Choubi Choubi! Folk and Pop Sounds from Iraq* (comp. Mark Gergis, 2010/2014) has khishba on many tracks [SUB].
- **Amir Al-Azraki** (Univ. Waterloo) plays khishba and maruwas [AZR24].
- A Reuters/Screenocean clip "'Khashaba' music and its rhythms from Basra" exists but redirects behind a paywall.
- No spectrum or acoustic analysis of any of these was found.

---

## 2. Membrane physics on a small head

### 2.1 Ideal circular membrane (calc, scipy Bessel zeros)
f_mn = j_mn·c_m/(2πa), with c_m = √(T/σ) and j01 = 2.4048.

| mode | (0,1) | (1,1) | (2,1) | (0,2) | (3,1) | (1,2) | (4,1) | (2,2) | (0,3) | (5,1) | (3,2) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| f/f01 | 1.000 | 1.593 | 2.136 | 2.295 | 2.653 | 2.917 | 3.155 | 3.500 | 3.599 | 3.652 | 4.059 |

Useful modal integrals for the (0,1) mode with ψ = J0(j01 r/a):
- Modal mass M = σ·πa²·J1(j01)² = **0.2695 σπa²**.
- Volume-velocity-equivalent area S_eq = 2πa²J1(j01)/j01 = **0.432 πa²**.
- All calc.

### 2.2 Air loading and the enclosed air: timpani reference numbers
Christian et al. 1984 [CHR84], with parameters reproduced by Rienstra 1989 [RIE89]:
- Head 2a = 0.656 m, **σ = 0.2653 kg/m² (Mylar; 0.262 in [CHR84])**, **T = 3990 N/m**, kettle volume 0.14 m³, vent hole Ø 28 mm. pub.
- Realistic tension range 3000–5000 N/m [RIE89]. Standard Mylar timpani head **0.19 mm** [ROS92]. pub.

| mode | in vacuo [RIE89] | in air + kettle [RIE89] | ratio | β = ((f_vac/f)²−1)/μ, μ = ρa/σ = 1.50 |
|---|---|---|---|---|
| (0,1) | 143.1 Hz | 126.9 Hz | 0.887 | 0.18 (net: air mass minus kettle stiffness) |
| (1,1) | 228.0 | 150.9 | **0.662** | 0.86 |
| (2,1) | 305.6 | 227.7 | 0.745 | 0.53 |
| (3,1) | 379.7 | 300.2 | 0.791 | 0.40 |
| (4,1) | 451.6 | 370.6 | 0.821 | 0.32 |
| (0,2) | 328.5 | 252.3 | 0.768 | 0.46 |

The frequencies are pub; ratios and β are calc. Other points from these sources:
- **Radiation-only T60 (pub) [RIE89]:** (0,1) **0.79 s**, (1,1) **9.1 s**, (2,1) 28 s, (3,1) 101 s. Monopole modes radiate fast; multipole modes barely radiate. Real decay is set by internal and air losses.
- **Kettle volume (pub) [ROS92], [CHR84]:** reducing the kettle volume *raises* the axisymmetric (0,1), (0,2), (0,3) modes (stiffer enclosed air) and *lowers* the (m,1) modes (more air-mass loading). Kettle shape has "little or no effect".
- **Scaling to the khishba (calc).**
  - The mass-loading parameter is μ = ρ_air·a/σ. For a = 4.75 cm, σ = 0.25: **μ = 0.23**. For fish skin (σ = 0.15): μ = 0.38. Timpani: μ = 1.50.
  - Using the timpani β per mode: f/f_vac = 1/√(1+βμ) gives **(1,1) ×0.91, (2,1) ×0.94, (3,1) ×0.96** (σ = 0.25) and **(1,1) ×0.87, (2,1) ×0.91** (fish, σ = 0.15).
  - So the small head is **only mildly air-loaded**. Air loading cannot "harmonise" it the way it does a timpano. The (0,1) is instead dominated by the coupling to the tube (§3.4).
- **Bodhrán consistency check (calc), from [LI22]:**
  - Dry goatskin T = 3.79 MPa × 0.36 mm = **1364 N/m**. With ρ_skin = 1200 kg/m³, σ = 0.43, a = 0.205 m: f01,vac = **105 Hz**. Measured **84 Hz** → air factor 0.80 at μ = 0.57.
  - That is consistent with open-frame air loading (β ≈ 1 for (0,1) on an open frame).

### 2.3 Measured modal data on related drums

| Drum | (0,1) | (1,1) | (2,1) | (0,2) | (3,1) | (1,2) | (2,2) | Notes | Tag |
|---|---|---|---|---|---|---|---|---|---|
| **Doumbek** (slim, synthetic head with central dark dot) | **313 Hz** | 440 (1.40) | — | 565 (1.81) | 845 (2.70) | 787 (2.51) | 962 (3.07) | (1,2) and (3,1) swap order. (3,1) and (1,2) drift 4–6 Hz with climate. | pub [SOR09] |
| 12″ Remo frame drum (FiberSkyn, open back) | 176.7 Hz | 309.5 (**1.75**) | 426.2 | 426.2 | 520 | 575/615 | 662.6 | (0,1) *depressed* by air mass on both sides | pub [TRO10] |
| Djembe | bass (Helmholtz) **65–80 Hz**; tone (0,1) **343 & 401 Hz** (split pair) | tonpalo (1,1) **568 Hz** | slap (2,1) **812 Hz** | | | | | Slap partials to >4 kHz. >105 dB SPL possible. | pub/fig [WDJ], [ROS01] |
| Turi-daiko (29 cm × 7 cm deep) | 195 Hz | | | | | | | "fundamental is lowered by increasing the length (volume)" | pub [ROS01] |
| Snare (batter/snare pair) | 183 Hz (heads in phase) / 330 Hz (opposite) | 275 / 344 | 405 | 451 | | | | | pub [ROS92] |

The doumbek's **(1,1)/(0,1) = 1.40**, below the ideal 1.59. That is the signature of the (0,1) being pushed *up* by the stiffness of the enclosed/body air (calc interpretation). The open frame drum's 1.75 is the opposite case: (0,1) pulled *down* by unbaffled air mass.

### 2.4 Membrane materials

| Material | Thickness | Density | σ | E | Notes | Tag |
|---|---|---|---|---|---|---|
| Mylar (PET) timpani | 0.19 mm | ~1390 kg/m³ | **0.262–0.265 kg/m²** | ~3.5 GPa | humidity-insensitive | pub [ROS92], [RIE89]; E pub (patent) |
| Synthetic darbuka (Egyptian clear) | 0.3 / 0.6 mm | ~1390 | 0.42 / 0.83 | | "open sound", strong dum | pub [ARB]; σ calc |
| Remo FiberSkyn / Power Beat | 0.8 mm | — | — | | crisp, more bass | pub [ARB] |
| Goatskin (bodhrán) | **0.36 ± 0.03 mm** | (1200 inf) | 0.43 (calc) | **680 MPa dry / 330 MPa damp** | stress 3.79 → 1.31 MPa on wetting; strain 0.28 → 0.20%; f 84 → 36 Hz | pub [LI22] |
| Goat hide, West African (supplier bands) | thin 0.15–0.30; med-thin 0.35–0.55; medium 0.60–0.75; thick >1 mm | | 0.2–1.3 (calc) | | djembe heads ~1–1.3 mm | pub [MOT] |
| Goat leather (vegetable-tanned, lab) | 0.81 mm | 950 kg/m³ | 0.77 (calc) | 74/66 MPa (radial/circ) | Low-quality PINN paper; its stated "typical" tensions of 200–650 N/m are implausibly low (calc: give f01 ≈ 40–75 Hz on a 30 cm head) | pub [SHI26], caution |
| Fish skin, striped bass (wet) | ~0.3 mm | — | ~0.3 (calc, ρ 1100) | 13 MPa at 0% → 54 MPa at 20% strain; rupture 10–15 MPa at 30–40% strain | | pub [ZHA22] |
| Fish skin, raw (Albanian study, via search snippet) | 0.174–0.213 mm | — | 0.19–0.26 (calc) | tensile 23–32 MPa | not verified in full text | pub (snippet) |
| Sheep skin (khishba) | not published; thin "parchment" 0.15–0.3 mm | ~1200 | **0.2–0.35** | ~0.3–0.7 GPa dry (by analogy with goat) | | inf |

**Tension ceilings (calc).** Fish-skin rupture × thickness: 14 MPa × 0.3 mm = **4200 N/m**; 25 MPa × 0.2 mm = 5000 N/m. A khishba at T ≈ 1200 N/m would run at ~25–30% of rupture. That fits the reports that fish heads tear in dry air or when over-heated [ARB].

**Tension ↔ pitch for the khishba (calc).** T = σ(2πa·f01/j01)².

| a | σ | f01,vac 450 Hz | 600 Hz | 800 Hz |
|---|---|---|---|---|
| 4.75 cm | 0.15 (fish) | 468 N/m | 832 N/m | 1479 N/m |
| 4.75 cm | 0.25 (thin sheep) | 780 | **1386** | 2464 |
| 4.75 cm | 0.40 (thicker) | 1248 | 2218 | 3943 |
| 4.00 cm | 0.25 | 553 | 983 | 1748 |

Wave speed c_m = 55.8 / 74.5 / 99.3 m/s for f01 = 450 / 600 / 800 Hz at a = 4.75 cm (calc).

**Why f01 ≈ 600 Hz (inf).**
- (i) "High, dry tone" [GEI153].
- (ii) The doumbek's (0,1) is 313 Hz on a head ~2.3× larger. The same membrane wave speed would give ~700 Hz on 9.5 cm.
- (iii) Fish/sheep heads glued and heat-tensioned can reach 1000–2000 N/m before tearing.
- (iv) The head is moistened before playing [WHA], which *lowers* T (goatskin: −65% stress on hydration [LI22]). A wetted khishba head may sit at the low end, 400–550 Hz.

### 2.5 Fish skin versus goat/sheep skin
- **Thickness and mass.** Fish skin is 0.17–0.3 mm raw [ZHA22] vs goat 0.15–1.3 mm [MOT]. Drum-grade fish heads are "very thin" [NAS], [ARB]. So **σ ≈ 0.15–0.25 vs 0.25–1.2 kg/m²** (calc).
- **Sound (practitioner claims).**
  - Fish: "an amazing resonance and higher sound" [ARB]; "bright, lively, clear, crisp" (retailers).
  - Goat: thickness lets you choose warmer/darker [ARB]. Persian players prefer lower, looser goat [NAS].
  - No measured comparison exists.
- **Humidity.** Fish is "more influenced by humidity than synthetic" (riq makers). It needs heat (lamp inside the drum, hair dryer) to re-tension [ARB], and **tears in very dry climates** [ARB].
- **Goatskin hydration [LI22], measured.** Stress −65% (3.79 → 1.31 MPa), E −51% (680 → 330 MPa), pitch 84 → 36 Hz (−57%, more than √T predicts (−41%) because hydration also raises σ). pub/calc.
- **Physics consequences of a lighter head at equal pitch (calc).**
  - Lower T (∝ σ).
  - Larger air loading μ, so a bit more downward shift of the (m,1) modes.
  - **Lower radiation Q** (Q_rad ∝ σ): at 600 Hz, Q_rad = 54 for σ = 0.25 → **32 for σ = 0.15**, i.e. T60_rad 200 → 120 ms.
  - Larger velocity per unit impulse (∝ 1/σ), so it is **louder and brighter at the attack** and dies faster.
- **Internal damping (inf).**
  - Collagen skins: η ≈ 0.02–0.05 (Q_int 20–50). Thin fish skin is wetter, with more mucopolysaccharide, so probably lossier: Q_int ≈ 15–25.
  - Mylar is much less lossy (timpani decays of seconds imply Q_int ≳ 200).
  - The fish skin (0.3 mm) "energy dissipation" hysteresis is large: the stress–strain loop in [ZHA22] encloses a big area.

### 2.6 Radiation damping of the (0,1) mode (calc)
Q_rad = 4πc·M/(ρω·S_eq²), monopole into free space from one face:

| case | ka | Q_rad (monopole) | T60_rad | Q_rad (dipole, open frame, d ≈ 1.2a) | T60 |
|---|---|---|---|---|---|
| Khishba a = 4.5 cm, σ = 0.25, 600 Hz | 0.50 | **54** | 200 ms | ~460 | 1.7 s |
| Khishba fish σ = 0.15, 600 Hz | 0.50 | **32** | 120 ms | ~280 | 1.0 s |
| Darbuka a = 11 cm, σ = 0.30, 313 Hz | 0.63 | 21 | 150 ms | ~110 | 0.77 s |
| Bodhrán-like a = 20.5 cm, σ = 0.43, 84 Hz | 0.32 | 32 | 840 ms | ~670 | 17 s |

The dipole column is a crude estimate (inf on the separation d).

---

## 3. The air column / body

### 3.1 Two limiting pictures
- **Darbuka/djembe = Helmholtz resonator.** A large bowl volume V plus a short neck of area S and effective length L′: f_H = (c/2π)√(S/(V·L′)).
  - The membrane acts as a compliant wall of the bowl.
  - Djembe bass 70–80 Hz "appears to be due to the Helmholtz resonance of the shell" [ROS01]; 65–80 Hz, independent of skin tension [WDJ].
  - Check (calc): V = 16 L, neck r = 6 cm, L = 30 cm → **74 Hz**.
- **Khishba = long narrow tube with a tiny head.** Length 35.5 cm ≫ head radius. Volume under the head is small (~0.2–0.45 L, inf).
  - The tube's own standing waves sit in the same range as the membrane modes.

### 3.2 Tube resonances (calc)
Cylinder of length L = 0.355 m, open-end correction 0.6133·r (unflanged). Membrane end treated as rigid (closed) or as pressure-release (open):

| bore/foot radius | L_eff | closed–open f1 / f2 / f3 (= (2n−1)c/4L_eff) | open–open c/2L′ (head as compliant/open end, +0.61a) |
|---|---|---|---|
| 2.0 cm | 0.367 m | **233 / 700 / 1167 Hz** | 434 Hz |
| 2.5 cm | 0.370 m | 232 / 695 / 1158 | 431 |
| 3.0 cm | 0.373 m | 230 / 689 / 1148 | 428 |

### 3.3 Waisted shape: cup + neck ("Helmholtz") estimate (calc)
f_H = (c/2π)√(S_n/(V_c·L′)), L′ = L_n + (0.61 + 0.85)r_n:

| cup V_c | neck r_n | neck L_n | f_H (rigid head) | L′/λ |
|---|---|---|---|---|
| 0.20 L | 1.5 cm | 30 cm | 181 Hz | 0.17 |
| 0.30 L | 1.8 cm | 29 cm | 179 Hz | 0.16 |
| 0.45 L | 2.0 cm | 28 cm | 164 Hz | 0.15 |

- The neck is 0.15–0.17 λ long, so the lumped formula is only marginal. The transmission-line model in §3.4 is better.
- **Darbuka for comparison (calc, rigid head):** V = 2.5–3.5 L, neck r = 3.5–4.5 cm, L = 15–18 cm → **148–151 Hz**. With a compliant head it drops, probably to **~100–140 Hz** (inf).
- No published darbuka "dum" frequency was found. Practitioner sources place the tek at 500 Hz–2 kHz [DON].

### 3.4 Coupled model: membrane (0,1) + cup + neck (calc, my lumped/transmission-line model)
Model setup:
- Membrane (0,1) is a piston with M and S_eq (§2.1), stiffness K = M(2πf01,vac)², internal loss via Q_int.
- External face radiates as a baffled piston: Z = ρc/S·(½(ka)² + j·8ka/3π).
- Internal face drives the cup compliance (V_c) in parallel with a lossy cylindrical neck: r_n = 1.8 cm, L_n = 29 cm, visco-thermal α ≈ 3×10⁻⁵√f/r Np/m, unflanged open end.
- Peaks are taken from the membrane velocity under a point force at the centre. Q is the −3 dB width.
- Baseline: a = 4.5 cm, σ = 0.25, V_c = 0.30 L, Q_int = 30.

| f01,vac | air mode | lower head mode | upper head mode | tube 5/4-λ | comment |
|---|---|---|---|---|---|
| 450 Hz | 117 Hz, Q 69, foot +7.5 dB over head | **515 Hz, Q 28, T60 117 ms** | 687 Hz, Q 34, T60 108 ms | 1181 Hz, Q 10 | |
| **600 Hz** | 131 Hz, Q 80, foot +11 dB | **568 Hz, Q 38, T60 145 ms** | **740 Hz, Q 21, T60 62 ms** | 1182 Hz, Q 12 | baseline |
| 800 Hz | 141 Hz, Q 86 | 598 Hz, Q 57 (tube-like) | **866 Hz, Q 15, T60 37 ms** (head-like) | 1184 Hz | |
| 600, Q_int = ∞ | 131 Hz, Q 97 | 568 Hz, Q 55, T60 214 ms | 740 Hz, Q 32, T60 94 ms | | radiation + tube losses only |
| 600, fish (σ 0.15, Q_int 20) | 119 Hz, Q 69 | 564 Hz, Q 33, T60 128 ms | 784 Hz, Q 14, T60 38 ms | 1186 Hz | wider split (lighter head couples more strongly) |
| 600, thicker (σ 0.40, Q_int 40) | 139 Hz, Q 85 | 572 Hz, Q 42, T60 163 ms | 708 Hz, Q 30, T60 94 ms | | |
| 600, wider bore r 2.5 cm | 165 Hz, Q 87 | 563 Hz, Q 29 | 755 Hz, Q 23 | 1197 Hz | |

Reading of the model (calc/inf):
1. **The (0,1) is split into two partials ~570 Hz and ~700–870 Hz.** This is mode repulsion with the tube's ¾-wave resonance (~690 Hz for a rigid end).
   - The membrane frequency sits where the tube's input reactance is mass-like (kL ∈ (π, 3π/2) …), so the head mode is pulled down while the tube mode is pushed up.
   - Splitting is ~150–250 Hz, larger for lighter heads.
   - A synth should therefore model the "dum" of the khishba as **two damped partials ~1:1.3** rather than one.
2. **A weak low air mode at ~115–165 Hz** (cup + neck with compliant head) radiates mostly from the **foot** (+8 to +15 dB volume velocity vs the head).
   - Its high Q (60–90) but small residue means little energy in a stroke.
   - None of the descriptions mention a bass on the khishba ("high, dry"). Treat it as a faint body thump, −15 to −25 dB (inf).
   - Covering or partly blocking the foot with the hand/thigh would lower and damp it further (inf, by analogy with the udu hand-over-hole effect).
3. **Decays:** head-dominated partials T60 ≈ **40–150 ms**; the dum body ≈ 100–200 ms. The tube's 5/4-λ mode at ~1.18 kHz (Q ~10–20) colours the tek region.
4. For a cylinder without a cup (pure tube), the low air mode would sit near the closed–open 230 Hz instead (calc §3.2).

### 3.5 Comparison: what the "dum" is on each drum

| drum | low "dum" mechanism | frequency | Tag |
|---|---|---|---|
| Djembe | Helmholtz of the shell | 65–80 Hz | pub [WDJ], [ROS01] |
| Darbuka | Helmholtz bowl + neck, coupled to the (0,1) | ~100–150 Hz (no published value) | calc/inf |
| Doumbek (0,1) | membrane, stiffened by body air | 313 Hz | pub [SOR09] |
| Khishba | membrane (0,1) split by the tube | **~500–600 Hz and ~700–870 Hz** | calc/inf |
| Khishba air mode | cup + neck | ~115–165 Hz, weak | calc/inf |

---

## 4. Strokes

### 4.1 Vocabulary
- **Dum** (د / D): centre, open, the low sustained stroke. **Tek/tak** (T): edge, dominant hand, short and sharp. **Ka** (K): edge, weak hand. **Pa / sak**: closed, "resting the hand briefly on the head". Snaps, slaps, pops and rolls are ornaments.
  - Sources: [WGD], [ELS] (dum = "sustained, low-pitched", tek = "short, high-pitched"), [MW] (dum "bassy and sustained", tak "dry and sharp"). pub.
- **Turkish split-finger** (parmak) snaps need the exposed sharp edge of the Turkish drum [WGD]. pub. Unlikely on a glued-head wooden khishba (inf).
- **Iraqi dumbak:** centre = low tone, edge = "sharp, dry sound" [GEI151]. pub.
- **Khishba-specific:**
  - Played with "the fingers of both hands" [GEI153].
  - "Rapid strikes … fast-paced roll" [WHA]; "machine-gun-like" [SUB]; "deafening ornamental passages" [KUB75].
  - Two tones a fifth apart from "different touches" [KUB75].
  - All pub.
- **Fifth interpretation (calc).** Take (0,1) as the dum, shifted by tube coupling ×~0.95, and (1,1) as the edge tone, shifted by air loading ×0.91. Ratio = 1.593·0.914/0.947 ≈ **1.54**, within ~50 cents of a fifth (1.50).
- **Rolls (*rash*, *rashat*):** alternating fingers. The rate is not published. Estimate **12–20 strokes/s** for a skilled player (tabla/darbuka rolls are commonly quoted in this range; inf).
- **Damping with the resting hand:** the non-playing hand lies on the head or edge. Model it as an added mass-damper patch on the outer ring that kills (0,1)-family modes more than edge-localised modes (inf).

### 4.2 Where you strike: which modes ring (calc)
Modal velocity per unit impulse ∝ ψ_mn(r0)/N_mn. Values are dB relative to (0,1) struck at the centre. Air coupling and the contact-time filter are not included.

| mode (ratio) | r = 0 | 0.3a | 0.5a | 0.7a | 0.8a | 0.9a | 0.95a |
|---|---|---|---|---|---|---|---|
| (0,1) 1.00 | 0.0 | −1.2 | −3.5 | −7.8 | −11.4 | **−17.7** | **−23.9** |
| (1,1) 1.59 | — | +4.1 | +5.7 | +3.4 | +0.4 | −5.5 | −11.6 |
| (2,1) 2.14 | — | +1.1 | +6.6 | +6.4 | +3.9 | −1.6 | −7.7 |
| (0,2) 2.30 | +7.3 | −0.1 | −8.1 | −0.6 | −2.1 | −7.2 | −13.1 |
| (3,1) 2.65 | — | −3.1 | +6.3 | +8.3 | +6.4 | +1.2 | −4.7 |
| (4,1) 3.16 | — | −7.9 | +5.4 | +9.5 | +8.3 | +3.5 | −2.3 |
| (0,3) 3.60 | +11.3 | −9.2 | +2.3 | −4.3 | +0.8 | −2.0 | −7.4 |

- **Edge strikes:** going from centre to 0.9a costs the (0,1) ~18 dB, while (2,1)…(4,1) stay within a few dB of their maxima. Near the edge J_m(j r/a) ≈ J_m′(j)·j(r/a − 1), so all modes fall ∝ (1 − r/a) and the *relative* weighting favours higher modes.
- **Centre strikes:** excite only m = 0 modes. A real dum with the fingers spread over ~0.5a excites (0,1) strongest because (0,2) and (0,3) partly cancel over the contact area (inf).

### 4.3 Contact, velocity, mass (no hand-drum measurements exist)
- **Published anchors:**
  - Drumstick–head contact **3–6 ms** on snare [WAG06].
  - **5–8 ms** for a mezzoforte centre stroke on tom/rototom/snare; contact durations 4.5–8 ms at slow tempi [DAH] (survey).
  - Contact time falls with tension and near the rim [WAG06].
  - Striking velocities of sticks at *f*: **4–6 m/s** [DAH].
  - Finger pulp static stiffness at contact points **0.2–7.7 N/mm**, compliant below 1 N and stiffening rapidly above [PG], [SER97].
  - Tapping studies: forces vary 3×, energy dissipation ~81% in the pulp [BEC03].
  - Djembe slap: palm lands, then fingertips ~40 ms later (video-frame estimate) [KRZ].
- **Bandwidth argument (calc).** A half-sine force pulse of duration t_c is −3 dB at 600 Hz for t_c = 1 ms, −15 dB for 2 ms, −37 dB for 4 ms. The first spectral zero is at 1.5/t_c.
  - Practitioner tek energy spans 0.5–2 kHz [DON].
  - So a bright khishba tek needs **t_c ≲ 1–1.5 ms**; a dum with 4–8 ms contact naturally suppresses everything above ~300–500 Hz.
- **Recommended values (inf):**

| stroke | effective mass | velocity | contact t_c | Hertz K (F = Kδ^1.5) | δ_max | F_max |
|---|---|---|---|---|---|---|
| tek (fingertip at edge, partly on the rim) | 15 g | 1–2 m/s | **0.7–2 ms** | 1.1×10⁶ (2 ms) … 6×10⁶ (1 ms) | 1.4 / 0.7 mm | ~55 / ~100 N |
| ka (weak hand) | 10–15 g | 1–1.5 m/s | 1–2 ms | ~1–3×10⁶ | ~1 mm | 30–60 N |
| dum (flat fingers + palm, centre) | 0.2 kg | 2 m/s | **4–8 ms** | 2.6×10⁵ (10 ms) … 9.4×10⁵ (6 ms) | 4–7 mm | 150–250 N |
| closed slap / sak (hand stays) | 0.35 kg | 3 m/s | 3–5 ms, then a **damping hold** (contact persists) | ~4×10⁶ | 4 mm | ~1 kN peak (probably too high; F caps through hand compliance) |

- Hertz relations (calc): t_c = 2.94·(5m/4K)^0.4·v^−0.2 and δ_max = (5mv²/4K)^0.4.
- **Sanity check (calc):** treating the finger mass on the pulp in series with the static local membrane stiffness 2πT/ln(a/b) (≈3–9 N/mm at T = 1200 N/m) gives 7–30 ms. That is too long for a "dry" tek. So for a tek the finger must hit near the rigid edge (higher local stiffness) and/or rebound by muscle action. This supports the edge position of tek strokes.

### 4.4 Spectra and decays: dum vs tek
- **No published spectrum of a darbuka or khishba dum vs tek was found.**
  - [KRZ15] classifies darbuka *doum, tek, pa* from the spectral centroid etc. of the first 50 ms (95% accuracy) but gives no centroid values.
  - Djembe (published) [WDJ]: bass 65–80 Hz; tone 300–420 Hz (the (0,1) pair at 343/401 Hz); slap 700–1000 Hz with partials beyond 4 kHz. A slap emphasises (2,1), (0,2), (3,1), (1,2), (0,3) and suppresses the Helmholtz, (0,1) and (1,1).
  - Tabla (for analogy) [TABW]: shorter decays for Re/Te/Tu/Na, longer for Ghe/Ge/Tee/Teen.
- **Khishba estimates (inf, from §3.4 and §4.2):**
  - **Dum:** partials ~570 + ~740 Hz, centroid **~700–900 Hz**, T60 **100–200 ms**, plus a faint 120–160 Hz body mode.
  - **Tek:** (1,1) ~850–900 Hz, (2,1) ~1.2 kHz, (3,1) ~1.5 kHz, (4,1) ~1.8 kHz, plus the tube's ~1.18 kHz. Centroid **~1.5–2.5 kHz**, T60 **20–60 ms**.
  - **Pa/sak:** broadband click, effective T60 **<15 ms**.

### 4.5 Nonlinear pitch glide (tension modulation)
- **Bass drum (pub) [ROS92]:** "initial drumhead amplitude is 6 mm, which results in an upward frequency shift of about 10%".
- **Khishba (calc).** For the (0,1) shape, the mean added membrane strain is ε ≈ 0.78·A²/a². Then ΔT = E·h·ε/(1−ν) and Δf/f = √(1+ΔT/T) − 1, with T = 1200 N/m and a = 4.5 cm.
  - E = 680 MPa, h = 0.2 mm (dry stiff skin): A = 0.3 / 1 / 2 mm → **+0.3% / +3.1% / +11.8%**.
  - E = 330 MPa (damp skin): **+0.1% / +1.5% / +5.9%**.
  - E = 50 MPa (soft, wet fish skin): +0.03% / +0.3% / +1.1%.
- So a hard dum on a dry head gives a perceptible downward glide (~½ semitone over the first ~50 ms). A **moistened** head (khishba practice [WHA]) glides much less (inf).

---

## 5. Goblet vs frame drums (quantified where possible)
- **Mode ratio evidence (pub):**
  - Open-backed 12″ frame drum: (1,1)/(0,1) = **1.75** [TRO10].
  - Closed-body doumbek: **1.40** [SOR09].
  - Ideal: 1.59.
  - The frame drum's (0,1) is lowered by air mass sloshing around the rim. The goblet's (0,1) is raised by the body-air stiffness (or split by the tube, for the khishba).
- **Radiation (calc, §2.6):**
  - A goblet drum's (0,1) radiates as a **monopole** from the head (plus the in-phase/out-of-phase foot or neck source).
  - An open frame radiates as a **dipole**: back and front faces cancel for ka < 1.
  - For a khishba-sized head at 600 Hz, monopole/dipole radiated power ≈ **8.5×** (Q_rad 54 vs ~460).
  - For a 22 cm head at 313 Hz ≈ 5×. For a bodhrán-sized head at 84 Hz ≈ 20×.
  - So the frame drum's (0,1) is weak *as a radiator* but rings long (low radiation loss). Its sound is carried by higher modes and the jingles (riq).
- **Frame drums of the region (pub):** riq/riqq frame "preferably lemon tree wood", head "fish or calf skin", modern Mylar [MWR]. Iraqi riq/reqq has a sheep hide [GEI-reqq via search]; daf (Kurdish) is goat or fish.
- **Two-headed drums** (snare, bass, o-daiko): (0,1) splits into in-phase and anti-phase pairs (183/330 Hz snare; 168/193 → 202/227 Hz o-daiko with humidity) [ROS92], [ROS01]. pub. Not relevant to the single-headed khishba except as a reminder that **humidity moves skin-head modes by ~15–20%** (o-daiko 168 → 202 Hz, calc +20%).

---

## 6. Iraqi rhythms (dum/tek patterns)
Notation: one character per 16th note. `D` dum, `T` tek, `K` ka/weak-hand tek, `.` rest (or filler).

| Rhythm | Meter | Pattern (16ths) | Source and tag |
|---|---|---|---|
| **Hacha'a / hacha / haja** (2/4 folk, Iraqi Roma) | 2/4 | `D...T.T.` | [MW] hacha' 2/4 "D s T T" in 8ths (fig); [BYM] "Haja (Iraq) 2/4: D . . . T . T ." (pub). [KUB75] (fig): Shuaib Ibrahim's pattern ♩ ♫ = `X...X.X.`; Kubica's extended 2-bar form ♩ ♫ ♫ ♫ = `X...X.X.|X.X.X.X.` (stroke types not marked). |
| Hacha' (Sufi/urban 4/4 form) | 4/4 | `D...T.T.T...T.T.` | [MW] (fig): "D s T T T s T T" in 8ths |
| **Jurjuna / jurjina / curcuna** | 10/8 (written 10/16 when fast) | `D.TT.D.T..` (one char per 8th or 16th; grouping 3+2+2+3) | [MW] (fig): "D s T T s D s T s s"; [BYM] (pub) same in 20 counts: `D . . . T . T . . . D . . . T . . . . .`; fill variant `D...T.T...D.T.T...T.`; [OFG]: accents 1, 4, 6, 8 |
| Yuruk sama'i | 6/8 | `D.T.T.D.T...` (8ths: D T T D T s) | [MW] (fig), [FRI] |
| **Sengin (sanakin) sama'i** | 6/4 | `D...T...T...D...T.......` (beats: D T T D T –) | [FRI] (fig) |
| Maqsum (used for "Foag el-Nakhal" + chobi dance) | 4/4 | `D.T...T.D...T...` | [MW] (fig) "D T s T D s T s"; [BYM]; [CAR] says the chobi song uses maqsum |
| Malfuf | 2/4 | `D..T..T.` | [MW], [BYM] |
| Ayyub | 2/4 | `D..KD.T.` | [BYM] |
| Fallahi | 2/4 | fast maqsum `D.T.T.D.` (one common realisation) | [WDR] ("speedier variation of the Maqsum, 2/4 feel"); pattern inf |
| **Chobi / choubi / jobi** (Iraqi dabke) | 2/4, fast | **not published** in any source I found. Best guess: `D.TKD.T.` or ayyub-like `D..KD.T.`, with the zurna/mijwiz and tabl driving every 8th | inf. [CAR] links the chobi dance to iqa' maqsum. [FAN], [LDB] describe only the dance. |
| **Khashaba (Basra) lazma / basta** | 4/4 "of the simple family" | Notated only as images in [NAJ] (not extractable). Described as having the same meter as masmudi saghir and maqsum but "different in its dums and teks". The kasur deliberately displaces dum/tek accents against the mawazim. | pub (text) |
| Basra "janubi" | 2/4 | (image only) | pub [NAJ] |
| **Yugrug** (Iraqi maqam iqa') | 12/4 | **not found** as a dum/tek pattern | pub (existence and meter): iraqimaqam glossary; ElSaffar: the Baghdadi maqam uses 8 iqa'at [ELS]; Iraqi folk uses 38 [CAR] |
| Sama'i thaqil | 10/4 (10/8 in [MW]) | `D.......T.......D...D...T.......` (approx. from [FRI]: D – – – T – D D T –) | [FRI] (fig, approximate) |

Tempo anchors:
- Swahli with khashaba at **♩ = 164** [KUB75] (pub).
- Hacha'a melodic phrases are "mostly … six bars of 2/4" [KUB75] (pub).
- Khishba rolls "many beats faster than the main beat" [WHA]. Estimate: rolls in 32nds at ♩ ≈ 120–160 → **16–21 strokes/s** (calc/inf).

---

## 7. What is not published (so the model must assume it)
- Any acoustic measurement of a khishba, kasura or Basra dunbuk: modes, spectra, Helmholtz/tube frequencies, decay times.
- Bore, foot diameter, cup volume and wall thickness of any khishba. Only length and head diameter are catalogued.
- The tension, surface density and internal loss of fish-skin or sheep-skin drum heads. Only bodhrán goatskin stress [LI22] and fish-skin tensile data for non-drum purposes [ZHA22] exist.
- Finger/palm contact times, velocities and effective masses on any hand drum. Only stick data exist.
- A darbuka Helmholtz "dum" frequency. Only djembe values are published.
- Dum/tek patterns for chobi and yugrug in any accessible source. Basra khashaba patterns exist only as notation images in [NAJ].

---

## Sources
- [AJZ] Ammar al-Salih, "الكاسورة.. آلة عزف وإيقاع شعبي اشتهرت من البصرة" (The kasura: a folk percussion instrument famous from Basra), Al Jazeera Net, 9 Apr 2019. https://www.aljazeera.net/arts/2019/4/9/الكاسورة-آلة-عزف-وإيقاع-شعبي-اشتهرت-من
- [ARB] ArabInstruments, "Which Darbuka Skin / Head Will be the Best Choice – Doumbek Skin Full Review". https://arabinstruments.com/help-you-choose-a-doumbek-skin
- [AZR24] Amir Al-Azraki, "How drums tell the story of Black Arab heritage", 6 Aug 2024 (syndicated). https://www.inlandtown.com/how-drums-tell-the-story-of-black-arab-heritage/ ; also https://funtimesmagazine.com/2024/09/07/504376/how-drums-tell-the-story-of-black-arab-heritage
- [BEC03] D. L. Jindrich, Y. Zhou, T. Becker, J. T. Dennerlein, "Non-linear viscoelastic models predict fingertip pulp force-displacement characteristics during voluntary tapping", J. Biomech. 36(4), 497–503 (2003). https://stacks.cdc.gov/view/cdc/214532
- [BYM] BabaYagaMusic, "Oriental dance rhythm diagrams and descriptions". https://www.babayagamusic.com/Music/oriental-dance-rhythm-diagrams-and-descriptions.htm
- [CAR] Carnegie Hall Musical Explorers, Layth (Iraqi folk), Lessons 1–2. https://www.carnegiehall.org/Education/Programs/Musical-Explorers/New-York-City/Layth/Lesson-1 ; https://carnegiehall.org/Education/Programs/Musical-Explorers/Digital/Program-Six/Layth/Lesson-2
- [CHR84] R. S. Christian, R. E. Davis, A. Tubis, C. A. Anderson, R. I. Mills, T. D. Rossing, "Effects of air loading on timpani membrane vibrations", J. Acoust. Soc. Am. 76, 1336–1345 (1984). Parameters taken via [RIE89].
- [COI] Crafts of Iraq (Univ. of Victoria), "al-Hiba: Mud Drum Makers" (after E. Ochsenschlager, *Iraq's Marsh Arabs in the Garden of Eden*, 2004, pp. 74–75). https://craftsofiraq.uvic.ca/al-hiba-mud-drum-makers/
- [DAH] S. Dahl et al., "Striking movements: a survey of motion analysis of percussionists" (Acoust. Sci. Tech. 2011) and Dahl & Altenmüller, "Motor control in drumming", Acoustics'08 Paris 2aMUb1. https://www.researchgate.net/publication/270711356 ; https://acoustics.org/pressroom/httpdocs/155th/dahl.htm ; http://www.sofiadahl.net/pdf/DahletalForumAcusticum2011.pdf
- [DON] Donizetti Darbuka, "Darbuka 'Tek' (T) stroke mastery guide" (practitioner). https://www.donizettidarbuka.com/en/post/darbuka-single-t-stroke-mastery-guide
- [DPL] Darbuka Planet product pages, Gawharet el Fan 17″ and 19″ Dohola. https://www.darbukaplanet.com/products/17-black-metal-gawharet-el-fan-darbuka-doumbek ; https://www.darbukaplanet.com/19-gawharet-el-fan-black-metal-dohola-darbuka.html
- [ELS] Amir ElSaffar, "Iraqi Maqam". https://www.amirelsaffar.com/iraqi-maqm
- [FAN] Fanack, "The Choby Dance: When Iraqis Shake the Ground". https://fanack.com/iraq/culture-of-iraq/the-choby-dance-when-iraqis-shake-the-ground/
- [FRI] M. Frishkopf, "Some Arab rhythmic cycles (durub, awzan, iqa'at)", chart rev. 2009, Univ. of Alberta. https://sites.ualberta.ca/~michaelf/Theory/Durub%20chart%20rev%202009.pdf
- [GEI141], [GEI151], [GEI153], [GEI154], [GEI155] Koizumi Fumio collection catalogue, Tokyo University of the Arts (Geidai): naqqare 141; tabla/dumbak 151; khishba 153, 154, 155. https://www.geidai.ac.jp/labs/koizumi/english/gcato/html-text/153.html (and …/141.html, …/154.html, …/155.html; 151 at https://www.geidai.ac.jp/labs/koizumi/gcat0/english/html-text/151.html)
- [GRO] "Khishba", Grove Dictionary of Musical Instruments, via Oxford Reference (only the search snippet was seen; the page returned 403). https://www.oxfordreference.com/view/10.1093/acref/9780199743391.001.0001/acref-9780199743391-e-3854
- [GSP] "Assala: Iraqi Dance and Its Unknown Dance Styles", Gilded Serpent, 23 Jul 2016. https://www.gildedserpent.com/cms/2016/07/23/assala-iraqi-dance-and-its-unknown-dance-styles/
- [KRZ], [KRZ15] M. Krzyzaniak & G. Paine, "Realtime classification of hand-drum strokes", NIME 2015. https://nime.org/proceedings/2015/nime2015_147.pdf ; Krzyzaniak dissertation ch. 3. https://michaelkrzyzaniak.com/Research/Dissertation/chapter_03.php
- [KUB75] V. Kubica, "The Gipsy Music of Iraq", Annals of the Náprstek Museum 8, Prague 1975, pp. 45–62. https://publikace.nm.cz/file/2b83a90b5850d74e33a46b29f13ffd1c/26421/ANpM_1975_2_Kubica_ocred.pdf
- [LDB] Learn Dabke, "Dabke Chobi / Jobi: the Iraqi folk dance". https://learndabke.co/dabke-chobi-jobi-iraqi-folk-dance/
- [LI22] G.-Y. Li, A. L. Gower, M. Destrade, S.-H. Yun, "Non-destructive mapping of stress and strain in soft thin films through sound waves", Communications Physics 5, 231 (2022). https://doi.org/10.1038/s42005-022-01000-3
- [MET] Metropolitan Museum of Art, darabukka 89.4.335 (via search summary). https://www.metmuseum.org/art/collection/search/500962
- [MOT] Motherland Music, goat skin thickness grades. https://motherlandmusic.com/products/parts/drum-skins/goat-skin/
- [MW] MaqamWorld, iqa'at index and notation images (jurjina, hachaa_2_4, hachaa_4_4, yuruk_semai, maqsum, malfuf, ayyub). https://ftp.maqamworld.com/en/iqaa.php ; https://ftp.maqamworld.com/en/iqaa/jurjina.php ; https://ftp.maqamworld.com/note/iqaa/jurjina.png
- [MWR] MaqamWorld, Riqq page. https://ftp.maqamworld.com/en/instr/riqq.php
- [NAJ] Ali Najm Abdullah (Kirkuk Univ.), "جماليات استخدام الضروب الايقاعية في خشابة البصرة" / "The aesthetics of using percussion patterns in Basrah khashaba", Nabu Journal for Research and Studies (Univ. of Babylon) vol. 30, no. 37 (April; the year digits are garbled in the PDF, c. 2022–23). https://naboo.uobabylon.edu.iq/index.php/nabo/article/download/467/278/287
- [NAS] P. Nasehpour, "The differences among tonbak, darbouka and djembe". https://sites.google.com/view/nasehpour/the-differences-among-tonbak-darbouka-and-djembe
- [OFG] Oud for Guitarists, "Jurjunah rhythm". https://www.oudforguitarists.com/jurjunah-rhythm/
- [PG] Pérez-González et al., fingertip contact stiffness (static, 0.2–7.7 N/mm), via search summary. https://core.ac.uk/download/61432218.pdf
- [RIE89] S. W. Rienstra, "The acoustics of a kettledrum", IWDE report 89-10, TU Eindhoven (1989). https://pure.tue.nl/ws/files/1716044/716604.pdf
- [ROS01] T. D. Rossing, "Acoustics of percussion instruments: Recent progress", Acoust. Sci. & Tech. 22(3), 177–188 (2001). https://www.jstage.jst.go.jp/article/ast/22/3/22_3_177/_article
- [ROS92] T. D. Rossing, "Acoustics of drums", Physics Today 45(3), 40–47 (March 1992). https://aip.brightspotcdn.com/PTO.v45.i3.40_1.online.pdf
- [SER97] E. R. Serina, C. D. Mote, D. Rempel, "Force response of the fingertip pulp to repeated compression", J. Biomech. 30, 1035–1040 (1997) (abstract via search).
- [SHI26] W. Shi, S. Huang, "Material selection and tone control mechanism of leather drumheads for percussion instruments", Advanced Electromagnetics 15(3), 2456–… (Aug 2026). https://www.aemjournal.org/index.php/AEM/article/download/3299/983 (use with caution)
- [SOR09] K. P. Sørensen, "Drum Head Modal Vibrations", Physics 498POM student report, Univ. of Illinois (2009/2010). https://courses.physics.illinois.edu/phys406/sp2017/Student_Projects/Spring09/Knud_Sorensen/K_Sorensen_Phys498POM_Spring09_Final_Report.pdf
- [SUB] Sublime Frequencies, *Choubi Choubi! Folk and Pop Sounds from Iraq*, comp. Mark Gergis (2010/2014), liner notes. https://sublime-frequencies.bandcamp.com/album/choubi-choubi-folk-and-pop-songs-from-iraq
- [TABW] "Categorization of Tablas by Wavelet Analysis", arXiv:1601.02489 (via search summary). https://arxiv.org/pdf/1601.02489
- [THO] Thomann product pages, Meinl HE-215 / HE-315 darbukas. https://m.thomann.de/se/meinl_he_215_copper_darabuka.htm ; https://www.thomann.ae/meinl_he315_jingle_darabuka.htm
- [TRO10] G. Tronel, "Near-Field Acoustic Holography: The Frame Drum", REU report, Univ. of Illinois (2010). https://courses.physics.illinois.edu/phys406/sp2017/NSF_REU_Reports/2010_reu/Gregoire_Tronel/Gregoire_Tronel_Final_Report.pdf
- [WAG06] A. Wagner, "Analysis of Drumbeats – Interaction between Drummer, Drumstick and Instrument", MSc thesis, KTH (2006). https://www.speech.kth.se/publications/masterprojects/2006/AndreasWagner.pdf
- [WDJ] Wikipedia, "Djembe" (acoustics section; its citations include Rossing 2000, A. Prak's djembe FAQ and M. Henning's Djembe Forum posts). https://en.wikipedia.org/wiki/Djembe
- [WDR] Wikipedia, "Dumbek rhythms". https://en.wikipedia.org/wiki/Dumbek_rhythms
- [WGD] Wikipedia, "Goblet drum". https://en.wikipedia.org/wiki/Goblet_drum
- [WHA] Wikipedia, "Hacha'a". https://en.wikipedia.org/wiki/Hacha'a
- [WTO] Wikipedia, "Tombak". https://en.wikipedia.com/wiki/Tombak
- [ZHA22] E. Zhang, C.-H. Tung, L. Feng, Y. R. Zhou, "Superior damage tolerance of fish skins", arXiv:2210.14651 (2022). https://www.arxiv.org/pdf/2210.14651
- Iraqi maqam glossary (yugrug 12/4): https://iraqimaqam.blogspot.com/p/glossary-of-terms.html
- Further background, not directly cited: euphonics.org §3.6 "Tuned drums" (J. Woodhouse), https://euphonics.org/3-6-tuned-drums/ ; Rhaouti, Chaigne & Joly, "Time-domain modeling and numerical simulation of a kettledrum", JASA 105(6) (1999), doi:10.1121/1.424679 (parameters not retrieved).
