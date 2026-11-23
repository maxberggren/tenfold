# The pipe drum: research

The kit after the hang is a rack of PVC pipes slapped on their mouths with a
flip-flop. The instrument has many names: tubulum, thongophone (Papua New
Guinea's, struck with rubber thongs), slap tube, or plain "pipe drum", as in
the street drummer's video this kit was asked for (Zooich, "pipe drum",
youtube.com/shorts/DKAyFSjJvyk: a rack of tubes angled toward the player,
their open ends facing her).

This page is what the model (`src/pipedrum.h`) was built from. `docs/pipe-drum.md`
maps each constant to the line here it answers to. Items are marked:

- **[source]**: read in the source named.
- **[code]**: taken from a published research code that cites the paper.
- **[derived]**: calculated here from cited inputs, method given.

Notation: a is the bore's radius, k = ω/c, S = πa², Z₀ = ρc/S, L the pipe's
length, L′ its acoustic length (with end corrections).

## 1. The air column

### Open at both ends, or closed at one

A uniform cylinder's air is a one-dimensional wave system: pressure waves
travel down it at c and reflect at each end.

- **Open end:** an open end is nearly a pressure node. The wave comes back
  inverted.
- **Open at both ends:** the resonances are f_n = n·c/2L′, for n = 1, 2, 3, …:
  every harmonic. The half-wave pipe.
- **Closed at one end:** a rigid lid is a velocity node. The wave comes back
  upright. The resonances are f_n = (2n − 1)·c/4L′: only the odd harmonics,
  with the fundamental near an octave below the open pipe's.
- **Exact frequencies (Fletcher & Rossing, *The Physics of Musical
  Instruments*, ch. 8):**
  - Open at both ends: L′ = L + 2δ.
  - Closed at one end: L′ = L + δ (a flat lid adds nothing).
  - So the closed note is not quite an octave down. It is
    f_closed / f_open = (L + 2δ) / 2(L + δ), a little over a half: +18 cents
    on a 1.5 m pipe, +60 cents on a 0.45 m one.

### Air at 20 °C [code]

Keefe's (1984) temperature fits, as in Scavone's TMMT `thermoConstants.m`:

| Property | Value |
|---|---|
| c | 343.3 m/s |
| ρ | 1.204 kg/m³ |
| μ | 1.814 × 10⁻⁵ Pa·s |
| γ | 1.402 |
| √Pr | 0.841 |

c rises about 0.17 % per °C, about 3 cents per °C. Johnson (2012, below) saw
his lowest pipe's best length move 2.4 cm across a classroom's swings.

## 2. End corrections and radiation

### The unflanged end

**Levine & Schwinger (1948)**, "On the radiation of sound from an unflanged
circular pipe", *Phys. Rev.* 73:383. The values below are as tabulated by
Silva, Guillemain, Kergomard, Mallaroni & Norris (2009), "Approximation
formulae for the acoustic radiation impedance of a cylindrical pipe", *JSV*
322:255 (arXiv 0811.3625) [source].

- **End correction:** δ → 0.6133a as ka → 0. It falls slowly with frequency,
  δ/a ≈ 0.6133 − 0.1168(ka)², to about 0.6a at ka = 0.3 and about 0.5a at
  ka = 1.
- **Reflection:** |R| ≈ 1 − (ka)²/2 at low ka. Equivalently, the real part
  of the radiation impedance is (ka)²/4 · Z₀.
- **Dalmont, Nederveen & Joly (2001)** fits, valid to ka ≈ 1.5 [code: Scavone,
  `radiation.m`]:
  - |R| = (1 + 0.2ka − 0.084(ka)²) / (1 + 0.2ka + 0.416(ka)²)
  - δ/a = 0.6133·(1 + 0.044(ka)²)/(1 + 0.19(ka)²) − 0.02·sin²(2ka)

### The flanged end

- δ → 0.8216a.
- Re Z = (ka)²/2 · Z₀: twice the unflanged.
- A pipe resting its far end against a floor or a board is partly flanged
  and sounds a little flat.

### A network for the time domain [derived]

The radiation impedance can be built from a parallel inertance and
resistance, Z = jωL·R / (R + jωL).

- At low frequencies this is jωL + ω²L²/R.
- Matching both low-ka terms gives L = ρ·0.6133a/S and R = 4(0.6133)²·Z₀
  = 1.505 Z₀.
  - L gives the end correction.
  - L²/R gives the (ka)²/4 resistance.
- Above ka ≈ 1 the network's reflection tends to (R − Z₀)/(R + Z₀) = 0.20
  rather than to 0. That is harmless here: by then the wall has already
  taken the harmonics.
- The network's own end correction falls with frequency, as the true one
  does: 0.6119a at 175 Hz and 0.592a at 700 Hz, for this bore.

### Why the open end makes a round, boomy tone

The open end reflects (ka)² less than everything.

- For a 56 mm bore, ka = 0.08 at 150 Hz: the note loses 0.3 % of its
  amplitude per bounce.
- At 1.5 kHz, ka = 0.77: the end lets out a quarter of the amplitude every
  bounce.

The upper partials leave the pipe within a few round trips, while the
fundamental stays and rings. What comes out of the pipe is therefore low,
round and nearly sinusoidal after its first tens of milliseconds. That is
the "pop" tone of a slap tube, and why players call it a boom.

## 3. The walls' boundary layer

**Keefe (1984)**, "Acoustical wave propagation in cylindrical ducts", *JASA*
75:58. The losses are as implemented in Scavone & Darabundit's
`sectionLosses.m` [code]. Kirchhoff's result, with r_v = a√(ρω/μ) = 645.7·a√f
at 20 °C:

- **Attenuation:** α = k · 1.045/r_v = **2.95 × 10⁻⁵ √f / a** Np/m.
  - This is ν = 1.51 × 10⁻⁵ m²/s, with the thermal layer's (γ − 1)/√Pr
    added to the viscous one.
  - Fletcher & Rossing give 3 × 10⁻⁵ √f / a.
- **Phase velocity:** v = c·(1 − α/k) = **c·(1 − 1.62 × 10⁻³ / (a√f))**.
  - The same layer that wears the wave away slows it.
  - In a 56 mm bore at 150 Hz this is −0.47 %, or **−8 cents**.
  - The upper harmonics are slowed less, so the series is stretched by a
    couple of cents.
- **Wall compliance (Korteweg) [derived]:** a thin PVC wall gives under the
  pressure, slowing the wave by 1/√(1 + 2aρc²/Eh).
  - For VU 50 that is 0.07 %, about 1 cent. It is in the model's tuning
    for completeness.

## 4. The paddle

### A pad at height h over an open end

The paddle at a gap h above the rim is a woodwind key over its tone hole.
The air leaving the pipe squeezes radially through the gap, and that air's
inertance adds a length to the end.

- **Dalmont & Nederveen (2001), Eq. 48** [code: Scavone, `tmmTonehole.m`]:
  the extra length is

      t_p = a / [3.5(h/a)^0.8·(h/a + 3w/a)^−0.4 + 30(h/R)^2.6]

  with w the wall's thickness and R the pad's radius. It falls to nothing
  past h ≈ a, and grows without bound as h → 0.
- **Keefe (1990) / Benade** give a pad term within about 10 % of it for
  0.1 ≤ h/a ≤ 1 [code: WWIDesigner].
- **Radial flow between two discs** [derived]: with the pipe's flow entering
  uniformly over the bore, v(r) = Qr/(2πa²h). The kinetic energy gives an
  inertance **ρ/(8πh)**: an extra length of a²/8h. This is the asymptote as
  h → 0, which the fit approaches more slowly (as h^−0.8). Below h ≈ 0.05a
  the fit is out of its data, so the model takes the larger of the two.
- **Losses in the gap:**
  - The flow through a narrow gap separates into a jet, and loses its
    dynamic head, Δp = ½ρ(U/C_cA)², with A = 2πah the curtain's area and
    C_c ≈ 0.6 the contraction of a sharp-edged orifice.
  - Below h ≈ δ_v = √(2μ/ρω) (0.16 mm at 100 Hz) viscosity takes over as
    well.
  - The two together are why a nearly-closed tone hole is lossy, and why a
    slowly tilted paddle chokes the tone on its way from closed to open.

### Does the slapped pipe sound open or closed?

No peer-reviewed study of a paddle-struck tube was found. The builders'
measurements agree:

- **Johnson (2012)**, "Construction and Properties of a Tubulum", UIUC
  Physics 406 report [source].
  - 1.5″ PVC, a flat rubbery beater, a tuner, seven pipes:
    0.45 m → 374.3 Hz, 0.74 → 229.7, 1.16 → 148.5, 1.57 → 109.7,
    1.85 → 93.0, 3.07 → 56.3, 3.36 → 51.5.
  - These fit only the **open–open** half wave, f = c/2L′.
  - His fitted total end correction, 0.325 D, is below Levine's 2 × 0.6133a.
    His pipes had coupling sockets at both ends and a diameter quoted
    ambiguously.
- **Sundberg, Barton & Walter (2018)**, "A Robotic Percussive Aerophone",
  NIME'18 [source].
  - 7 cm pipes, 2.61 m (C2) to 0.58 m (C4).
  - Cut by c/2f less 0.6d per open end: **open–open**.
  - A strong second harmonic, which a closed pipe could not have.
  - Their foam paddles excite the air "without needing to contact the top
    of the tube": the squeeze of the approach alone.
- **Echegorri, Monteiro, Stari & Martí (2026)**, arXiv 2609.31131 [source].
  - A 1.498 m PVC tube, a = 18.5 mm, struck with an open palm **kept on the
    end**.
  - Odd harmonics only, 19 of them, (2n − 1)·c/4L′ with L′ = L + 0.6133a:
    56.3, 170.5, 285.0, 398.6, 513.1 Hz …
  - "Any premature bounce would alter the acoustic impedance."
  - The closed pipe, measured.
- **"Octavator" caps** sold for Boomwhackers drop them one octave by closing
  one end, for the same reason.

So the paddle that bounces off leaves an open pipe; the paddle kept on
closes it.

### The physical picture [derived]

- The sole approaches. Within a few millimetres of the rim the gap's
  inertance makes the mouth nearly closed, and the sole pushes the column
  like a piston. Inside the mouth that is p = ρcv: about 2 kPa for a
  5 m/s slap. Slap tubes are loud.
- The foam lands on the rim and springs back. Contact lasts a few
  milliseconds: π√(m/k) for a sole of effective mass m on the stiffness of a
  rim sunk in EVA.
- The hand pulls the sole away, and the gap opens again.
- A long pipe's round trip, 2L/c, is about 6 ms for 1 m: comparable to the
  contact. So the first round trip sees a nearly closed mouth (a quarter-wave
  "thoomp"), and then the pipe rings open (the half-wave note).
- The faster and harder the slap, the shorter the contact. The foam
  stiffens as its cells close. So a harder slap is louder and brighter.

### Contact time and swing

Neither has been measured for a flip-flop.

- **Proxy:** stick-on-drumhead contacts are 5–8 ms at mf, and tip speeds
  reach 10–12 m/s (Dahl 2011, "Striking movements", *Acoust. Sci. & Tech.*
  32:168) [source].
- **A flip-flop's [derived]:**
  - Mass: 100–200 g (one listing: 158 g), with the hand behind it as the
    wrist flicks.
  - Contact: the rim annulus sinks into EVA of E ≈ 5–10 MPa at sole
    densities (Chen et al. 2024, *Polymers* 16:34) [source], so 1–8 ms.
  - Speed: 3–10 m/s.

## 5. The plastic

- **Material: rigid PVC** (JIS, via the MISUMI catalogue) [source]:
  - E = 2.94 GPa
  - ρ = 1430 kg/m³
  - ν = 0.35–0.40
  - Bar speed √(E/ρ) ≈ 1434 m/s
- **Loss factor:** 0.028–0.040 near 1 kHz (IUPAC working party, Gonze &
  Chauffoureaux 1973, *Pure Appl. Chem.* 35:315). Seen only in an excerpt.
- **Pipes: JIS K 6741** (ESLON catalogue) [source]. The VU pipes are the thin
  drain pipes a Japanese street drummer buys:

| Pipe | OD | Min wall | Bore |
|---|---|---|---|
| VU 40 | 48 mm | 1.8 mm | 44 mm |
| VU 50 | 60 mm | 1.8 mm | 56 mm |
| VU 65 | 76 mm | 2.2 mm | 71 mm |
| VU 75 | 89 mm | 2.7 mm | 83 mm |
| VP 50 (pressure pipe) | 60 mm | 4.1 mm | — |
| US Sch 40 2″ | 60.3 mm | 3.9 mm | — |

- **The pipe's own modes [derived]:**
  - **Axial**, a free bar: f_n = n·√(E/ρ)/2L. That is 650 Hz for a 1.13 m
    pipe. The rim moves along the pipe: what a slap on the rim drives first.
  - **Bending**, a free–free beam of a thin tube (I/A ≈ a²/2):
    f = (βL)²/(2πL²)·√(E/ρ)·a/√2, with (βL)² = 22.37, 61.67, 120.9. About
    80, 230 and 450 Hz for 1.13 m. A paddle glancing off one side of the
    rim drives these. The rack's rubber damps them.
  - **Ovalling**, a ring (n = 2, 3):
    f_n = (1/2π)(t/a_m²)√(E/12ρ(1 − ν²))·n(n² − 1)/√(n² + 1). For VU 50
    that is **455 and 1286 Hz**, in the same range as the air's notes: thin
    drain pipe knocks where thick pressure pipe (939 Hz for Sch 40) rings
    higher.
  - Each decays as τ = 1/(πfη): tens of milliseconds. A dry plastic knock
    under the note, never a ring.
- **How they couple:**
  - The wall's modes hardly change the air's volume, so they couple weakly
    to the column. Through the wall's compliance the effect is 0.07 % of the
    speed.
  - They are driven mostly through the paddle's contact force on the rim.
  - They radiate weakly: the axial mode through the rim's annulus, bending
    as a line dipole, ovalling as a quadrupole.

## 6. How long the pipes ring

The only measurement found is Echegorri et al.: a palm-closed 1.5 m tube
rings 0.36–0.40 s until it falls below the room's noise. That is not a T60.

Theory [derived], with T60 = 6.91/σ, σ_wall = αc and σ_rad = c(ka)²/2L′
(two open ends):

| Pipe | L | f₁ open | T60 radiation only | T60 wall only | T60 |
|---|---|---|---|---|---|
| VU 40 | 0.6 m | 274 Hz | 2.0 s | 0.91 s | **0.63 s** |
| VU 40 | 1.0 m | 167 Hz | 9.0 s | 1.17 s | **1.03 s** |
| VU 50 | 1.0 m | 166 Hz | 5.7 s | 1.49 s | **1.18 s** |
| VU 40 | 2.0 m | 85 Hz | 69 s | 1.64 s | **1.60 s** |
| VU 65 | 0.3 m | 499 Hz | 0.13 s | 1.09 s | **0.12 s** |

- Wall losses dominate below a few hundred hertz. Radiation dominates for
  short or wide pipes.
- The upper partials die faster: σ_rad ∝ f², σ_wall ∝ √f.
- Real pipes add losses at rough cut ends, joints and the rack, so the
  figures above are upper bounds.

## 7. Tuning by length

Cut for the open note: L = c_phase/2f − 2δ.

- **Johnson's 24-note chart:** C2 2.633 m down to B3 0.686 m, at 24 °C
  (c = 346.5), D = 4.8 cm.
- **Sundberg et al.:** C2 2.61 m to C4 0.58 m.
- **This kit:** the key's scale up ten degrees from its root near 125 Hz,
  on VU 50.
  - In D minor: D3 1.13 m, E3, F3, G3, A3, Bb3, C4, D4, E4, F4 0.45 m.
  - In A minor, the studio's default: A2 1.53 m … C4 0.62 m.

## Not found

- Hopkin & Dadson's *Slap Tubes and Other Plosive Aerophones* (2007), chapter 3
  "Tuning and Construction". It is sold, not online.
- A measured T60 of a paddle-struck pipe.
- A paddle's contact time.
- A loss factor for neat EVA.
- US thin-wall drain pipe dimensions.
