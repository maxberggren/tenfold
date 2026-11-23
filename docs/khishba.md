# The khishba

The drums page's khishba kit is one instrument, like the hang. The whole pad
becomes the head of a khishba (خشبة, "a piece of wood"), the long narrow
goblet drum of Iraq, seen from above. It is simulated as it is built
(`src/khishba.h`) and drawn as it is played (`src/drums.cpp`,
`paintKhishba`).

- The skin fills the pad. A dashed circle in its middle is the dum.
- The ring between that circle and the rim is the tek on the right (the
  right hand's fingers) and the ka on the left (the left hand's).
- A band of wood is drawn round the skin: the rim. Past it, in the corners,
  is the drum's wooden body.

## Playing it

- **Where you tap:** the stroke is where the finger lands, and the physics
  does the rest.
  - **Dum (the middle):** three loose fingers land side by side, a fraction of
    a millisecond apart. The skin's (0,1) speaks, split in two by the air
    column (about 570 and 800 Hz). Under it, from the foot, comes a thump of
    the cup's air, strong in a soft, pressed dum and faint in a hard, snapped
    one.
  - **Tek and ka (the rim):** one fingertip near the clamped edge, partly on
    the wooden rim. It reaches the lobed modes ((1,1) near 990 Hz and up)
    and the rim's ring, and leaves the (0,1) family almost still. Further
    in, the finger sinks in longer and the dum's partials answer.
  - **Wood (the corners):** a knuckle on the body. Only the wood's ring
    modes sound.
- **How hard:** pressure sets how fast the fingers arrive and how sharply
  the hand pulls back. A harder stroke is louder, shorter in contact and
  brighter: the pulp gives way to the bone behind it. A hard dum also
  starts a little sharp while the skin is stretched.
- **No two strokes alike:** each lands a little off where it aimed, a little
  harder or softer, up to a millisecond late, so rolls and grooves never
  machine-gun.
- **A finger left on the skin damps it.** A finger that stays down longer
  than a tap (80 ms) settles onto the skin. Its pulp is a spring and a
  dashpot and the hand holds it, so it chokes the drum: in the middle by
  about 25 dB in 100 ms, at the rim a little (there it damps the wood's ring
  too). Pressing harder damps more; lifting lets the skin ring free again.
- **Closed strokes (pa, sak):** a finger landing flat (a wide contact) stays
  where it struck, already pressed in. A dum's three fingers close it within
  milliseconds; the cup's air, already moving, rings out on its own.
- **Rolls (rash):** alternate two fingers as fast as you like. Every landing
  is a stroke on the same skin, so a roll builds and never pumps.
- **Grooves:** fingers that land together and stay groove the hacha'a, the
  2/4 dance the khishba drives (`D . . . T . T .`), twice to the bar.
  - Slide up for busier: the ka fills between in ghost strokes and the tek
    doubles. The REPEAT strip and the B/N keys roll as on any kit.
- **Keys:** each row of keys plays dum, tek, ka and wood, left to right.
- **Shaping a stroke (click and drag):**
  - sideways tunes the skin, in semitones of tension (a player warms a
    skin to tighten it);
  - up lengthens the ring;
  - twist goes from the soft pad of the finger to the hard tip.

## The model

Everything is in SI units, integrated sample by sample. Each mode is a
damped oscillator, `u' = d·u − c·x + f·dt²/m` and `x' = x + u'`, with `d`
and `c` set so its poles are exactly the mode's frequency and decay.
Everything that couples is a force between them. `docs/khishba-research.md`
is the survey the numbers come from: tags in brackets below refer to its
source list.

The literature has no acoustic measurement of a khishba at all. Its
dimensions, materials and playing come from three museum specimens, one
1965 field study and the sources on the hacha'a. Its acoustics are
calculated from the physics of those dimensions, anchored on what is
measured on timpani, a doumbek, frame drums and djembes. Where the model
had to assume, the table says so.

| Part | What it is | Set from |
|---|---|---|
| Head | Radius 4.75 cm (a 9.5 cm head), glued over a one-piece wooden body 35.5 cm long. | Geidai 153–155: 35.0–35.5 cm long, heads 8.0–9.5 cm [GEI153] |
| Skin | Fish skin, σ = 0.17 kg/m² (0.15 mm at 1150 kg/m³), T = 1200 N/m: the (0,1) at 677 Hz in a vacuum. | fish skin 0.17–0.30 mm [ZHA22]; T ≈ 1200 N/m (range 500–2500) and a (0,1) of 450–800 Hz for "a high, dry tone" [GEI153], [§2.4]; well under the ~4200 N/m fish skin tears at |
| Modes | Every (m, n) with Bessel zero j_mn below the cap: 207 modes up to 8.3 kHz, pairs counted twice. Each has shape J_m(j·r/a)·cos(m(θ − θ_k)), modal mass σ·∫φ² plus its air, and stiffness T·k²·∫φ². In a vacuum they are Bessel's ratios exactly: 1 : 1.593 : 2.136 : 2.295 … | ideal membrane [§2.1] |
| Uneven skin | The two modes of each pair (m > 0) are 0.2–0.6 % apart, along axes set at random, so a ring beats. | a hand-stretched glued skin; the djembe's (0,1) measured as a split pair, 343/401 Hz [WDJ] |
| Air loading | The part of each shape that sweeps no volume carries ρ/k of air per m² outside and ρ/(k·tanh(k·d)) in the 5 cm cup: the near field of a wave slower than sound. The part that does (the m = 0 modes' piston share, A²/S of ∫φ²) carries the unflanged pipe end's mass outside, 0.6133ρ/(πa) per m³ swept, squared, and the column inside. This gives (0,1) ×0.90, (1,1) ×0.92, (2,1) ×0.94 and (3,1) ×0.95; uncoupled, (1,1)/(0,1) = 1.63. | timpani loading [RIE89] scaled to μ = ρa/σ: ×0.87–0.91, ×0.91–0.94 [§2.2]; ideal 1.59, doumbek 1.40, open frame 1.75 [SOR09], [TRO10] |
| Skin loss | Loss factor η = 0.028 (Q ≈ 36), rising 0.006 per kHz. | fish skin Q_int ≈ 20, sheep or goat 30–40 [§2.5]; viscoelastic skins lose more as the frequency rises |
| Radiation loss | Each mode's far field, integrated over the space in front: ρω⁴∫\|D(θ)\|²dΩ/(8π²c) per unit swing, where D is the Hankel transform of the shape, 2πa·J_{m+1}(j)·J_m(k₀a·sin θ)/k. The m = 0 modes, monopoles at a pipe's open end, radiate into the whole space (half the baffled resistance). The lobed modes are multipoles that hardly radiate. | radiation Q of the (0,1) ≈ 30–60 [§2.6]; timpani radiation-only T60s [RIE89] |
| Stretch | The skin's tension rises with its mean strain, T + (E·h/(1−ν))·(1/2S)·∫\|∇w\|², with E·h/(1−ν) = 4.5·10⁴ N/m. This is the Kirchhoff–Carrier nonlinearity: a hard dum starts sharp by under 1 %, as a wetted skin would. | wetted skin 330 MPa (goat) to 50 MPa (soft fish) × 0.17 mm [LI22], [§4.5]; bass drum +10 % at 6 mm [ROS92] |
| Bore | A shallow 5 cm cup (a third of a litre) narrows to a neck of 1.75 cm radius, flaring to 2.5 cm at the open foot: 35.5 cm in all. | cup 0.2–0.45 L, bore ≈ 3.5 cm, foot 4–6 cm, inferred from photographs [§1.2] |
| Column | Webster's horn equation for that bore, shot from the foot to a rigid skin: modes at 150, 616, 1123, 1593, 2077 and 2567 Hz. Each becomes a tank of the Foster series: compliance Λ/ρc² (Λ = ∫S·ψ² dz) and inertance from its frequency. The cup's pressure is their sum, driven by the volume the skin's m = 0 modes push in, and it pushes back on them. A tank's spring pulls toward the volume pushed in, so a skin held pressed in leaves no pressure. | cup + neck 164–181 Hz, tube ¾-wave ≈ 690 Hz, 1150 Hz [§3.2–3.3] |
| Foot | The foot opens a few centimetres from the player's knee or hip, not into free air. That adds half a foot radius of end correction and a resistance of 0.07 ρc/S to the unflanged end. | played "on his left knee" [KUB75], slung against the body [GEI153]; the gap is an assumption |
| Column losses | Each mode loses what the foot radiates, ½·(ρc/S·(ka)²/4 + the gap's resistance)·\|U\|², and what the walls take: viscous ¼ρωδ_v\|v\|² and thermal ¼(γ−1)ωδ_t\|p\|²/ρc² per m², ×1.5 for a carved, rough bore. Q comes out at 14, 24, 19, 15, 18 and 20. | boundary-layer theory; air-mode Q 60–90 in free air [§3.4]; roughness and gap are assumptions |
| Coupled | The skin's (0,1) (611 Hz with its air) and the column's 616 Hz mode repel: a dum rings at ~570 and ~800 Hz. The cup's mode falls from 150 Hz to ~115 Hz and sounds mostly from the foot. The coupling g/ω₁ω₂ is 0.73. | the research's own coupled model: 568 + 740 Hz, air mode 115–165 Hz [§3.4] |
| Wood | The rim's wall, 6 mm of fruitwood (E = 11 GPa, ρ = 700 kg/m³): Love's thin-ring modes n = 2 … 5, each a pair, Q 30. They radiate as pistons above the wall's coincidence frequency (2.7 kHz) and little below it. | wall 4–8 mm, one piece of wood [§1.2]; the species is not known |
| Fingers | A dum is three fingers, each 1 g at the pad (flicked from the last joint, a third of a ~3 g tip), 0.34 head radii apart, landing within 0.6 ms (closer when harder), on a 1 cm patch. A tek or ka is one 3.5 g fingertip on a 0.5 cm patch. Each has a Hertzian pulp (2·10⁵ N/m^1.5, a tangent of 3–5 kN/m at a newton) that bottoms out on the bone (10⁷ past 0.3 mm for the dum, 1.5·10⁷ past 0.4 mm for the tek), with Hunt–Crossley loss. On the rim the wood is rigid and only the finger gives. Arrival is 0.25 … 3 m/s, the ka at 0.8 of the tek. The hand pulls back in proportion to the arrival speed, lazily after a soft stroke and whipped back after a hard one: within 4 → 1 ms for the dum, 3 → 1.2 ms for the tek. Contact comes out at 2.2 → 1.5 ms for a dum and 2.8 → 0.85 ms for a tek. | fingertip pulp 0.2–7.7 N/mm [PG]; tek contact 0.7–2 ms [§4.3]; a dum needs a short contact to sound "high" (a 4 ms half-sine is −37 dB at 600 Hz) [§4.3] |
| Scatter | Every stroke strays ±0.025 radii and ±0.1 rad from where it aimed, ±6 % in speed and ±0.15 in tone, and lands 0–1 ms late. | no hand lands twice alike |
| Contact solve | Implicit. The skin under a fingertip moves far in one sample, so each force is solved (Newton's method in a bisection bracket) as the one that, applied this step, leaves the finger as far in as the force implies. | (numerics) |
| Resting finger | 12 g, its pulp 3000 N/m with a 4 N·s/m dashpot on a 1.2 cm patch. The hand presses it with up to 2.4 N, settling over 20 ms, and holds it with 4 N·s/m. It is solved implicitly. Across the rim it also puts a dashpot on the wood's ring. A closed stroke's hand starts fully pressed: three fingers over 2.2 cm in the middle. | fingertip stiffness 0.5–4 N/mm and damping 1–4 N·s/m [PG], [SER97]; the closed strokes [§4.1] |
| Sound | Two microphones a metre away, 49° off the head's axis, at bearings 37° and 54° round it from the tek's side. Each mode is heard through its exact far field at that angle, with its phase (−j)^m: the even modes through their volume acceleration, the odd ones a quarter cycle on. The foot is heard 0.33 m further away and later; its volume acceleration is −ω·Ũ(L)/p(0) per pascal in the cup. Below the boom, skin and foot push opposite ways and cancel. A point force radiates as F/σ above the highest mode, and as F/ρh on the wood. | Rayleigh integral; goblet monopole, frame-drum dipole [§5] |
| Level | 0.6: the hacha'a groove peaks near −3 dBFS through the studio, about 4.5 dB under house's loudness. Its strokes sit 17–24 dB below their own peaks, and as loud as house they would ride the master's limiter. | (the studio) |
| Room | 0.3 of the drum goes to the kit's room, 0.8 s and a little live. | (taste) |

The model's cost does not grow with the number of fingers. The skin is 207
modes stepped once per sample in one fused, vectorised loop, under 1 % of a
core while it rings. A stroke adds a contact for the millisecond or two it
touches, and the drum sleeps once it is 120 dB down.

## What it does, measured

From `tests/khishba_tests.cpp` and probes of the model as built (soft to
hard, v 0.1 … 1.0):

| | Model | Literature |
|---|---|---|
| (0,1) in a vacuum | 677 Hz | 450–800 Hz (calc) |
| Air loading, (1,1) / (2,1) | ×0.92 / ×0.94 | ×0.87–0.91 / ×0.91–0.94 (calc from timpani) |
| Dum's (0,1) pair | ~570 + ~800 Hz | 568 + 740 Hz for a 600 Hz skin (the research's coupled model) |
| Cup's air mode | 150 Hz rigid, ~115 Hz coupled, Q 14 | 164–181 rigid, 115–165 coupled (calc) |
| Dum centroid (0–50 ms) | 630 → 770 Hz | 700–900 Hz (inferred) |
| Dum boom against the head pair (0–100 ms) | +3 dB soft → −9 dB hard | a faint thump (inferred) |
| Dum T60 | 120 → 70 ms | 100–200 ms (inferred) |
| Tek centroid (0–50 ms) | 570 → 1010 Hz energy-weighted, ~1.5–3 kHz power-weighted | 1.5–2.5 kHz (inferred from mode lists) |
| Tek 4–8 kHz share | −53 → −32 dB | |
| Tek T60 | 180 → 57 ms | 20–60 ms (inferred) |
| Contact: tek / dum | 2.8 → 0.85 ms / 2.2 → 1.5 ms | 0.7–2 ms / 4–8 ms (by analogy with sticks) |

## How it was checked

A reviewer briefed to be hard to impress rendered strokes and measured
their spectra, decays, contacts, stereo, headroom and cost against these
numbers, over three rounds, and signed off on the third. Between rounds it
caught:

- air loading counted twice on the m = 0 modes;
- a dum that was a 115 Hz boom with a click on top, because its contact was
  too long to reach the head's own partials;
- a tek with no crack;
- rolls that repeated themselves exactly;
- a pulp far stiffer than measured fingertips;
- a wood knock out of phase between the two ears;
- peaks that would have driven the master's limiter.

It accepted these deviations, which stand:

- **The dum brightens little with velocity.** Its centroid moves only from
  about 760 to 810 Hz (power-weighted). What changes with force is the
  boom's balance (about 9 dB) and the decay. With a realistic pulp, the
  light finger and the skin set the contact, and a soft, pressed dum
  being boomier is right for a goblet drum.
- **A hard dum has no slap.** Finger-on-skin impact noise is not modelled,
  so above 4 kHz a fortissimo dum stays 60 dB down. Next to a recording, a
  hard dum may sound a little clean.
- **Closed strokes leave the cup ringing.** A closed dum's EDT halves
  (68 → 37 ms), but the cup's air behind the pinned skin is still a
  Helmholtz resonator, Q ≈ 15, and rings out. A closed tek barely changes:
  a finger at the rim sits near every mode's node.
- **The dum and the tek share their strongest partial, the upper (0,1)
  partner near 800 Hz.** So Kubica's "two tones a fifth apart" is not a
  pitch here. The contrast is brightness (about 800 Hz against
  1.8–2.5 kHz, power-weighted) and decay.
- **The dum is mono at the microphones.** Its m = 0 modes radiate alike in
  every direction; the room decorrelates it.

It left three small things wrong. Two are now fixed:

- A fingertip in the rim's last millimetre made the skin a rigid wall. The
  wood now takes the stroke there.
- The tek's velocity curve had a 13 dB knee. It now spans 36 dB with
  steps of at most 6 dB above the softest.

The third is documented:

- **The dum's tone control reverses at fortissimo.** At ff the soft pad
  bottoms out on the bone and comes out brighter than the tip.

`docs/khishba-research.md` holds the literature survey these numbers come
from, with its sources. `tests/khishba_tests.cpp` checks the model against
them.
