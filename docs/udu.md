# The udu

The drums page's last kit is an udu, the Igbo water-pot drum. Like the
hang before it, it is not a kit of samples. The whole pad becomes one
instrument of clay and air, simulated as it is built (`src/udu.h`) and
drawn as it is played (`src/drums.cpp`, `paintUdu`).

The pad shows the pot from the front and a little above:

- the round belly, over most of the pad;
- the side hole, on the front of the belly to the left, with its collar;
- the neck, rising from the top of the belly;
- the mouth, at the top of the neck, an ellipse seen a little from above.

Each opening is drawn dark as far as it is open and filled as far as a
hand covers it. Rings of air breathe out of the openings while the bass
sounds.

## Playing it

- **The side hole:** a tap is a palm slapped over the hole.
  - The bass sounds at the mouth's note while the finger stays on the hole.
  - It glides up almost an octave, to the open note, as the finger lifts.
    That glide is the udu's "whoop".
  - A quick tap whoops at once. Held longer, it stays deep until you let go.
  - Pressure sets how hard the palm comes.
- **Covering:** a finger resting on the hole or the mouth covers it by
  area. The more of the opening its hand spans, the lower the bass, and
  sliding over an opening bends the pitch continuously.
  - Cover the mouth with one finger and slap the hole with another for the
    hole's note, a major third under the open one.
  - Slide a finger onto the mouth while the bass rings for the liquid,
    falling bend.
  - A finger that lands on the clay and slides onto an opening stops
    playing the clay and covers the opening.
- **The mouth:** a tap there is a palm over the mouth. Let go, it sounds
  the open note; held, the hole's.
- **The body and the neck:** taps ring the clay.
  - On the neck near its lip, the "cowbell" family.
  - Near the hole, the hole's own pair of modes.
  - On the belly, its own modes.
- **The rest of the page:** works as on any kit.
  - Rolls and grooves come from fingers held on the clay. Fingers over the
    openings cover them instead.
  - Loops keep the slaps, the strokes and the covering, which the page
    sends as a sound of its own (On, Move, Off).
  - A pad's tune (a click and a sideways drag) makes the pot bigger or
    smaller by semitones.
  - Twist sets how long the clay rings.
- **Tuning:** the pot is sized to the key, its open bass on the key's root
  nearest 140 Hz (between 99 and 198 Hz). That makes it 3 to 25 litres,
  the range udus are made in. The measured pot, 10.2 litres, sounds 134 Hz.

## The model

Everything is in SI units and integrated sample by sample. A pot of
another size is the same pot scaled: every length by s, every frequency by
1/s. The constants below are for the reference pot, s = 1.

| Part | What it is | Set from |
|---|---|---|
| Cavity | 10.2 L of air, compliance C = V/ρc² (ρ = 1.204, c = 343). | [VA22]: 10.2 L by water fill; [AHG13]: Utar 9.25 L |
| Mouth | A neck 4.4 cm across inside and 6.3 cm long. End corrections: 0.82a inside (flanged by the shoulder), 0.61a at the free lip (Levine–Schwinger). Mass ρL′/S. | [VA22]: R = 2.2 cm, L = 6.3 cm, L′ = L + 1.4R |
| Side hole | 6 cm across in a 12 mm wall, flanged both sides (0.82a + 0.85a). | [VA22]: R = 3.0 cm, wall 1.2 cm, L′ = L + 1.7R |
| Two openings | One cavity and two plugs: conductances add, f² = f₁² + f₂². The model gives 68.6, 115.3 and 134.2 Hz (hole shut, mouth shut, both open). | measured 68/116/136 Hz [VA22], 73/113/137 Hz [AHG13] |
| Standing waves | Three cavity modes: l = 1 vertical and horizontal (ka = 2.08, ≈ 0.85 kHz), and l = 2 (ka = 3.34). Each is normalised so (1/V)∫ψ² = 1, which puts it at 1.93 (l = 1) and 2.68 (l = 2) at the wall. The plugs and the clay drive them where they open, and the plugs hear them. Below resonance they load an opening as a mass kc·ψ²/ω², the first terms of the modal sum its inner end correction is, so that mass is taken back out of the plug. Q = 35 for the porous unglazed wall (Sabine with α ≈ 0.05–0.08 gives 35–55). A slap's pulse rings them: the hollow "pok" in its attack. | first non-Helmholtz mode 570–900 Hz (survey §4.4) |
| Losses | Each plug: Ingard's viscous and thermal layers, (ρ/S)·√(2νω)·(L/a + 2). Radiation: a small source in free air, ρck²/4π, on the summed flow of both openings (they share one near field). The cavity walls' thermal layer: 1/Q = (γ−1)·δ_t·A/2V. All of them are taken at the frequency the air rings at now (radiation ∝ ω², the layers ∝ √ω). Q = 43 open, 56 with the hole held, 53 with the mouth held. Rendered, a held hand on the hole gives about 50. | the survey's loss budget: Q 48 open, 59 mouth-only; a moringa with a hand on its hole measured Q = 57 [AF21] |
| Jet | The flow separating at a lip: Δp = ρu\|u\|/2C_d² with C_d = 0.6, on each opening and on the gap under a closing palm. Under a lifting palm the gap is a radial diffuser and loses about half its dynamic pressure. A hard slap's bass falls faster at first. | Ingard–Ising orifice nonlinearity; survey §2.6: Q ≈ 20 at 3 m/s |
| Palm | 0.4 kg on its flesh, F = K·δ^1.5 with K = 6e5 on the wall round the hole and a third of that on the mouth's narrow lip (×2^(0.8·tone)), and Hunt–Crossley loss 0.25 s/m. It arrives at 0.02–3 m/s (a hard clap's speed), from as far above as it travels in 3 ms. Coming down it drives the air over the opening, and a quarter of the film it squeezes off the lip (b = 2a over the hole), into the pot. Once it has bottomed out, only its pad over the opening faces the air. Coming back out, the flesh gives back little (soft tissue's low restitution). The forearm follows through: from the moment the palm touches, it presses on with up to 25 N (more the harder the slap, or the harder a resting finger presses) and drags on any rebound, so the palm stays on the lip, sealed, for 45 ms (or for as long as a finger rests there). Then the arm lifts it, coming up to speed over 10 ms, faster after a harder slap (0.4–1.2 m/s), its heel last. A second slap on a palm still down is the arm's push over 4 ms: it never jumps. The pulse runs from tens of pascals (vel 0.05) to about 400 Pa (vel 1), rising over several milliseconds, and the whoop rises its full octave at every strength. A finger laid on an opening comes down gently (5 cm/s), presses in over 60 ms with the arm easing it both ways so it settles rather than rings, and lifts slowly (0.25 m/s). | survey §6: pulse 40–200 Pa (hard ≈ 1 kPa), 4–10 ms; hand ≈ 0.4 kg; release 0.3–1 m/s; claps 0–4 m/s [FU25] |
| Palm on clay | The palm presses all round the lip, the heel a third harder, so the ring modes round the hole feel almost none of it. Its flesh deadens the modes it lies on (600 N·s/m, weighted by each mode's φ² round the lip: a few milliseconds of ring), so a hard slap's belly "tok" stays 16 dB or more under its bass. Its force is spread over a patch wider than the clay's bending wavelength above about 600 Hz, so it doesn't radiate as a point force. | the critic's round-1 measurement: a point force rang the clay 11–20 dB over the bass |
| Gap | Under the palm, a radial channel from the opening's edge out to the palm's (6 cm over the hole, the neck's lip over the mouth). Inertance ρ·ln(b/a)/2π⟨g⟩, tapered by 1/(1 + (g/a)²) once the gap passes half the opening's radius. Resistance 6μ·ln(b/a)/π⟨g³⟩ plus the jet. The averages are over a lifting palm's tilt, a wedge from g₀ to g₀ + tilt. The gap closes smoothly over the last half millimetre (a softplus of the palm's height, 0.4 mm wide), to a 30 µm leak under a hand pressed firmly on (more when lightly). Plug, gap and palm are solved together, implicitly in the gap's resistance. | survey §2.5: ΔL = (a²/2h)·ln(b/a), f(both) 83/92/106/116 Hz at h = 1/2/5/10 mm; a leaky seal kills the tone [LHG19], [VALT]; a player "raises one side of the palm" [AHG13] |
| Shading | A resting hand spread across part of an opening leaves a crescent that conducts as (open share)^0.6 of the whole. Rayleigh's K = 2a goes with linear size; a thin crescent keeps a little more. Its losses come from its width (about the open share of the diameter): viscous layers through the wall, (ρ/S)·√(2νω)·(t/w + 2), and the jet at the finger's edge. Q falls from 46 open to 33 at 90 % covered. It sits in parallel with the gap under the hand, the pair combined as complex impedances at the frequency the air rings at, so a gap that is all resistance carries nothing. | survey §2.5: K ∝ φ^0.5…0.7 (inferred); "cover it completely, otherwise the sound will be short" [VALT] |
| Clay | Earthenware: ρ = 1900, E = 9 GPa, ν = 0.15, 9 mm thick, η = 0.015 (rising a little with frequency). The modes measured on an udu: the lip's "cowbell" at 556, 1108 and 1644 Hz (ring orders 2, 3, 4 round the neck); 970 and 1898 Hz round the hole; 1378 and 1470 Hz on the belly. The belly's higher modes continue a thin sphere's lower branch (membrane root plus bending) from that pair, past the 2.4 kHz ring frequency to n = 13, about 6 kHz. Modal masses come from the clay where each mode lives: the whole neck for the lip's family (its rings carry on down into the shoulder), a patch round the hole for its pair. Each belly mode stands for the real modes in its third of an octave, so it carries only their share of a quarter of the belly. A plate has (S/2)·√(ρh/B) modes per hertz, a shell below its ring frequency fewer, as f/f_ring. That makes the finger meet the shell's real driving-point mobility. | [AHG13] laser vibrometry; E 8–15 GPa, η 0.01–0.02, ρ 1850–1950 (survey §4.3); Cremer–Heckl modal density |
| Clay and air | The 14 low modes change the pot's volume by 3 % of their area over n and push the standing wave of their own order. The tail's many lobes cancel, so it doesn't. The cavity's stiffening is taken back out of each mode's spring. The shell's give lowers the bass by a negligible amount. | inferred (a sphere's harmonics change no volume; the pot's openings break that) |
| Fingertip | 20 g with a Hertzian pulp (K = 2e6 × 2^(1.5·tone)) that stiffens smoothly into the bone behind it (3e7, over 0.2 mm). It arrives at 0.25–2.5 m/s and touches the clay for 1.5 ms soft to 0.85 ms hard, solved against the clay's motion where it lands. The skin's ridges and the clay's grain make its force fret (±30 %), which rings the fine modes a smooth pulse would pass by. Above the ring frequency the clay's modes crowd too densely to ring one by one, so their sum is rendered as a noise as loud as the force that drives it, dying in 3.5 ms (η ≈ 2.5 % at 3–6 kHz), heard a little differently at each ear. A tap's 3–8 kHz sits 15–17 dB under its 1–3 kHz. Its force radiates as a point-forced plate's, a monopole of √2·F/ρh (Heckl), above kR = 1 (380 Hz), below which the pot is too small to radiate it. The skin's smack is noise as fast as the contact grows, 1–5 kHz, gone in 3 ms; a palm's soft rolling contact makes 0.2 of it. The skin rasps above 5 kHz. A firm tap sits 10–15 dB under a slap played as hard, the bass the loudest voice. | survey §6: finger 0.01–0.05 kg, contact 1–3 ms (inferred); Heckl's point-force radiation |
| Radiation | (A belly mode standing for its band's real modes radiates from their share of the area, as it carries their share of the mass, so the band's power comes out right.) Two microphones 50 cm in front, 35 cm apart. Each hears the flow out of the hole and the mouth after its own time of flight. It also hears each clay mode's equivalent volume velocity: a vibrating sphere's radiation efficiency for its order, σ = x^(2n+2)/((2n−1)!!²(n+1)² + x^(2n+2)), as √(4πσS)/k, times the mode's own pattern toward that microphone (normalised by its RMS over all directions). The air a palm squeezes out sideways is radiated as a dipole (its flow's rate × 8 cm/c). The jet hisses through a closing gap (turbulence ∝ u\|u\|, 3 kHz band). | [AHG13], survey §5: in-phase monopoles, the hole ≈ +9 dB over the mouth |
| Level | 0.56 of full scale per pascal at a metre, which makes an udu groove as loud as house's (the drums' loudness test) while a full-strength slap on the largest pot the keys make stays under the kit's per-hit ceiling. | the drums suite |

The pot costs well under a percent of a core while it sounds, and nothing
once it has rung 90 dB down. Hands don't add to it: one pot rings whatever
strikes it.

## How it was checked

`tests/udu_tests.cpp` checks the model against the survey's numbers:

- the measured frequencies and the conductance rule;
- the held, released and mouth-covered notes, and the octave glide (its
  range, direction and speed);
- continuous sliding and partial covering;
- tuning to the key;
- Q (open, held, part-covered), and the faster early fall of a hard slap;
- the pulse's pressure and its range;
- the clay's modes and losses, and where a finger finds them;
- contact time, and taps as a voice of their own (level, range, their "tk");
- a slap that is bass, not clank, in each ear, and the two ears alike;
- two fingers on the hole at once heard as one slap;
- stability under random hammering, and the cost.

A reviewer briefed to be hard to impress measured it over three rounds
(the user's cap), rendering strokes through the pot and the whole studio
and measuring them against the survey. By the third it judged the air
voice to meet the bar:

- the held note within 1.5 % of 68.6 Hz at every strength;
- the whoop +1160…+1180 c, two thirds of it in 13–15 ms;
- Q 47 open, about 50 held, 34 and 15 at 90 and 98 % covered;
- the release losing 1–3 dB;
- the deep tone 6 dB under the open one;
- clean stereo and mono, and flams.

It found the bass's narrow dynamic range (about 6 dB from vel 0.3 to 1)
to be the orifice jet's physics, not a defect. It had not signed off when
the rounds ran out. What it left open, and what was done after its last
round:

- **Taps' high end.** It measured a tap's third-octaves above 2 kHz
  falling in steps. The statistical sum of the crowded modes above the
  ring frequency was added after that round, and is not re-reviewed.
- **Taps against the bass.** At full pressure taps came out about as loud
  as the bass; it wanted them 4–8 dB under. The drums' loudness test,
  which weights above 100 Hz, needs the taps where they are for an udu
  groove to be as loud as house's. This one stays open.
- **The hard slap's belly "tok."** It was 11 dB under the bass, against
  the critic's target of ≤ −15. Fixed after the round: the flesh now
  deadens the clay it lies on, and the tok sits 16 dB under.
- **A resting finger.** It was too vocal laid on and lifted. Fixed after
  the round: it is now laid on gently and lifted slowly, about 17 dB under
  the softest slap.
- **A held roll that slides.** It resized the pot. Fixed after the round:
  on the udu a sliding roll no longer changes the pot's size.

`docs/udu-research.md` holds the literature survey the numbers come from,
with its sources.
