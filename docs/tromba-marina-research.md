# The tromba marina: what is known

The survey behind `src/trombamarina.h`. `docs/tromba-marina.md` says
which number in the model comes from which line here. Each value is marked
**pub** (published, with its source) or **inf** (inferred: my estimate, from
the physics or from neighbouring instruments, where nothing is published).

## The instrument

| What | Value | Mark | Source |
|---|---|---|---|
| Names | Trumscheit, tromba marina, trompette marine, Nonnengeige ("nuns' fiddle"). "Trumbe" (trump, also drum) + "Scheit" (stick) | pub | [PG05] after [AD91] |
| Era | In common use 1511–1619 (Trumscheit); a 1480s shorter variant in Memling's *Angel Musicians*. Concertos by Jean-Baptiste Prin, who also wrote its *Traité* and *Méthode*; parts by Lully, Scarlatti and Gletle | pub | [PG05], [BHP16] |
| Length overall | 4 to 7 ft, varying widely. Museum examples: 166.4 cm, with a 101.4 cm soundbox (Met); 216.6 cm, with a 132.6 cm soundbox; 191 cm (V&A, about 1700). Praetorius gives 7 ft | pub | [WIKI], [MET], [GAC], [VA] |
| Body | A long, slightly flared, quasi-trapezoidal or triangular box of three boards, the soundboard the widest | pub | [BHP16], [WIKI] |
| Strings | One bowed string, sometimes two; some later instruments have dozens of sympathetic wire strings inside (the V&A's: one thick catgut string and 41 wires) | pub | [VA], [BHP16] |
| String material | Gut. A replica uses 1.30 mm natural gut | pub | [BHP16] |
| Open string | A Bologna instrument has the partials marked on its neck for a whole string of **D1** (37 Hz). Long instruments reach their high harmonics more surely, short ones less | pub | [PG05] |
| String length | Not published for the instruments above. A soundbox of 1.0–1.3 m and a neck above it give about **1.5–1.8 m** | inf | from [MET], [GAC] |

## How it is played

| What | Value | Mark | Source |
|---|---|---|---|
| Natural harmonics only | The left hand touches the string lightly at a node ("harmoniques effleurées"), and the string sounds that harmonic of the open string. Stopping it instead loses loudness, makes vibrato impossible and, above all, loses the brass character | pub | [PG05] |
| Where the bow goes | Standing, the player bows the **upper** part of the string (near the nut), with the left hand producing the harmonics **under** the bowing point | pub | [PG05], [BHP16] |
| Why harmonics keep the buzz | With harmonics, the pitch changes without changing what drives the bridge. A pressed note would need the bridge readjusted for each note | pub | [PG05] |
| The thumb as a reflector | At a node, displacement and velocity are zero. The finger exerts no force there and acts as a mirror for the wave, with a negative reflection for velocity and force | pub | [PG05] |
| Energy into the bridge | With a harmonic, the energy the string sends to the bridge is about **n times lower** than for a pressed note at the same pitch | pub | [PG05] |
| Range | Harmonics up to about the 16th in practice. On the Bologna neck the marked scale runs through the 3rd octave of the open string | pub/inf | [PG05] (the 16th as the usual top is inf) |

## The trembling bridge

| What | Value | Mark | Source |
|---|---|---|---|
| Shape | A "shoe". The string passes over one foot, and the other is free and rattles against the soundboard, on a plate of ivory or glass | pub | [BHP16], [WIKI] |
| Mechanism | A mass–spring system driven by the bowed string. Its motion is free in one direction and stopped by the soundboard in the other: a nonlinear oscillator, as on the hurdy-gurdy's trompette | pub | [PG05] |
| Gap | The **guidon** sets the rest gap between the free foot and the soundboard, **around 20 µm**. Players keep two or more "good adjustments" and change them to vary the timbre, even during a piece | pub | [PG05] |
| Polarisation | Measured both horizontally and vertically (their Fig. 4): bridge displacement and velocity, with table acceleration. The bow drives the string parallel to the soundboard, which rocks the bridge about its loaded foot | pub / inf | [PG05] (the rocking about the loaded foot is inf, from the geometry) |
| What makes it a trumpet | **Phase ruptures**, discontinuities in the signal synchronised with the foot's knocks during the slip part of the string's motion, play the part that shock waves play in a loud trumpet. A good adjustment shows them every period | pub | [PG05] |
| Period doubling | The knocking can double its period and lead toward chaos: the classical scenario | pub | [PG05] Fig. 6 |
| What matters most | Physical models agree: the rattling bridge colliding with the body is the greatest contributor to the trumpet-like sound | pub | [WSBD20] abstract |
| Hard to adjust | On a replica, players found the bridge hard to set and to keep buzzing reliably | pub | [BHP16] |
| Bridge mass, stiffness, lever | **Not published** | — | — |

## The bowed string

| What | Value | Mark | Source |
|---|---|---|---|
| Helmholtz motion | One corner circulates. The bow sticks for the share (1 − β) of the period and slips for β, where β is the bow's distance from the end as a share of the length | pub | [MSW83], [WG04] |
| Schelleng's limits | Most force F_max = 2Z·v_b / ((μs − μd)·β); least force F_min ∝ Z²·v_b / (2Rβ²). Above F_max the string sticks too long ("raucous"); below F_min there are extra slips (a "surface sound") | pub | [Sch73] |
| Friction of rosin | A hyperbola in sliding speed is the usual fit: μ = μd + (μs − μd)·v0/(v0 + Δv), with μs ≈ 0.8, μd ≈ 0.3 and v0 ≈ 0.1 m/s. Rosin is really thermal (a softening melt), and the hyperbola ignores that | pub | [SW00], [WG04] |
| Solving it | Where the string's impedance line meets the friction curve, with hysteresis: stay stuck while possible, stay sliding while a sliding solution exists | pub | [MSW83] |
| Attack | Clean attacks need the bow's force and acceleration in a band (Guettler's diagram) | pub | [Gue02] |
| Flattening | A heavy bow lowers the pitch a little, more near F_max | pub | [MSW83], [WG04] |
| Bowed flageolets | Model the finger as a point damper: a wave-digital resistor on a waveguide, or a contact in a finite-difference model. A rigid stop is wrong: it uncouples the two parts and gives the wrong tone | pub | [Pak05], [DB15], [FIE] |

## Contact and collision

| What | Value | Mark | Source |
|---|---|---|---|
| Hunt–Crossley contact | F = K·δ^α·(1 + λ·δ̇) for penetration δ > 0, else 0: Hertzian stiffness with damping that vanishes at first touch, so there is no force step | pub | [HC75] |
| Collisions in musical instruments | Power-law barriers with energy-stable schemes. The tromba marina and the snare are worked examples | pub | [BTC15], [DBWS21] |
| Buzzing bridges elsewhere | The sitar's and tanpura's curved bridges: a distributed contact that sweeps a formant. The tromba marina's is a separate rigid body knocking, closer to the hurdy-gurdy's trompette | pub | [Val95], [PG05] |

## The wood

| What | Value | Mark | Source |
|---|---|---|---|
| Spruce, along the grain | E_L ≈ 10–12 GPa, ρ ≈ 400–450 kg/m³ | pub | [HAI79], [WH] |
| Spruce, across it | E_T ≈ 0.6–0.9 GPa, G_LT ≈ 0.6–0.8 GPa, ν_LT ≈ 0.37 | pub | [HAI79], [WH] |
| Loss | Material loss factor 0.007–0.01 (Q ≈ 100–140). Built plates with glue joints and radiation have Q of about 20–60 | pub/inf | [HAI79]; plate Q inf |
| Gut | ρ ≈ 1300 kg/m³, E ≈ 4–6 GPa | pub | [FR98] |
| Gut string decay | Several seconds at low frequency on rigid ends, much less at kilohertz frequencies. No tromba-marina measurement | inf | — |

## Not found

- A published spectrum or bridge-force measurement with numbers.
  [PG05] shows figures but gives no values, and the full [WSBD20] paper
  (17 MB) could not be fetched here; only its abstract was read.
- The bridge's mass, rocking stiffness and lever.
- The soundboard's modes or the box's air resonances.

## Sources

- [AD91] C. Adkins, A. Dickinson, *A Trumpet by Any Other Name: A History of the Trumpet Marine*, 2 vols., Buren: Knuf, 1991. This is the "Adams/Adkins" organology; I read it here only through [PG05].
- [PG05] A. Padilla, V. Gibiat, "The 'trumpet marine', an instrument between a trumpet and a violin", Forum Acusticum, Budapest, 2005, pp. 385–389.
- [WSBD20] S. Willemsen, S. Serafin, S. Bilbao, M. Ducceschi, "Real-time implementation of a physical model of the tromba marina", Proc. SMC 2020, doi:10.5281/zenodo.3898741 (abstract only).
- [DBWS21] M. Ducceschi, S. Bilbao, S. Willemsen, S. Serafin, "Linearly-implicit schemes for collisions in musical acoustics based on energy quadratisation", J. Acoust. Soc. Am. 149 (2021).
- [BHP16] A. Baldwin, T. Hammer, E. Pechiulis, P. Williams, D. Overholt, S. Serafin, "Tromba Moderna: a digitally augmented medieval instrument", NIME 2016.
- [MSW83] M. E. McIntyre, R. T. Schumacher, J. Woodhouse, "On the oscillations of musical instruments", J. Acoust. Soc. Am. 74 (1983) 1325–1345.
- [Sch73] J. C. Schelleng, "The bowed string and the player", J. Acoust. Soc. Am. 53 (1973) 26–41.
- [SW00] J. H. Smith, J. Woodhouse, "The tribology of rosin", J. Mech. Phys. Solids 48 (2000) 1633–1681.
- [WG04] J. Woodhouse, P. M. Galluzzo, "The bowed string as we know it today", Acta Acustica united with Acustica 90 (2004) 579–589.
- [Gue02] K. Guettler, "On the creation of the Helmholtz motion in bowed strings", Acta Acustica united with Acustica 88 (2002) 970–985.
- [Pak05] J. Pakarinen, "Physical modeling of flageolet tones in string instruments", EUSIPCO 2005.
- [DB15] C. Desvages, S. Bilbao, "Two-polarisation finite difference model of bowed strings with nonlinear contact and friction forces", DAFx-15.
- [FIE] "Mathematical models of flageolet harmonics on stringed instruments" (abstract read): a Dirichlet condition at the finger gives the wrong tone.
- [HC75] K. H. Hunt, F. R. E. Crossley, "Coefficient of restitution interpreted as damping in vibroimpact", J. Appl. Mech. 42 (1975) 440–445.
- [BTC15] S. Bilbao, A. Torin, V. Chatziioannou, "Numerical modeling of collisions in musical instruments", Acta Acustica united with Acustica 101 (2015) 155–173.
- [Val95] C. Valette, "The mechanics of vibrating strings", in *Mechanics of Musical Instruments*, Springer, 1995.
- [HAI79] D. W. Haines, "On musical instrument wood", Catgut Acoust. Soc. Newsletter 31 (1979).
- [WH] *Wood Handbook*, USDA Forest Products Laboratory.
- [FR98] N. H. Fletcher, T. D. Rossing, *The Physics of Musical Instruments*, 2nd ed., Springer, 1998.
- [WIKI] "Tromba marina", Wikipedia. [MET], [GAC], [VA]: the Metropolitan Museum, Google Arts & Culture and Victoria and Albert Museum catalogue entries for their trumpets marine.
