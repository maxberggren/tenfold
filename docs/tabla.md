# The tabla pair

The drums page's tabla pair kit is not a grid of samples. The whole pad
becomes the dayan and the bayan: two goatskin heads loaded with the syahi,
simulated as they are made (`src/tabla.h`) and drawn as they are played
(`src/drums.cpp`, `paintTabla`). It sits beside the older grid-based
`tabla` kit, which plays tabla bols as synthesised drums on the step grid
and is unchanged.

- The bayan, the large bass drum, is on the left; the dayan, the small
  treble drum, on the right, as the player sits at them.
- Seen from above: the dayan's kinar (its outer ring), open maidan and
  black syahi; the bayan's syahi off centre, away from the player.

## Playing it

- **Where you tap picks the stroke**, as on the real drums:
  - the dayan's kinar is **na**: the ring finger is already resting on the
    syahi's edge as the index finger strikes the kinar, so the fundamental
    dies and the 2nd and 3rd harmonics ring;
  - its maidan is **tin**, the same with the ring finger barely touching:
    the 2nd and 3rd strong, brighter than tun;
  - its syahi is **tun**: struck and left, the deep (0,1) rings;
  - past the bayan's syahi is **ge**, the bayan's deep tone, with the heel
    of the hand resting at its near edge;
  - the bayan's near part is **ka**, the hand slapped flat and left there.
  - A stroke that arrives without a place of its own (a loop recorded on
    another kit, say) is played where that stroke is played.
- **How hard:** pressure sets how fast the finger arrives (a na is flicked
  at up to 3.3 m/s; a tun is a finger let fall, 1.25 m/s). Harder is
  louder and brighter, and a hard ge starts a few cents sharp as the skin
  stretches.
- **A finger left down rests on the skin** and damps it: a tun held down is a
  closed stroke (te). Lift it and what is left rings on.
- **The bayan's heel:** a finger held on the bayan and slid away from you
  presses the heel into the skin. The tension rises and the ge glides up, an
  octave at most (a third of the pad's height). The bend shows above the
  bayan and on the finger.
- **Two hands:** strokes on both drums at once make the compound bols: dha is
  na with ge, dhin is tin with ge.
- **The rest of the page:** REPEAT rolls a stroke. A held hand doesn't groove
  on this kit: it is resting on the skin. Loops keep the kit, the resting
  fingers and the bends.
- **Tuning:** the pair follows the key. The dayan's Sa is the key's root near
  D4 (its (1,1) an octave above, as a player tunes it with na); the bayan's ge
  is an octave and a fifth below Sa.

## The model

Everything is in SI units, integrated sample by sample. The modes are not
chosen: each head's are solved for, once per process, from what it is made
of.

| Part | What it is | Set from |
|---|---|---|
| Heads | Goatskin, 0.25 kg/m² (0.28 on the bayan), E = 0.6 GPa, 0.22 mm thick. The dayan 14 cm across, the bayan 21 cm. | Rossing 1992 (0.2–0.3 kg/m²); head sizes from Patel & Iversen, Tiwari & Gupta |
| Kinar | A 15 mm ring of skin laid loose over the edge: 0.05 kg/m², a damper more than a load. | Courtney (13–19 mm); Raman ("a damper rather than a load") |
| Dayan's syahi | Over 54.5 % of the head's radius, laid on in seven layers, each a disc smaller than the last, from 4.69 kg/m² over the skin at the middle to 47 % of that at the edge: 15.3 g of paste. | Raman (stepped in 3, 5 or 7 stages); Rossing (9–15 g, "about ten times" the skin); Sathej & Adhikari (k = 0.45–0.55) |
| Bayan's syahi | Over 25 % of the radius, centred 18 % off the head's centre (away from the player), 9.9 kg/m² over the skin: 18 g. | Ramakrishna 1957 (k = 0.28, offset 0.18 b) |
| Modes | Rayleigh–Ritz over the bare membrane's own Bessel modes, J_m(j_mn·r/a)·{cos, sin} mθ for m ≤ 8, n ≤ 7: their stiffness T·κ² each, the mass matrix the skin's, the air's (2ρ/κ per mode, the evanescent air either side) and what the syahi and the kinar add. A centred syahi keeps each angular order apart; the bayan's, off centre, mixes them. 44 modes kept on the dayan (to 7 × Sa), 36 on the bayan. | — |
| The loading | The syahi's radius, peak, edge and taper were fitted so the dayan's modes land on Raman's groups, the shell's air included, and so each group's members lie together: (1,1) = 2, (0,2) 2.987 and (2,1) 2.994–2.998, (1,2) 3.985–3.989 and (3,1) 4.002–4.006, (0,3)/(4,1)/(2,2) 5.015–5.039, and (0,1) = 1.152 × Sa. The bayan's were fitted to Ramakrishna's measured dugga. | Tiwari & Gupta Table III (3rd −10…0 c, 4th ±16 c, 5th −9…+52 c, each group within 1 % of Sa; (0,1) 1.10–1.16 × Sa) |
| Degenerate pairs | No head is tensioned evenly: each pair of the dayan splits by 1–4 c, its nodal lines turned at random. | Raman (the (1,1) nodal line rocking), Courtney (the out-of-tune doublet) |
| Shells | Sealed: the air inside is a spring ρc²/V on the head's net volume, 0.7 L under the dayan and 5.5 L under the bayan. It couples every mode that sweeps a volume, so the drum's modes are the roots of a secular equation over the head's. It raises the dayan's (0,1) 9 % and leaves every mode with a nodal diameter alone. | Tiwari & Gupta, Bhat (only axisymmetric modes shift; no Helmholtz mode) |
| Tuning | The dayan's tension puts its (1,1) at 2 × Sa (9.0 kN/m at C#4: the published syahi mass needs a tight head). The bayan's puts its lowest mode, the syahi riding on the skin and the shell's air, on the ge (1.26 kN/m at C#2 + a fifth). Retuning while a head rings keeps its energy. | Sa = (1,1)/2 (Tiwari & Gupta); ghe 80–100 Hz, "a single strong harmonic" (Rohit & Rao) |
| Radiation | Each mode's far field in closed form (Jacobi–Anger and Lommel's integral over each Bessel term), tabulated against the wavenumber once, heard at two microphones over the player's shoulders: p = ρ/(2πr)·(Re F·ẍ − Im F·ω·ẋ). Its radiation damping R = ρω²/(4π²c)·∫|F|²dΩ. The monopole part acts on the whole head's net volume velocity (ρω₁²/2πc at the drum's lowest mode), so modes coupled through the air radiate together. | Rayleigh (baffled); the (0,1) a monopole, (1,1) a dipole (Sathej & Adhikari) |
| Losses | A loss factor per drum (0.0006 dayan, 0.02 bayan: a slack head and thick paste); the skin and kinar rubbing, whose loss factor grows as f² (8 s⁻¹ at 1 kHz, ∝ f³); and on the dayan air leaking through the lacing, 800 kg/(m⁴·s) on its net volume velocity. That gives T60 ≈ 1.4 s for tun's (0,1), 2.5 s for the (1,1), 0.4–1.1 s for the 3rd group, 0.4–0.5 s for the 4th, 0.15–0.3 s for the 5th and less above. | Fleischer (tun 1.5 s; na's 3rd 0.5–0.7 s, its (1,1) > 1 s); Raman (nothing above the 5th) |
| Skin stretch | Kirchhoff–Carrier: the area a swing adds, ½∫|∇w|², is a strain of ∫|∇w|²/(4A) each way in the plane, and the tension rises by E·h/(1−ν) times it (∫|∇φ_k|² = λ_k·M_k), so a hard stroke starts sharp and settles: a few cents on a hard ge. | — |
| Fingers | A Hertzian pulp with Hunt–Crossley loss, F = K·δ^1.5·(1 + λ·δ̇), solved against the skin under it and pulled back by the hand: the index finger's side for na (4 g, flicked at up to 3.3 m/s), its tip for tin (5 g), its pad for tun (5 g, K = 10⁶, let fall at 1.25 m/s), the fleshy middle fingers for ge (18 g, K = 10⁵), the palm for ka (50 g, 1.4 m/s). The arrival speed goes as the velocity. Each touches a patch of skin and drives each mode by its shape averaged over it. Contact lasts 1.5–7 ms, the palm 10 ms. The force under the finger also radiates directly (F/σ, above the modes). | contact times not published; 1–3 ms inferred for bounce strokes, 5–15 ms for the palm |
| Resting fingers | A spring and a dashpot on a patch of skin, solved implicitly in the skin's own velocity, so a hand only ever takes energy out. Na: the ring finger already resting as the index finger strikes the kinar (at 0.925 of the radius), 1.2 rad round from the stroke toward the player, at the syahi's edge (0.9 of its radius), 300 N/m and 20 N·s/m: the (0,1) dies, and of each pair the member with its node under the finger rings on, woken by the stroke as the sine of m times the angle between them. Tin: the same finger barely touching, 3 N·s/m. A finger left down (te): 3 kN/m and 30 N·s/m. Ka: the palm and fingers flat, 60 N·s/m, for 0.3 s or while held. The heel on the bayan: 70 % of the radius out on the near side, 2 kN/m and 8 N·s/m. | Fleischer (na: the ring finger enforces a nodal line, "only anti-symmetric vibrations evolve"); Raman (a harmonic isolated by touching a node and striking 90° away); Patel & Iversen (kat half-decay ≈ 8 ms, the dha balance) |
| The heel's bend | Pressed a depth δ over a broad patch, the skin takes a point-loaded membrane's logarithmic shape, ∫|∇w|² = 2πδ²/ln(R/r₀), and stretches. Pressed all the way the heel goes 20 mm deep, which brings the ge up an octave; the strain per semitone is solved against the shell's air at each tuning. It eases in over 30 ms. On the pad the depth goes as the square root of the slide, so the bend grows nearly evenly along it. | Rohit & Rao ("≈ an octave"); Patel & Iversen (the onset glide in dha, 213 ms) |

What it sounds like, measured on the model (Sa = 277 Hz):

| Stroke | Measured | Published |
|---|---|---|
| tun | (0,1) dominant; (1,1) −19, 3rd −19, 4th −23 dB under it; (0,1) T60 1.4 s; centroid ≈ 400 Hz | (0,1) dominant; (1,1) −20, 3rd −15, 4th −20 dB; T60 ≈ 1.5 s; centroid ≈ 295 Hz |
| na | (0,1) 11–14 dB under the 3rd at once and gone in 0.1 s; (1,1) 1–11 dB under the 3rd and long (T60 2.5 s); 3rd T60 0.55–0.65 s; 4th −11…−13, 5th −16…−27 dB (Sa 246–330) | (0,1) absent; 3rd strongest, T60 0.5–0.7 s; 4th −8, 5th −12 dB; the (1,1) "strong" (Courtney) or −20 dB (Fleischer) |
| dha | the ge 17–19.5 dB over the dayan's 2nd in the first 0.2 s, 9–11 dB after (Sa 246–330), where the page plays na and ge | +19 dB, then +11 (Patel & Iversen) |
| tin | 2nd and 3rd strong; centroid 1.7 × tun's | "strong second and third" (Courtney); 1.9 × (Patel & Iversen) |
| ge | 92 Hz dominant, T60 ≈ 1 s, half its level in 51 ms; weaker peaks near 3× and 4.7× | "a single strong harmonic close to 100 Hz", peaks near 290 and 450 Hz; half-decay 67 ± 18 ms |
| ka | half its level gone in 7 ms | ≈ 8 ms |

**Cost.** Each head is one set of modes stepped once per sample, about 80
between them, and each sleeps once it has rung 120 dB down (a finger still
resting on it stays, and damps it again when it is struck). Both heads
ringing with the ring finger and the heel down cost about half a percent of
a core, as the hang does. A head lets go of its upper modes once they are
100 dB down (a stroke wakes them again), and a resting finger damps only the
modes that would ring longer than a tenth of a second by themselves, so each
finger resting adds well under a tenth of a percent while the head rings. A retune is lookups and one secular
solve (well under a millisecond); the modes and far-field tables are built
once per process (about 60 ms).

## Deviations

- **The dayan's tension** comes out at 9 kN/m, where scaling Tiwari &
  Gupta's model gives 2.4–3.0 kN/m. Their fit carries a syahi of about 2 g;
  the published 9–15 g on a 14 cm head needs the tighter skin.
- **The shell's air** raises the dayan's (0,1) 9 % against Tiwari & Gupta's
  2.7 %. Theirs is a 10 cm head over a 0.79 L column; a 14 cm head over 0.7 L
  sweeps four times the volume against the same spring.
- **Na's (1,1)** is 1–11 dB under its 3rd, nearer Courtney's "strong
  second" than Fleischer's single plot (20–25 dB under); Patel & Iversen's
  dha balance holds with it.
- **Levels.** The pair is set so its groove sounds as loud as house's
  (the page's rule) with every full-strength stroke under 0.6 of full scale
  out of the drums page. Struck strokes have more crest than a ringing hang,
  so its peaks run about 6 dB over the hang's for the same loudness.
- **The attack above 3 kHz** is quiet: a fingertip's contact of 1.5–2 ms
  puts little force there, and nothing published measures a tabla stroke's
  first milliseconds up there. A nail or the kinar rattling on the skin
  would add it; neither is modelled.
- **The bayan's ratios** come within about 1.6 semitones of Ramakrishna's
  dugga, not exactly: the (0,2) is 164 c flat, the (2,1) 70–80 c sharp, and
  the (1,1) pair splits 0.3 % against the measured 5 %.
- **The ge is the bayan's lowest mode.** Tiwari & Gupta's radiation-only
  model damps it in 17 ms and suggest a (1,1)-type ge. Here the (1,1) is a
  dipole that barely radiates at 90 Hz, while the lowest mode is a monopole
  that does. Its partials at 1, 3 and 4.7 × match the measured ghe spectrum
  (100, 290, 450 Hz).
- **The octave bend** needs the heel 20 mm into the skin. A real heel also
  shortens the free head, a moving boundary these modes can't follow, so the
  stretch does all of it here.
- **Decays shorten as Sa rises**, as Tiwari & Gupta's do.

## How it was checked

A reviewer was briefed to be hard to impress. Over three rounds it rendered
strokes at several tunings, velocities and positions, and measured their
spectra, decays, envelopes, the dha balance, the heel's glide, stability
and cost against the published numbers, until it signed off. Besides the
deviations above, it left these open:

- In na's first 30 ms the (0,1) is still 7–12 dB under the 3rd (Fleischer:
  absent); it is gone within 0.1 s. Tin opens with a (0,1) 5–9 dB over its
  3rd that dies in about 0.15 s.
- Na's 4th is 11–16 dB under the 3rd early, against Fleischer's 8.
- With eight or more fingers resting across both drums at once the pair
  costs 1–2 % of a core.

`docs/tabla-research.md` holds the literature survey these numbers come
from, with its sources. `tests/tabla_tests.cpp` checks the model against
those numbers.
