# The gongs

The Lead page has three gongs, each simulated as it is built (`src/gong.h`)
and played as a melody instrument (`src/lead.cpp`, `gongStrike`):

- **Gong** (gamelan): a bossed gong, the kempul, bonang or kulintang kind.
- **Opera gong** (Chinese opera): a luo, whose pitch glides as it rings.
- **Tam-tam** (film score): the orchestra's great flat gong, whose thud
  blossoms into a shimmer.

Every voice of the line, melody and harmony, is a gong of its own, tuned
to its note: a gong chime.

## Playing them

- **Where you play:** the neck picks the note, as with every lead. The
  tam-tam's thud sounds an octave under it. The opera gongs are the
  family's real sizes:
  - low on the neck, the big flat gongs (daluo), which glide down;
  - from about E4 up, the small domed ones (xiaoluo), which glide up;
  - between about E4 and B♭4, the maker's choice, crossing over.
- **How hard:** pressure sets how fast the mallet lands. Harder is louder
  and brighter. On the opera gong it also glides further, on the tam-tam
  it blossoms further, and on the bossed gong its octave beats harder.
  A light touch on an opera gong is nearly a pure tone.
- **How high:** the lower half of the pad strikes the middle (the boss,
  the dome). Higher strikes further out toward the rim:
  - on the bossed gong, a clang with less of the note;
  - on the tam-tam, less thud and more shimmer.
- **A flatter finger:** a fatter, softer beater, duller.
- **Holding:** a gong rings on its own while held. A tam-tam held past a
  beat is rolled with soft beaters on the studio's grid (about nine strokes
  a second), swelling over a second or two, harder the harder the press.
- **Lifting:** a hand lies on the gong and stills it, a bonang player's
  damping. It takes about 1.2 s on the bossed gong and 0.9 s on the opera
  gong (the time for "everything dies within", `Gong::mute`). On the
  tam-tam the hand only rests: it rings on for more than ten seconds.
- **Bends:** the bend wheel, a twist and legato glides move the whole
  gong in time. Its stretching stiffens with it, so a bent gong is the same
  gong, retuned. Legato notes strike again.
- **Polyphony:** four tam-tams ring at most. A fifth note takes the
  quietest one nobody holds, as a percussionist has a few, not eight.

## The model

Everything is in SI units, integrated sample by sample with each mode's
poles placed exactly (as in the hang). Sources are in
`docs/gong-research.md`; **inf** marks a value I estimated where nothing is
published.

| Part | What it is | Set from |
|---|---|---|
| Sheet | Bronze: E = 100 GPa, ρ = 8700 kg/m³, ν = 0.34. Thickness: opera gong 0.8 mm, bossed gong 3 mm (its face; the boss's thickening is not modelled), tam-tam 1.5 mm. The radius follows from the pitch, ω₁ = λ₁²√(D/ρh)/a² | [RF83] 0.7–0.8 mm centres; [KGG10] 3 mm face; [LF89]/[F99] 1–2 mm tam-tams |
| Modes | The clamped circular plate's, computed from Bessel functions when the instrument is built: R = J_m(λr) − c·I_m(λr), times cos or sin of mθ. The roots are found by bisection and tested against Leissa (λ₀₁ = 3.1962, λ₁₁ = 4.6109 …). Opera gong: 23 modes up to 13 × f1. Bossed gong: 17 up to about 4.4 × f1. Tam-tam: 116, which with the thud at 110 Hz reach 8.6 kHz. Modes above 0.18 × the sample rate sit out | [LEI]; the rim has negligible motion [RF83]; clamped centre [F85] |
| Twins | Every mode with diameters is a cos/sin pair, pulled apart by uneven hammering. Opera gong: the (1,1) pair 8 % apart, others up to 1.5 %. Bossed gong: as Krueger measured. Tam-tam: up to 1.2 % | [RF83] (the two single-diameter modes far apart), [CTT05] (tam-tam twins), [KGG10] |
| Bossed tuning | The smith's spectrum, not the plate's: 1, 1.490 / 1.648, 1.964, 2.433 / 2.482, 2.591 / 2.632, 3.822 / 3.86. The rest are the plate's ratios compressed as the measured ones are (λ² to the 0.68, **inf**). Each mode's own stiffness is solved, with the shell's membrane and the coupling it makes, so its eigenfrequencies land on these | [KGG10] |
| Ombak | The second axisymmetric mode sits under 2f1 by 2.5·√(f1/62 Hz) Hz, held between 2 and 6 Hz: 2.5 Hz on a 62 Hz gong ageng, 4.5 Hz at 200 Hz. The rate law is **inf**; [WIKI] says beats of 1–5 Hz. The boss's own stretching sounds 2f1 and 3f1, which beat against f2 and f1 + f2. f2 − f1 sounds too, just under f1 (Krueger's 59.5 Hz) | [KGG10] Table 1: 2f1 − f2 = 2.25 Hz, beats ≈ 2.5 Hz, in phase at 120 and 180 Hz |
| Rise | The shell's static shape w0. Bossed gong: a spherical cap 3 thicknesses deep (the quadratic coupling it gives peaks near a rise of about a thickness, by Fletcher's law; deeper changes little). Opera gong: flat up to 330 Hz, rising (smoothstep in log frequency) to 1.43 thicknesses at 470 Hz and above. Tam-tam: a central dome 0.6 thicknesses over 35 % of the radius | [F85]: small gong x0 ≈ 1 mm on h ≈ 0.7 mm; [RF83] Table I: 376 Hz glides down, 472 Hz up |
| Stretching (opera, bossed) | Von Kármán, projected. The rim's uniform tension (Berger's mean; the opera flange and the bossed gong's deep rim hold all of an immovable rim's) and the first four (opera) or three (bossed) axisymmetric Airy modes. Each is a strain ε = ½xᵀHx + G·x, with H from L(φk, φl) (modes of the same symmetry) and G from L(w0, φ), and an energy ½κε² | [DT15] factorisation, [CTT05] selection rules, [F85] (which it is exactly, for one mode). A truncation: the in-plane modes with 2m diameters, which an off-centre strike's asymmetric modes would also feel, are left out |
| Stretching (tam-tam) | Eight patches of the sheet in four rings, each with the uniform tension of its own mean stretch: Marguerre's ½\|∇w\|² + ∇w0·∇w where the patch is, with the texture's slope. A local, Berger-like approximation of a stress that is really nonlocal; it is chosen because it couples every mode to every other, as a thin sheet's stretching does, at 4 multiply-adds per mode and patch. The patches' share of the rim's tension is 0.2: low enough that the texture-driven forcing (L·Q, threshold-free) carries the cascade over the parametric pumping (Q², a threshold), so the shimmer grows steadily with the strike instead of switching on, and high enough that a medium stroke still shimmers | [CTT05]: thin structures, energy transfer and chaos; [LF89]: coupling at sharp changes of shape |
| Texture | The hand-hammered sheet's irregular rise, standing for its rings of bumps and hammer marks. Each angular order p gets a random amplitude of 0.9/√p thicknesses rms (about 3 mm of irregular rise on the 1.5 mm sheet, against bump rings 8 mm high), a random phase, and the radial shape ρ^min(p,2)(1 − ρ²), which peaks in the outer half where the bump rings are. Its slope is what lets the stretching reach modes of every symmetry. The smooth central dome stiffens the sheet but does not drive the cascade | [LF89]: the bump rings break circular symmetry, and the mode conversion is at sharp changes of shape |
| Sheet modes | The tam-tam's linear part (plate, smooth dome, texture) is solved for its own modes once (Jacobi, 116 × 116). The tam-tam rings in those; only the nonlinear rest of the stretching is worked out each sample. So the stretching can fade out (once the sheet has rung 46 dB under a thickness's swing, apart from the thud) without retuning any partial | tested: every late partial within 3 c of a sheet mode |
| Guards | Numerical, not physics. Each patch's stretch beyond the linear (Q) is bounded under what would stiffen a mode past what the explicit step holds; the linear part, exact in the sheet's modes, is never touched. A tam-tam hammered past about 12 thud-thicknesses² of swing energy (a few hard strokes) is scaled back within a block. Played strokes never reach either | keeps the sheet bounded under ten hard strokes a second (tested) |
| Mallet | A mass on Hertzian felt, F = K·δ^p (p = 2.5; 1.5 for the bossed gong's beater), solved against the plate under it each sample, with the drive averaged over its padded head (radius 0.3, 0.35 and 0.12 of the face). Mass: 1.5, 0.2 and 1.3 times the fundamental's modal mass (opera, bossed, tam-tam). Felt: a rigid-face contact of 0.45, 0.55 and 0.7 of the fundamental's period at full speed, never longer than 0.7 and 0.6 periods on the opera and bossed gongs. Full speed: 4.5 (halved on the domed gongs), 0.6 and 8 times ω₁h, so every pitch is struck alike against its thickness. A flatter finger softens the felt up to 11 times | [RF83] amplitude 0.8–1.0 h at a hard blow (tested: 0.7–1.5 h); [F85] the small gong's light stick; [F99] "a heavy and softly padded striker"; [F99] soft blows nearly sinusoidal (tested) |
| Decay | T60, **inf**. Opera gong: fundamental 4.5 s·(f1/200 Hz)^−0.25 (2.5–6 s), partials ×(f/f1)^−0.9. Bossed gong: fundamental 12 s·(f1/100 Hz)^−0.6 (2.5–25 s), partials ×(f/f1)^−0.8. Tam-tam: the axisymmetric thud 2.5 s (radiation-damped), other axisymmetric modes 4 s, the rest 10 s·(f/1 kHz)^−0.3 (3–14 s) | [F99] opera glide over about a second; [KGG10] very high Q; [F99] the tam-tam's fundamental dies rapidly; [LF89] dying over as long as 10 s |
| Radiation | Each mode's volume acceleration, heard by two microphones ±0.6 rad off the axis. Axisymmetric modes as pistons (∫φ dA); a mode with m diameters as a multipole, weight √μ·q/(1+q), q = (ka/(m+1))^(m+1), with the sign of the lobe facing each microphone. A tam-tam's sheet modes are worth what their plate modes are | calc |
| Level | Pressure at a metre, ρ/(4π)·d²V/dt². On the page: a level per kind (`kGongLevel`), and across the neck a factor (f/330 Hz)^−0.75 (bounded), since a small gong struck as hard against its size radiates more | |

### Why two resolutions of the stretching

Thick and curved gongs (the opera gong, the bossed gong) stay weakly
nonlinear: they glide and sound harmonics, and a few modes do the trading
[CTT05][F99]. For them, the smooth axisymmetric in-plane modes give exact
coefficients, the opera glide reduces to Fletcher's cap law, and the cost
is small.

A tam-tam is a thin, nearly flat sheet driven past its thickness. Its
energy has to reach every mode, of every symmetry, as wave turbulence does.
With the smooth axisymmetric in-plane modes, in tests, it moved its energy
into a few sparse lines instead of a wash; the patches couple every mode to
every other.

### Keeping it in real time

- **Patches.** A tam-tam's stretching is 116 modes × 8 patches × 4
  multiply-adds a sample, worked out four modes at a time (each block of
  four modes' slopes at every patch side by side, so the slopes and then
  the forces are each one pass along one array, the forces folded straight
  into the step). That is about 1 % of a core while it shimmers, and a
  fraction once the stretching has faded. (It was 189 modes and 16 patches
  at 2.6 %. Fewer, larger patches couple more strongly each, and the sheet
  shimmers as brightly at a medium stroke; the modes dropped added
  density, not reach.)
- **Polyphony.** At most four tam-tams ring at once. A fifth note takes the
  quietest one nobody holds, as a percussionist has a few, not eight.
- **The others.** A bossed or opera gong costs well under 1 %.
- **Sleep.** A gong sleeps 100 dB under a hard strike.
- **Build cost.** The designs (Bessel functions, roots, coupling tables,
  the tam-tam's sheet modes) are built once, when the Lead's engine
  starts, in about 0.2 s.

## How it was checked

`tests/gong_tests.cpp` checks the model against the measurements above:

- the plate's roots;
- every gong on its note;
- the bossed gong's measured ratios and its ombak (rate, 2f1 against f2,
  3f1 and f1 + f2);
- the large opera gong's downward glide (+220…+400 c at the strike,
  against Table I's +287…+316, settling within a second or two), and the
  hard blow's swing of about a thickness;
- the small gong's upward glide (−80…−350 c, against −155…−323);
- the light touch's near-pure tone;
- the tam-tam's blossom (its high band, 30–90 times the thud, at least
  12 dB up between 0.15 and 2 s, the balance turning from thud to wash),
  a soft strike staying a thud, and its partials staying on the sheet's own
  modes once its stretching has faded;
- energy conservation with every loss removed, within 3 % over a second of
  trading;
- stability under ten hard strokes a second while bent;
- on the page: the sounds by name, the strike's touch, the roll's swell,
  the hand's damping;
- the budget.

The review (three rounds, by a critic briefed to judge against the
measurements) signed off the opera gong. It left these open:

- **The tam-tam's ceiling.** Its modes stop at 0.18 of the sample rate
  (8.6 kHz at 48 kHz), the same for every note. Legge's spectra reach
  10 kHz and beyond. Raising it needs the sheet run at twice the rate
  (about twice the cost), since higher modes don't stay put in the explicit
  step under hard playing.
- **The tam-tam's density.** At a 110 Hz thud the sheet is 34 cm across,
  and with 116 modes they stand about 70 Hz apart between 1 and 5 kHz,
  against about 6 Hz on a 1 m sheet (116 is what real time on the slowest
  machines allows). Late in a strong stroke's ring the wash resolves
  into lines (tens of them, not hundreds). A sheet three times the area
  would cost about three times as much.
- **The tam-tam's growth with the stroke.** About 4–5 dB per dB of strike
  speed above a medium touch, against the 2–3 of Legge's measured power
  laws. A medium stroke's shimmer is darker than a hard one's (centroid
  about 1.2–1.4 kHz late on).
- **The bossed gong's ombak.** It is 6–9 dB shallower than Krueger's
  (the boss's octave stands that far under the smith's at 2–3 s, where he
  found them about equal). The boss's mass, which the plate's shapes leave
  out, is the likely reason.
- **Stereo.** The opera and bossed gongs are nearly mono: a strike in the
  middle wakes few of the modes that differ between the microphones.

