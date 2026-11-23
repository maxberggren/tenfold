# Gongs, opera gongs and tam-tams: the physics, in numbers for a model

Compiled 2026-11-23 from the published literature. Bracketed tags such as
[RF83] point to the source list at the bottom.

Each value is marked by where it comes from:
- **pub**: stated in the source.
- **fig**: read off a published figure, roughly ±10 %.
- **calc**: my arithmetic on published numbers.
- **inf**: my inference or estimate, with nothing published on it.

**Caveat.** Gongs are much less measured than bells or steelpans. The
nonlinear behaviour is well studied but on few instruments:
- six Chinese opera gongs and one gold pan [RF83];
- a Turkish gong, a tam-tam and a cymbal [LF89];
- spherical caps and gongs under sinusoidal forcing [CTT05];
- one large Balinese gong ageng [KGG10].

No published measurement gives the decay time of a kempul or bonang. None
gives the contact time of a gong beater, the spectral density of a
tam-tam's late shimmer in modes per hertz, or the thickness profile of a
tam-tam's hammered bumps. Those values below are labelled **inf**.

---

## 0. Numbers for the model (summary table)

| Quantity | Value | Range / notes | Tag | Source |
|---|---|---|---|---|
| Material | Bronze (B20 bell bronze, ~78 Cu / 22 Sn); E ≈ 100 GPa, ρ ≈ 8700 kg/m³, ν ≈ 0.34 | Wikipedia's quoted composition doesn't add up, so treat it loosely | pub (alloy) / inf (moduli) | [WIKI] |
| Opera gong, large (daluo) | 300–332 mm overall; centre essentially flat, about 0.8 mm thick; conical shoulders about twice that | | pub | [RF83] |
| Opera gong, small (xiaoluo) | 213–225 mm overall; centre a shallow spherical shell about 0.7 mm thick; striking area about 10 cm across, domed by about 1 mm | | pub | [RF83], [F85] |
| Opera gong pitches (soft blow) | Large 176–180 Hz; small 376–700 Hz | Table I | pub | [RF83] |
| Opera gong glide | Large (flat): hard blow **+18 % / +20 %** (176→212, 180→212 Hz), gliding down. Small (domed): **−7 … −17 %** (472→390, 472→430, 700→640 Hz), gliding up. "As much as three semitones" down, "as much as two semitones" up | Glide lasts "a second or so" as the sound decays | pub | [RF83], [F85], [F99] |
| Amplitude reached | 0.6–0.9 mm, about **0.8–1.0 × the thickness** | Measured with a pin touching at the greatest deflection | pub | [RF83] |
| Flat clamped plate hardening | +16 % at amplitude = h, +35 % at 2h (Wah); simply supported +30–37 % | | pub | [RF83] |
| Domed cap softening | H = 2h, A = h: −10 %. H = h, A = h: −5 %. Thin cap: up to −50 % near A ≈ x0, recovering above about 2x0 | | pub | [RF83], [F85] |
| Fletcher's cap law | ẍ + ω0²x = −[(ω0² − c/m)/2x0²](x³ + 3x0x²), with c/m = αω0² and α = [1 + 0.85 x0²/h²]⁻¹ | One axisymmetric mode, clamped rim | pub | [F85] |
| Bossed gong (gong ageng wadon) | Face about 3 mm thick, boss 7 mm at its centre; modes I–IX at 61.75, 92.00, 101.75, 121.25, 150.25, 153.25, 160.00, 162.50, 236.00 Hz | I and IV are axisymmetric, nearly 2:1, both with an antinode on the boss | pub | [KGG10] |
| Bossed gong ratios | **1, 1.490, 1.648, 1.964, 2.433, 2.482, 2.591, 2.632, 3.822** | | calc | [KGG10] |
| Ombak | **≈ 2.5 Hz**. 2f1 (123.5) beats against f2 (121.25), and 3f1 (185.25) against f1+f2 (183.0), in phase | Nonlinear; the boss swings ±0.44 mm at a performance blow. No measurable pitch change with amplitude; chaotic only when struck several mm | pub | [KGG10] |
| Other beats on bossed gongs | Hammered pairs at 150/153 and 160/162.5 Hz give 3–13 Hz beats. Some bossed gongs are "made to generate an additional beat note … 1 to 5 Hz" | | pub | [KGG10], [WIKI] |
| Gamelan gongs' nonlinearity | "Do not exhibit any marked nonlinearity": thick, highly curved walls, played moderately | | pub | [F99] |
| Tam-tam | About 1 m across, **1–2 mm** bronze. A central striking dome, a rolled-down rim, and 3–4 rings of 50–100 hammered bumps (about 15 mm across, 8 mm high) | | pub | [LF89], [F99] |
| Tam-tam fundamental | Axisymmetric, **≈ 80 Hz**: "a massive deeply pitched thud" | | pub | [F99] |
| Tam-tam cascade | "Over a time of about **1 s**, blossoms into a bright shimmering high-frequency wideband blaze of sound, dying away over as long as **10 s**". Fletcher 1999 gives "**2 to 3 s**" for the change to shimmer. Late on, the centre is "essentially motionless", with the vibration in the outer third | Spectra immediately after the strike and about 3 s later reach to 10 kHz | pub | [LF89], [F99] |
| Frequency multiplication step | A 2f vibration builds up in **≈ 0.1 s**. Its amplitude grows as the square (2f) or cube (3f) of the driving mode's | Kinked bars and strings | pub | [LF89], [F99] |
| Turkish gong axisymmetric modes | 96, 200, 416, 664, 992 Hz (1 : 2.08 : 4.33 : 6.92 : 10.3). Subharmonics of orders 2, 3 and 4 appear at amplitudes < 2 mm (a quarter of the shell height) | 60 cm, 2 mm bronze, dish 5 mm deep | pub | [LF89] |
| Selection rules | Quadratic nonlinearity: combinations p·Ω = ±ωi ± ωj. The 1:1:2 resonance is axisymmetric (0,1) at 224 Hz pumping the (6,0) twins at 111 Hz. Five to ten active modes | | pub | [CTT05] |
| Twin modes | Degenerate in a perfect disc. A tam-tam shows "significant differences between the frequencies of the twin modes". An opera gong's two single-diameter modes are far apart (azimuthal thickness variation) | | pub | [CTT05], [RF83] |
| Plate damping law | ξ = 8·10⁻³ ω^(−0.4) (Humbert, 0.5 mm plate); Ducceschi & Touzé use it for gong and cymbal synthesis | | pub | [DT15] |
| Strike for synthesis | Raised cosine, half-width 0.8–1 ms, 100–300 N on a 21×18 cm, 1 mm plate | | pub | [DT15] |
| Clamped circular plate | λ(m,n): (0,1) 3.1962, (1,1) 4.6109, (2,1) 5.9057, (0,2) 6.3064, (3,1) 7.1435, (1,2) 7.7993, (0,3) 9.4395; ω = λ²√(D/ρh)/a² | | pub | [LEI] |

---

## 1. What a gong is

A gong is a bronze plate that vibrates mostly in one region. That region
is held at its edge by something stiffer [F85][F99]:

- **The Chinese opera gong.** A conical flange, about twice the centre's
  thickness, clamps the centre and baffles it. Holograms show the motion
  confined to the centre [RF83].
- **The bossed gamelan gong** (gong ageng, kempul, bonang, kulintang). The
  face is thick and curved and stiffened by a deep rim, the bau. A raised,
  thickened boss in the middle is where it is struck. The boss's mass and
  size pull the two principal axisymmetric modes to nearly 2:1, and that
  gives the gong its tonality (Rossing & Shepherd, in [KGG10]).
- **The tam-tam.** A nearly flat thin sheet stiffened by a rolled-down rim,
  rings of hammered bumps and a central dome [LF89].

Fletcher also distinguishes gongs from bells by intent: a bell's modes are
tuned to a series, a gong's spectrum is left to its shape [F99]. The
exception is the bossed gong, whose smith tunes the boss for the 2:1 pair
and hammers twin pairs for beats [KGG10].

## 2. The plate's flexural modes

For small amplitudes the vibrating region is Kirchhoff's plate. Clamped at
its rim, its modes are R(r) = J_m(λr/a) − c·I_m(λr/a) times cos or sin of
mθ. The λ are the roots of J_m(λ)I_m′(λ) − J_m′(λ)I_m(λ) = 0 [LEI], and
ω = λ²√(D/ρh)/a² with D = Eh³/12(1−ν²) [RF83].

- **The clamped edge is a fair model** for all three gong kinds. On an
  opera gong "the rim has negligible motion in the principal modes"
  [RF83]. Fletcher treats the central section as clamped, the flange
  having 5–10 times its mass [F85].
- **Ratios, clamped:** 1, 2.08, 3.41, 3.89, 4.99, 5.95, 6.82, 8.26, 8.72 …
  A flat gong's spectrum is this one.
- **A domed centre stiffens the axisymmetric modes** through its
  membrane. Reissner's clamped-cap result, as Fletcher gives it, is
  ω0² ∝ 1 + (1+ν)(0.90 − 0.20(1+ν))x0²/h². At x0 = 1.4h the fundamental
  rises about 1.6× above the flat plate's, closer to the (1,1) pair [F85].
- **Every mode with nodal diameters is a twin pair.** A real gong's uneven
  hammering splits the pairs. On the opera gong the two single-diameter
  modes are far apart [RF83]; on a tam-tam the splits are "significant"
  [CTT05]. Split pairs beat.
- **Measured bossed gongs don't follow the plate.** The boss and rim
  compress the spectrum. Krueger's Balinese gong has nine modes between 1×
  and 3.8× (§0), against the plate's four in that range. A 56 cm brass
  kempul has "at least 9 partials" between f1 and 3f1, nearly harmonic.

## 3. Geometric nonlinearity: von Kármán

At amplitudes of order the thickness, a plate must stretch as it bends.
The von Kármán equations add an in-plane stress (Airy's function F) driven
by the deflection's Gaussian curvature. A shell with rise w0 also gets a
term linear in the deflection [CTT05][DT15]:

- ρh ẅ + DΔ²w = L(w + w0, F) + p
- Δ²F = −(Eh/2)[L(w,w) + 2L(w0,w)]
- L(f,g) = f_xx g_yy + f_yy g_xx − 2 f_xy g_xy

Projected onto the linear modes, this gives coupled oscillators with
quadratic terms (from the curvature) and cubic terms (from the
stretching). The cubic coefficients factor through the in-plane modes [DT15]:

  Γ^s_jkl = Σ_r H^r_kl E^s_jr / ζ_r⁴

Consequences:

- **Energy.** Eliminating the in-plane field leaves an energy that is a sum
  of squares of in-plane strains: ½Σκ_r ε_r² with ε_r = ½xᵀH^r x + G^r·x.
  It is non-negative, so the stretching only moves energy between modes.
- **Selection rules.** An axisymmetric in-plane mode couples only
  transverse modes with the same number of nodal diameters. In-plane modes
  with 2k_c diameters do the rest [DT15]. Quadratic combinations obey
  pΩ = ±ωi ± ωj [CTT05].
- **Hardening against softening.** The cubic terms harden. The quadratic
  ones (curvature) soften, by about 5a2²/(12ω0³) against 3a3/(8ω0) in the
  frequency shift. Which wins depends on the rise over the thickness
  [CTT05][F85].
- **Thin structures cross over early.** They turn quasi-periodic and then
  chaotic at amplitudes well under the shell height. The tam-tam goes
  there; thick gongs stay weakly nonlinear and glide instead [CTT05][F99].

Fletcher's one-mode cap law (§0) is exactly this sum of squares for one
mode and one strain. Its restoring force bx(x+x0)(x+2x0) is the derivative
of (b/4)((x+x0)² − x0²)²: the square of the stretch of a cap whose height
has gone from x0 to x0 + x [F85].

## 4. Pitch glides of opera gongs

Rossing & Fletcher measured six opera gongs [RF83]:

- **The three large ones** (flat centre) glide **down** "as much as three
  semitones". A hard blow sounds 18–20 % above a soft one: hardening.
- **The three small ones** (domed centre) glide **up** "as much as two
  semitones". Table I gives −7, −9 and −17 %: softening.
- **Amplitude:** 0.8–1.0 h for both kinds.
- **Comparison with theory:**
  - A clamped flat plate gives +12–17 % at that amplitude, simply
    supported +30–37 % (Wah), so the large gongs agree.
  - For the small gongs the H = h cap gives about −5 % at A = h, against
    the measured −7…−9 %.

Fletcher's 1985 analysis makes it quantitative [F85]:

- **Thin domed shell:** the frequency falls with amplitude to a minimum
  near A ≈ x0 (as much as 50 % for a very thin shell). It rises again
  past ~2x0.
- **Thicker shells** (h/x0 ≥ 2) simply harden.
- **The small gong** (10 cm striking area, h ≈ 0.7 mm, x0 ≈ 1 mm,
  h/x0 ≈ 0.7) is predicted to drop up to ~20 %, about 3 semitones, "rather
  more than the aurally judged glide". Its glide should be "fairly stable"
  for 0.5–1.5 x0. With the light soft stick the gongs come with, the
  reversing glide isn't reached.
- **The large gong** (15 cm centre, h ≈ 1 mm) gives "several semitones"
  down.

Fletcher 1999 [F99] adds:

- Struck softly, both gongs give "a simple and nearly sinusoidal tone".
- The initial strike reaches more than 1 mm, a pitch change "of as much as
  20 per cent, or a minor third".
- The glide happens "as the vibration decays over a time of order one
  second".

## 5. Bossed gongs

- **Tuning.** The boss is what makes a gamelan gong tonal. The two
  principal axisymmetric modes stand nearly 2:1, and both have an antinode
  on the boss, so a strike there excites them and they radiate well
  [KGG10].
- **The ombak, Balinese gong ageng wadon** [KGG10]:
  - It beats at about 2.5 Hz.
  - Four closely spaced asymmetric modes, perhaps hammered in on purpose,
    beat at 3–13 Hz.
  - The dominant beat is nonlinear. The struck gong radiates 123.5 Hz
    (2f1) and 183.0 / 185.25 Hz (f1+f2 / 3f1), which white-noise
    excitation does not show. These beat against f2 = 121.25 Hz and each
    other, in phase.
  - Driving the boss with f1 and f2 at ±0.38 mm reproduces the ombak.
  - The pressure and acceleration harmonics grow together, so the
    nonlinearity is structural, not acoustic.
  - The second harmonic and the second mode must be of about equal
    strength.
  - The pitch does not change with amplitude, and the response is not
    chaotic at normal strikes.
- **The smith's tuning.** Gong smiths shape the boss for tonality and
  appear to place twin pairs for beats [KGG10]. Some bossed gongs are made
  to beat at 1–5 Hz [WIKI].
- **Strike place.** Struck on the boss or beside it, a nipple gong gives
  "two distinct sounds" [WIKI].
- **Mallet.** The kempul is played with the soft, heavily padded end of a
  mallet; the bonang with padded sticks [WIKI].
- **Pitch range.** A gong ageng is about 44–62 Hz [WIKI][KGG10]. A kempul
  is 19–25 cm. A bonang covers about two octaves in its higher register,
  its lower kettles flat-headed and its higher ones arched [WIKI].

## 6. The tam-tam's cascade

Legge & Fletcher [LF89] describe the sound:

- "a dull, low-frequency thump which then, over a time of about 1 s,
  blossoms into a bright shimmering high-frequency wideband blaze of
  sound, dying away over as long as 10 s";
- a slow build-up and decay of energy in the higher partials, while the
  low modes simply decay. The sound is not wideband from the start with
  its highs masked.

Two mechanisms, each with considerable hysteresis:

1. **An upward cascade.** Coupling between tension and shear stresses at
   sharp changes of shape (the bump rings, the rim) passes energy upward.
   On kinked bars a 2f mode builds in about 0.1 s, its amplitude growing
   as the square of the driving mode's, and 3f as the cube.
2. **Subharmonics and chaos.** A transition to multiple fractional
   subharmonics or chaos fills the spectrum between the linear modes.

Fletcher 1999 [F99] adds:

- The fundamental is about 80 Hz.
- The change to "high-frequency non-tonal shimmer" takes 2–3 s.
- The cascade involves several steps and takes about 1 s.
- The low fundamental is heavily damped by radiation and dies out rather
  quickly. The higher modes, which radiate less, hold the energy.
- Late on, the centre is almost motionless and the vibration sits in the
  outer third.

Chaigne et al. [CTT05]:

- A sinusoidally forced gong goes periodic, then quasi-periodic, then
  chaotic, the last sounding "similar to real instruments normally
  excited by the strong impact of a mallet".
- Five to ten active degrees of freedom suffice for the bifurcations.

Ducceschi & Touzé [DT15]:

- A modal von Kármán plate with 100 transverse and 50 in-plane modes, up
  to 9 kHz.
- At w ≈ h it gives pitch bends; at w ≈ 2h a turbulent crash or shimmer
  just after the strike.

## 7. The mallet

Gongs are struck with padded beaters:

- **Opera gongs:** "a padded stick", "rather light", but hit very
  vigorously [F99][F85].
- **Tam-tam:** "a heavy and softly padded striker" [F99].
- **Gamelan gongs:** padded mallets [WIKI].

Ducceschi & Touzé model a strike as a raised cosine of 0.8–1 ms half-width
[DT15]: shorter and harder is louder and brighter.

**Inferred:** a padded beater's felt stiffens as it is squeezed. The
Hertzian F = Kδ^p with p ≈ 2–3.5 is the piano-hammer law. Contact time is a
large fraction of a big gong's fundamental period, which is why a big gong
starts dull. No published contact time exists for gong beaters.

## 8. Damping and radiation

- **Plates.** ξ(ω) = 8·10⁻³ ω^(−0.4) for a thin plate (Humbert, in [DT15]).
  Higher modes decay faster in time but slower per cycle.
- **The tam-tam's thud** is damped by radiation [F99]. Its asymmetric
  modes, below the coincidence frequency (about 13 kHz for 1.5 mm bronze,
  calc), radiate less and ring longer, and the sound dies "over as long as
  10 s" [LF89].
- **Bossed gongs** have "very high quality factors". Their natural
  frequencies are stable over a 10 s analysis [KGG10].
- **Opera gongs** ring for seconds. The glide spans about a second of the
  decay [F99].
- **Radiation.** An axisymmetric mode sweeps a net volume and radiates
  like a piston. A mode with nodal diameters radiates as a multipole,
  weakly until the gong is large against the wavelength (calc). Opera
  gongs' conical flanges act as a baffle [F99].

---

## Sources

- **[RF83]** T. D. Rossing and N. H. Fletcher, "Nonlinear vibrations in plates and gongs," J. Acoust. Soc. Am. 73(1), 345–351 (1983).
- **[F85]** N. H. Fletcher, "Nonlinear frequency shifts in quasispherical-cap shells: Pitch glide in Chinese gongs," J. Acoust. Soc. Am. 78(6), 2069–2073 (1985).
- **[LF89]** K. A. Legge and N. H. Fletcher, "Nonlinearity, chaos, and the sound of shallow gongs," J. Acoust. Soc. Am. 86(6), 2439–2443 (1989).
- **[F99]** N. H. Fletcher, "The nonlinear physics of musical instruments," Rep. Prog. Phys. 62, 723–764 (1999), §13.
- **[CTT05]** A. Chaigne, C. Touzé and O. Thomas, "Nonlinear vibrations and chaos in gongs and cymbals," Acoust. Sci. & Tech. 26(5), 403–409 (2005).
- **[DT15]** M. Ducceschi and C. Touzé, "Simulations of nonlinear plate dynamics: an accurate and efficient modal algorithm," Proc. DAFx-15 (2015); and "Modal approach for nonlinear vibrations of damped impacted plates," J. Sound Vib. 344, 313–331 (2015).
- **[KGG10]** D. W. Krueger, K. L. Gee and J. Grimshaw, "Acoustical and vibrometry analysis of a large Balinese gamelan gong," J. Acoust. Soc. Am. 128(1), EL8–EL13 (2010).
- **[LEI]** A. W. Leissa, Vibration of Plates, NASA SP-160 (1969): clamped circular plate roots.
- **[WIKI]** Wikipedia: Gong, Gong ageng (citing van Oldenborgh 2002, Ayers & Homer 1999), Kempul, Bonang.
