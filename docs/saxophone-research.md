# The saxophone: what is known

This is the survey the Lead's saxophones (`src/saxophone.h`) are built on.
`docs/saxophone.md` maps each of the model's constants to what is here.
Sources are listed at the end. I read most of them in full. Those marked
**sec.** I knew only from an abstract or from other papers citing them.

## 1. The reed and the mouthpiece

A saxophone has a single cane reed held by a ligature against the lay of
the mouthpiece. Between the reed's tip and the mouthpiece's tip rail is a
slit, a millimetre or two at rest: alto mouthpiece tip openings run from
1.4 to 2.6 mm [OCVCP21]. The player's lower lip lies on the reed and the
upper teeth on the mouthpiece.

- **A valve shut by pressure.** The air flows into the mouthpiece through
  the slit, driven by the difference between the mouth pressure and the
  mouthpiece pressure (Bernoulli: U = w·H·√(2Δp/ρ), with a jet that does
  not recover its pressure). That same difference pushes the reed toward
  the lay. Blowing harder therefore narrows the slit. The flow first rises
  with Δp, then falls, and stops entirely once Δp exceeds the closing
  pressure P_M = K·H0. This is the "inward-striking" valve of Helmholtz's
  classification. It can drive a resonator only *below* its own resonance
  [WB74, FR98]. The falling half of the flow curve is a negative
  resistance: it gives the bore energy at a resonance whose input
  impedance is high enough.
- **Dimensionless control.** Kergomard's parameters capture it: the mouth
  pressure over the closing pressure, γ = P_mouth/P_M, and the embouchure
  parameter ζ = Z_c·w·H0·√(2/(ρP_M)), where Z_c is the bore's
  characteristic impedance at the mouthpiece. For a lossless cylinder the
  reed starts to oscillate at γ = 1/3, beats from γ = 1/2, and stops at
  γ = 1. Losses raise the threshold. With the reed's dynamics included,
  the oscillation condition is ζ(√γ·Re D − (1−γ)/2√γ) = 1/Z(ω) at the
  playing frequency, where D(ω) is the reed's response
  [KGS16 sec., GKV05].
- **The reed with the lip on it.** Muñoz Arancón et al. measured a tenor
  reed in playing conditions [MGDC16 sec., via GKG24 Table A7]:
  - stiffness per unit area K_a = 4.8 × 10⁶ Pa/m;
  - resonance 672 Hz;
  - Q = 1.13 (q_r = 0.88).

  The lip damps the reed heavily: an unloaded clarinet reed has a Q of 4
  to 33. Colinot et al. model the alto with ω_r = 4224 rad/s (672 Hz),
  q_r = 1 and ζ = 0.6 [CVGD21 Table 1]. Kergomard et al. use ζ = 0.65
  for a soprano [KGSVDGK19]. No measured P_M was found. K_a·H0 with H0 of
  0.5 to 1 mm gives 2.4 to 4.8 kPa.
- **Blowing pressures.** Alto saxophonists blow:
  - about 2.0 kPa at pianissimo;
  - 1.6 to 2.8 kPa at mezzo forte;
  - 2.5 to 4.4 kPa at fortissimo;
  - up to 8 kPa at the extremes [FS96].

  Against a P_M of 3 to 5 kPa, that is γ ≈ 0.4 to 1.
- **The reed's equivalent volume.** The air the reed's swing sweeps acts as
  an added volume at the mouthpiece. For a tenor it is 3.2 ± 0.5 ml; for a
  soprano 1.5 ml [CSW11]. The flow that the moving reed's damping makes
  lag the pressure adds to it in the same way.
- **The lay.** The reed does not shut at once. It rolls onto the lay's
  curve from the tip, so the slit closes progressively. The measured
  quasi-static flow therefore approaches zero smoothly near closure
  [DGO03 sec., AvW04 sec.]. In subtone the lower lip covers more of the
  reed and cushions that closure even more.
- **Regimes.** In the idealised saxophone (a cone with a short cylinder
  standing in for its tip), the oscillation is an analogue of the bowed
  string's Helmholtz motion. The reed is shut during a short episode of
  each period, a fraction β = x1/(L + x1) of it, where x1 is the missing
  cone's length and L the bore's. In time that is 2·x1/c, whatever the
  note [ODK04 sec., KGKDG13]. That fraction is 0.15 to 0.38 across the
  first register. In simulation the first register appears between
  γ ≈ 0.3 and 0.45 (0.45 in practice) and goes out near γ = 1.75. The
  second register is stable for γ = 0.66 to 0.79 on the same fingering,
  so the registers are multistable [CVGD21].

## 2. The conical bore

- **Geometry.** A saxophone's body is a truncated cone:
  - alto: half angle 0.028 rad (1.60°), crook input radius 6.25 mm
    [PCKG20 Table A1], missing cone x1 ≈ 0.196 m [KGSVDGK19 Fig. 12];
  - tenor: half angle 1.52°, total length 1490 mm including the
    mouthpiece [CSW09];
  - soprano: 1.74°, missing tip 0.126 m, its volume 3.35 cm³, against a
    mouthpiece of 2.25 cm³ and 44 mm.
- **Why a cone has every harmonic.** A complete cone has the resonances
  of an open pipe of the same length, at every multiple of c/2L. A
  cylinder closed by the reed has only the odd ones (c/4L, 3c/4L, …).
  This is why a saxophone sounds its whole harmonic series while a
  clarinet's low register favours the odd harmonics [FR98, B76 sec.].
- **Truncation.** A truncated cone closed where the tip was cut has
  resonances that move from the complete cone's toward a closed cylinder's
  as the frequency rises: tan(kL) = −k·x1. They are inharmonic, stretched
  sharp. The mouthpiece stands in for the missing tip. Benade's rule asks
  for the same volume as the tip, together with the reed's equivalent
  volume, and the same natural frequency [B76 sec.]. A mouthpiece volume
  that is too large flattens the upper resonances against the lower; too
  small, it sharpens them [KGKDG13].
- **Measured resonances.** Colinot et al. fitted a measured alto's low
  written D♯ (a Buffet Senzo) with modes [CVGD21 Table 2]:

  | Mode | Frequency | Q | Peak |
  |---|---|---|---|
  | 1 | 190 Hz | 34 | 10.0 Z_c |
  | 2 | 395 Hz | 35 | 13.3 Z_c |
  | 3 | 593 Hz | 28.5 | 9.9 Z_c |
  | 4 | 820 Hz | 37 | 7.7 Z_c |

  So f₂/f₁ = 2.08 and f₃/f₁ = 3.12: sharp of harmonic, as the
  truncation makes them.
  - A tenor's lowest note has an input impedance peak of about
    11 MPa·s/m³, with the reed's compliance in parallel. This rises to
    93 at written D5, is 50 to 80 in the second register, and falls to
    5 to 31 in the altissimo [CSW11 Fig. 8].
  - The soprano's lowest note has resonances at 207, 411, 606, 824 and
    1063 Hz (ratios 1, 1.99, 2.93, 3.98, 5.14) [CSW09].
- **Losses.** Visco-thermal wall losses give an attenuation of about
  3 × 10⁻⁵·√f / r per metre (r in metres) [FR98]. On a smooth tube the Q
  of a resonance then grows as √f. The measured alto's Q values are
  instead nearly flat across its first four modes, which points to
  further losses that grow with frequency: the closed holes' cavities,
  the pads, the joints, and the lattice radiating.

## 3. Tone holes and register keys

- **The open-hole lattice.** Below the first open tone hole, the open
  holes form a lattice. Under its cutoff frequency f_c, waves reflect from
  the first open hole, so the bore's resonances are those of the shortened
  tube. Above f_c they travel down the lattice and radiate, so the
  resonances fade. Benade's estimate is f_c ≈ 0.11·c·(b/a)/√(s·t_e), with
  b the hole's radius, a the bore's, 2s the spacing and t_e the hole's
  effective height [B76 sec., CSW09].
  - Tenor: 760 ± 250 Hz.
  - Soprano: 1340 ± 240 Hz [CSW09].
  - Alto: from cell to cell, 500 Hz (holes near the bell) to 1500 Hz (top
    holes). Two first-register fingerings give 930 to 1140 Hz [PCKG20].
  - The tenor's b/a is 0.54 ± 0.08 [JG21].
- **The holes.** The model needs each open hole as a shunt impedance:
  the mass of the air in its chimney, with end corrections inside and
  out, its radiation, and the small negative series length it adds to
  the bore [Keefe 1990, Dalmont et al. 2002, both sec.].
- **Register vents.** A saxophone has two octave vents, about 2 mm across
  and 6 mm long on a soprano [CSW09]. The body vent serves written
  D5–G♯5 and the neck vent A5 upward. A vent opens near the second mode's
  pressure node. It hardly touches that mode but spoils the first, raising
  it and damping it, so the reed plays the second. The upper register is
  therefore the lower register's tubes overblown. The palm keys open
  holes high on the body for D6 to F6.
- **Altissimo.** Above F♯6 the notes are high resonances (the third,
  fourth and fifth) of cross-fingered tubes. These resonances are weak,
  5 to 31 MPa·s/m³ [CSW11], and the player's vocal tract holds the note
  there (section 4).

## 4. The vocal tract

- **In series.** The vocal tract lies upstream of the reed, in series with
  the bore: the reed sees the sum of the two impedances [SLdS08 sec.,
  CSW11].
- **Ordinary playing.** Tenor players' tracts have peaks near 150–250 Hz
  (≤ 3 MPa·s/m³) and 500–1100 Hz (0.4–10 MPa·s/m³), far under the bore's.
  They are not tuned to the note [CSW11].
- **Altissimo.** Experienced players raise the tract's peak to
  10–40 MPa·s/m³ and tune it at or a little above the note. One example
  is 33 MPa·s/m³ at 980 Hz against a bore peak of 9.8 [CSW08, CSW11].
  Inexperienced players cannot do this, and cannot play altissimo.
- **Bends.** The tract's peak grows about sixfold. The deepest bend
  measured was 300 cents down, 91 Hz on written D♯6 [CSW11]; Scavone et
  al. report 330 cents on average on alto [SLdS08 sec.]. Players can bend
  only a little upward. In the low register a bend is the lip and jaw
  more than the tract: some tens of cents.
- **Timbre.** Changing the tract shifts the radiated harmonics by 5–10 dB
  between 0.7 and 3 kHz [LCSW15].
- **Growl.** A player growls by humming or singing while playing. The
  vocal folds modulate the flow through the reed, which puts sum and
  difference tones of the voice's pitch and the note's around every
  harmonic. No peer-reviewed measurement was found; the descriptions are
  pedagogical.

## 5. Radiation

- **Sources.** The horn radiates from its bell and from its open holes.
  Below the cutoff the first open holes and the bell radiate most of it.
- **Inside against outside.** The spectrum outside is the inside's times
  a transfer that rises with frequency, steeply up to the cutoff and less
  above it. Benade's "spectrum transformation" is about 6 dB per octave
  [B76 sec.].
- **Measured spectra.**
  - The radiated spectrum shows a break near 618 Hz on the tenor and
    837 Hz on the alto [BL88 sec., via KGSVDGK19].
  - A tenor's B♭3 at 107 dB (one bell radius off) has its spectral
    centroid near 1.18 kHz [LCSW15].
  - The internal spectrum has anti-formants near m·c/2x1, where the
    missing tip would resonate [KGKDG13].

## 6. Breath noise, subtone, vibrato

- **Breath noise.** The jet through the slit is turbulent at the Reynolds
  numbers of playing. Its noise is carried by the flow and shaped by the
  bore. No measurement of its level against the tone was found.
- **Subtone.** A soft, dark, airy tone: much of the lower lip on the reed,
  the jaw dropped, blown just over the threshold. The reed barely beats
  and the spectrum is nearly a sine plus air. No tract tuning was observed
  in it [CSW11]; the mechanism is from players' descriptions.
- **Vibrato.** Saxophonists make vibrato with the jaw. Jazz players
  modulate the pitch widely: Coleman Hawkins' median depth is 40.8 cents,
  more than the trumpeters', and slower on long notes [APFZ14]. The
  general vibrato literature gives 5.5 to 8 Hz [TD00]. In pedagogy it is
  about 4.8 Hz classically and 4 to 6 Hz in jazz, ±15–40 cents.

## 7. The tenor against the alto

A tenor is not an alto scaled by 2^(5/12) in every length. (Tags: sec. is
secondary, a forum, retailer or museum record, or an abstract; dig. is
read off a published figure, to ±1–2 mm.)

- **The bore.** The tenor's neck takes the mouthpiece at about 6.7 mm in
  radius [JG21 Fig. 3, dig.], the alto's at 6.0–6.25 mm [CVGD21, PCKG20].
  The tenor's cone runs at 1.52° [CSW09] (1.55° in the CT [JG21, dig.])
  against the alto's 1.60°. So the tenor is longer mostly because its cone
  runs further towards its tip: about 0.245 m missing (11.5 ml) against
  the alto's ~0.2 m.
- **The mouthpiece.** Its shank is 16.98 mm across on average over 41
  tenor mouthpieces (16.5–17.4) [MPW, sec.]; one alto's is 16 mm. Filled
  with water, a tenor mouthpiece holds 16.7 ml and an alto's 13.4 ml with
  the shank [MPW, sec.]: about 11–12 and 8–9 ml past the neck, the
  missing cone's volume on each. Tip openings are 1.2–1.4 times the
  alto's (Selmer S80 C*: 1.90 mm against 1.55) [Syos, sec.]. No measured
  baffle heights were found.
- **The reed.** A tenor reed is about 80 mm long and 17.3 mm wide at the
  tip, an alto's 70 mm and 16 mm [Thomann, sec.]. Its resonance with the
  lip, 672 Hz and Q 1.13, was measured on a tenor [MGDC16]. None was
  found for the alto.
- **The tone holes.** On a tenor, measured by CT, the holes run from 11 mm
  across (C♯5) to 42 mm (C4, B3), b/a = 0.54 ± 0.08 [JG21]. No alto table
  was found.
- **The bell.** About 150 mm across on a tenor [JG21, dig.] (museum tenors
  140–160 mm [NMM, sec.]); an alto's 112–118 mm in museum records, about
  115–125 on a modern one.
- **The spectrum.** Benade and Lutgen fit one envelope to each horn's low-
  and second-register notes, rising to a break and falling steeply above
  it: 618 Hz on a tenor, 837 Hz on an alto, a ratio of 0.74, about the
  fourth between them [BL88, sec.]. On a tenor's low notes the strongest
  harmonics lie at 415–730 Hz (UNSW recordings, analysed here), the honk.
  Li et al. measured a forte tenor concert B♭3's centroid at about
  1.18 kHz [LCSW15]. No published paper sets the two horns' centroids side
  by side. Recordings from different collections (UNSW's tenor, Iowa's
  alto) differ in player, mouthpiece and microphone by more than the
  horns do, so they were not used to tune.
- **Where they play.** A tenor sounds A♭2 to E♭5 (E5 with the high F♯), an
  alto D♭3 to A♭5. In 157 tenor and 80 alto solos of the Weimar Jazz
  Database, the middle half of the notes lie from B3 to A♭4 (tenor) and
  D4 to B♭4 (alto), the medians E4 and F♯4 [WJD]: tenor players solo
  high in their horn, so the two are told apart by their sound more than
  their register.

## Sources

- **[APFZ14]** J. Abeßer, M. Pfleiderer, K. Frieler, W.-G. Zaddach, "Score-informed tracking and contextual analysis of fundamental frequency contours in trumpet and saxophone jazz solos", DAFx-14 (2014).
- **[AvW04]** F. Avanzini, M. van Walstijn, "Modelling the mechanical response of the reed–mouthpiece–lip system of a clarinet. Part I", Acta Acustica u. Acustica 90 (2004). sec.
- **[B76]** A. H. Benade, *Fundamentals of Musical Acoustics* (1976). sec.
- **[BL88]** A. H. Benade, S. J. Lutgen, "The saxophone spectrum", JASA 83 (1988) 1900–1907. sec.
- **[CSW08]** J.-M. Chen, J. Smith, J. Wolfe, "Experienced saxophonists learn to tune their vocal tracts", Science 319 (2008) 726.
- **[CSW09]** J.-M. Chen, J. Smith, J. Wolfe, "Saxophone acoustics: introducing a compendium of impedance and sound spectra", Acoustics Australia 37 (2009) 18–23.
- **[CSW11]** J.-M. Chen, J. Smith, J. Wolfe, "Saxophonists tune vocal tract resonances in advanced performance techniques", JASA 129 (2011) 415–426.
- **[CVGD21]** T. Colinot, C. Vergez, P. Guillemain, J.-B. Doc, "Multistability of saxophone oscillation regimes and its influence on sound production", Acta Acustica 5 (2021) 33.
- **[DGO03]** J.-P. Dalmont, J. Gilbert, S. Ollivier, "Nonlinear characteristics of single-reed instruments: quasistatic volume flow and reed opening measurements", JASA 114 (2003). sec.
- **[FR98]** N. H. Fletcher, T. D. Rossing, *The Physics of Musical Instruments*, 2nd ed. (1998). sec.
- **[FS96]** L. Fuks, J. Sundberg, "Blowing pressures in reed woodwind instruments", KTH TMH-QPSR 3/1996.
- **[GKG24]** A. Gaillard, V. Koehl, B. Gazengel, "Theoretical and experimental studies about single cane reeds: a review", Acta Acustica 8 (2024) 63.
- **[GKV05]** P. Guillemain, J. Kergomard, T. Voinier, "Real-time synthesis of clarinet-like instruments using digital impedance models", JASA 118 (2005) 483–494. sec.
- **[JG21]** T. Jensch, T. Grothe, "Tone hole adjustment for a wooden saxophone", DAGA 2021.
- **[KGKDG13]** J. Kergomard, P. Guillemain, S. Karkar, J.-P. Dalmont, B. Gazengel, "What we understand today on formants in saxophone sounds?", Tecniacustica 2013.
- **[KGS16]** J. Kergomard, P. Guillemain, F. Silva, S. Karkar, "Idealized digital models for conical reed instruments, with focus on the internal pressure waveform", JASA 139 (2016) 927–937. sec.
- **[KGSVDGK19]** J. Kergomard et al., "Role of the resonator geometry on the pressure spectrum of reed conical instruments", HAL hal-01865994 (2019).
- **[LCSW15]** W. Li, J.-M. Chen, J. Smith, J. Wolfe, "Effect of vocal tract resonances on the sound spectrum of the saxophone", Acta Acustica u. Acustica 101 (2015) 270–278.
- **[MGDC16]** A. Muñoz Arancón, B. Gazengel, J.-P. Dalmont, E. Conan, "Estimation of saxophone reed parameters during playing", JASA 139 (2016) 2754–2765. sec.
- **[OCVCP21]** M. Ozdemir et al., "Towards 3D printed saxophone mouthpiece personalization", Acta Acustica 5 (2021) 46.
- **[ODK04]** S. Ollivier, J.-P. Dalmont, J. Kergomard, "Idealized models of reed woodwinds. Part I: analogy with the bowed string", Acta Acustica u. Acustica 90 (2004). sec.
- **[PCKG20]** E. Petersen, T. Colinot, J. Kergomard, P. Guillemain, "On the tonehole lattice cutoff frequency of conical resonators: applications to the saxophone", Acta Acustica 4 (2020) 13.
- **[SLdS08]** G. P. Scavone, A. Lefebvre, A. R. da Silva, "Measurement of vocal-tract influence during saxophone performance", JASA 123 (2008) 2391–2400. sec.
- **[TD00]** R. Timmers, P. Desain, "Vibrato: questions and answers from musicians and science", ICMPC 2000.
- **[WB74]** T. A. Wilson, G. S. Beavers, "Operating modes of the clarinet", JASA 56 (1974) 653–658. sec.
- **[MPW]** Mouthpiece Work forum (archived at jackdesert.github.io/mouthpiece-work): K. Bradbury, "Tenor Shank Bore Stats"; M. Fleming, "Measuring volume in a mouthpiece". sec.
- **[NMM]** National Music Museum (Vermillion) catalogue records of saxophones. sec.
- **[Syos]** Syos tip-opening charts for alto and tenor mouthpieces. sec.
- **[Thomann]** Thomann reed listings (Pilgerstorfer alto and tenor). sec.
- **[UNSW]** UNSW Music Acoustics, tenor saxophone recordings (newt.phys.unsw.edu.au/music/saxophone).
- **[WJD]** Weimar Jazz Database (Jazzomat Research Project), 2017.
