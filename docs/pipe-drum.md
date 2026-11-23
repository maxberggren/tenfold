# The pipes

The drums page's kit after the hang is a rack of ten PVC drain pipes
slapped on their mouths with a flip-flop: a tubulum, thongophone or pipe
drum. It is simulated as it is built (`src/pipedrum.h`) and drawn as the
player sees it (`src/drums.cpp`, `paintPipes`). The research behind it is in
`docs/pipe-drum-research.md`.

## Playing it

- **The pad is the rack, seen end-on.** There are ten mouths, lowest at the
  left, zig-zagging up the scale between a near row and a far one. Each
  bore shows the far end's daylight, smaller the longer the pipe, and each
  pipe's length is written under it. Every point of the pad plays the
  nearest mouth.
- **Tap a mouth** to slap it. Pressure sets how fast the sole arrives
  (0.03–4.7 m/s). A harder slap is louder, and brighter because the foam
  stiffens and its contact shortens.
- **Off the mouth's middle**, the sole still covers the whole mouth until
  it lands most of a radius off. Further out it lands on one side of the
  rim: it seals less, pushes less air in, and more of its force goes
  across the pipe into the plastic. You get a glancing knock rather than a
  pop.
- **Hold the finger down** and the hand slaps the paddle back on the mouth
  and keeps it there. This happens once the finger has been down 0.2 s, as
  long as a roll's first step would wait, so a relaxed tap never slaps
  twice.
  - The pipe is now closed at one end and drops to its closed note: a
    little more than an octave down, odd harmonics only.
  - Slide the finger off the mouth and the paddle tilts up off the rim. The
    note glides up toward the open one, choking on the way as a
    half-covered hole does.
  - Lift the finger and the paddle comes off with a suction pop: the open
    note again.
- **The rest of the page** works as on any kit:
  - The REPEAT strip rolls.
  - Fingers landing together groove: a melody along the rack.
  - A pipe's "tune" moves it to another pipe's note.
  - Twist sets how long the pipes ring, as a rougher or smoother bore.
- **Keys:** Z X C V A S D F Q W are the ten pipes, lowest first.
- **Tuning:** the pipes follow the key, ten degrees of its scale up from
  the root near 125 Hz.
  - D minor: D3 (1.13 m) … F4 (0.45 m).
  - A minor: A2 (1.53 m) … C4 (0.62 m).

## The model

Everything is in SI units, integrated sample by sample.

| Part | What it is | Set from |
|---|---|---|
| Air column | A digital waveguide per pipe: a pressure wave each way, the length of the pipe at the phase velocity of its note, with a first-order Thiran allpass for the fraction of a sample. | exact for a cylinder (Fletcher & Rossing ch. 8) |
| Air | c = 343.2 m/s, ρ = 1.204 kg/m³ at 20 °C. | Keefe 1984 fits |
| Boundary layer | Attenuation α = 2.95 × 10⁻⁵ √f / a Np/m. It is fitted by one pole per direction at the note and its third harmonic, with the pole's delay taken out of the line. Phase velocity c(1 − α/k) at the note, which cuts the pipe a few mm shorter. | Kirchhoff / Keefe 1984 (ν, γ, Pr at 20 °C) |
| Wall compliance | Korteweg: c/√(1 + 2aρc²/Eh), 0.07 %. | thin-walled tube |
| Bore | JIS K 6741 VU 50: 60 mm outside, 2.0 mm wall (1.8 mm minimum), a = 28 mm. | ESLON catalogue |
| Open ends | Each end is solved every sample as a lumped load: inertance ρ·0.6133a/S in parallel with resistance 1.505 Z₀ (trapezoid rule). Its end correction is 0.6133a at low ka and falls with frequency (0.592a at 700 Hz). Its loss is (ka)²/4 · Z₀. A high-amplitude jet loss of ½ρu²·0.6 is added. | Levine & Schwinger 1948; Silva et al. 2009; vortex shedding at sharp ends |
| Cut to length | L = c_phase(f)/2f − 2δ(f), with δ(f) the network's own phase at the note. The pipe sounds its note within a cent. | open–open: Johnson 2012, Sundberg et al. 2018 |
| Through-flow | A steady flow straight through the pipe leaks away below 2 Hz. The leak's phase lead is allowed for in the length. | bore friction (Poiseuille), the ends' jets |
| Paddle gap | The mouth with the sole at height h is the open end plus the gap's inertance ρt_p/S. t_p is Dalmont & Nederveen's pad correction, with w = 2 mm and the sole reaching R = 5 cm, and never less than the radial sheet's ρ/(8πh). The gap's flow loses ½ρ(U/0.6·2πah)². Both fade smoothly to nothing at h = 3a, where the paddle is let go; the inertance is read from a table over log h. The flow through the gap is stepped backward and carries on as the inertance changes, so nothing steps when the gap appears or vanishes. The air sees the seal close only as fast as the foam conforms over the rim's last 0.3 mm at the sole's speed (a hard slap seals in a few samples, a soft one in a millisecond or two), and lets go at once. | Dalmont & Nederveen 2001 Eq. 48; radial-flow inertance; sharp-orifice contraction 0.6 |
| Paddle push | The sole's face over the mouth pushes S·v into the end region, split by the solve between the pipe and the gap. Its weight falls off as 1/(1 + (h/0.5a)²) with height, to exactly zero at 3a. The sole is a disc of 5 cm radius round where the finger lands, so it covers the whole bore until it lands 0.8 radii off centre. Past that it covers the two circles' lens, and the uncovered crescent is a hole of its own (inertance ρ·0.85√(A/π)/A) alongside the gap. A sole on the rim beside the mouth still squeezes a tenth of the push in. The weight fades in over 1.5 ms (smoothstep) from the strike: the sole is put 3 ms of flight off when the hit is due. The pressure in the mouth pushes back on the sole. | piston at a closed end: p = ρcv |
| Sole | 0.25 kg as the wrist flicks it: a flip-flop (100–200 g) and the hand. The foam is EVA, E = 6 MPa, 12 mm thick. The rim's annulus sinks into it with k = 0.85·E·A_rim/T (1.5 × 10⁵ N/m, softened by the sole's flex). Over the first 0.1 mm the force grows as δ² (the sawn edge's rounding, C¹ into the line). It stiffens ×(1 + δ/2 mm) as its cells close, with Hunt–Crossley loss λ = 0.4 s/m (restitution about ½ at 2–4 m/s). Contact runs from 3.5 ms soft to 2.6 ms hard. A pad's tone moves k by ×4^tone and the mass by ×2^(−tone/2): 4.6 ms for a soft sole, 1.7 ms for a hard paddle. | Chen et al. 2024 (EVA 2.8–7 MPa at 80–180 kg/m³; soles 150–250); EVA rebound resilience 40–60 %; contact 1–8 ms |
| Slap | Arrival 0.03–4.7 m/s (velocity^1.2), from 0.6–12 mm off. Once the sole lands, the hand pulls it back at v₀/4 ms, to no faster than 0.3 + 0.4·v₀ (at most 1.5 m/s). The body spans about 29 dB from the softest tap to the hardest, and the high end grows 8 dB brighter. A full slap puts about 1.5 kPa in the mouth. | Dahl 2011 (striking speeds); ρcv |
| Hold | Slap and stay: the hand puts the sole back on at half a slap's pace, 0.2–2 m/s (as hard as the finger landed): a "thup", not a second hit. From the moment it lands, the hand holds it where it is, its pace carrying on and easing to a stop, and presses on to 0.8 mm into the foam on a 40 kN/m grip damped 1.2 × critical: it doesn't bounce. Its pace speeds up and slows down at no more than 50 m/s². The pressed foam seals to a 5 µm leak, shrinking as it is pressed. Tilting takes it to 0.4·gap²·a at 0.03 m/s, and lifting takes it away at 0.6 m/s. A hand gives up after 6 s. While a paddle is held, the pipes send a third as much to the room: the player leans in. | Echegorri et al. 2026 (palm kept on: odd harmonics only) |
| Plastic | Per pipe: two axial modes (√(E/ρ)/2L), three free–free bending modes ((βL)² = 22.37, 61.67, 120.9; I/A = a²/2) and two ovalling modes (ring n = 2, 3: 455 and 1286 Hz). Modal masses are ½, ¼ and ½ of the pipe. E = 2.94 GPa, ρ = 1430, ν = 0.38, η = 0.03 (bending +0.04 for the rack's rubber). The sole's force drives them through the rim, with its share across the pipe 0.15 struck square and up to 0.75 glancing. The rim's axial motion is in the contact. | JIS / MISUMI; IUPAC 1973 (η 0.028–0.040) |
| Plastic's sound | The axial modes through the rim's annulus at each end, the bending modes as a line dipole (∝ ka), ovalling as a quadrupole (∝ (ka)²), all × 0.5. | radiation of small sources |
| Sound | Two overhead microphones, as a drum kit is recorded: 70 cm apart, 90 cm above the near row and 35 cm out over the pipes. Each hears every pipe's two ends: the volume of air leaving each, after its own time of flight, at 1/r. The far ends are a pipe's length away, back and down at 34°. From there the two ends are about as far away. The fundamental, which both ends sound in phase, adds up, and the even harmonics, which they sound in opposite phase, partly cancel: the round boom a pipe drum is known for. A player bent over the mouths hears the far ends half a period late, and less boom. Pressure is ρ/4π · d(Q/r)/dt. The sole's crack is 0.03 × ρAd/(4πrc) × its jerk, rolled off at 2.5 kHz. | monopole radiation; plate dipole |
| Level | 0.48 of full scale per pascal at the microphones. A pipes groove is within 1 dB of house's loudness, and peaks lower than house's (0.71 against 0.74); a single full slap peaks at 0.46. | the drums page's level rules |

## What it costs

Each sounding pipe is two delay lines, three first-order filters, two
lumped ends, a paddle and seven modes: a few dozen operations a sample. The
gap's air is read from tables, not computed. A pipe sleeps once its air and
its plastic have rung out, 80 dB down, with no paddle on it.

- One pipe ringing: about 0.12 % of a core; held, a little more.
- All ten struck at once: about 1 %.
- Ten fingers drumming through the whole studio: p99 load about 3 %.

## Where it parts from a real rack

- **Johnson's tuner readings run sharp of the textbook.** His seven pipes
  (UIUC 2012) sound 6–47 cents (mean about +20) above c/2(L + 2·0.6133a): an
  end correction about half Levine & Schwinger's. His pipes had sockets on
  both ends and a diameter quoted ambiguously. The model follows Levine &
  Schwinger, so its printed lengths may be a centimetre or two longer than
  a builder would cut by ear.

- **T60 is the theory's.** The fundamental rings 1.0–1.4 s in the low
  pipes. A real rack's rough cut ends, joints and supports take some of
  that away, and no measured paddle-struck T60 was found to set it by.
- **The paddle's coupling to the air is lumped.** The sole is one piston
  over the mouth, and its air pushed sideways is a fraction that falls with
  height. That is not a 3-D flow solution.
- **The closed note on hold comes after the slap.** A real player who
  leaves the paddle on never lets it bounce. On a trackpad a tap lasts
  50–160 ms, so the hand slaps the paddle back on at 0.2 s instead and
  stays: a slap, then a closing "thoomp" an octave down. How much of each
  closed harmonic sounds depends on where in its cycle the open note was
  when the sole landed, so the closed note's colour varies from slap to
  slap.
