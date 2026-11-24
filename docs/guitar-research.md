# The overdriven guitar: research

What the Lead's Guitar (`src/guitar.h`) is built from: an electric guitar
played through a cranked valve amp, as published acoustics, circuit analysis
and data sheets describe it, part by part, from the string to the
microphone. `docs/guitar.md` maps each of the model's constants to one of
these. **inf** marks a value I had to estimate where nothing is published;
**calc** one I worked out from published inputs.

The chain:

    string → pick → fret/finger → pickup → cable → preamp → tone stack
          → phase inverter → power valves → output transformer → speaker
          → cabinet → microphone
                ↑______________ air and body (feedback) ______________|

## 1. The steel string

### Waves, losses, the single delay loop

A plucked string is two travelling waves reflected at the nut and the
bridge. Karjalainen, Välimäki & Tolonen [KVT98] reduce the two to a single
delay loop (SDL) holding one round trip, with the losses and the
frequency-dependent effects lumped into filters at one end. A pickup or a
pluck at a point becomes a comb: a point d from the bridge sees the wave
leaving the bridge twice, d/c apart in each direction, so its response is
|sin(nπd/L)| at the n-th partial.

The losses of a solid-body guitar's string are the string's own (air
friction, which dominates on plain strings, and internal friction, which
dominates on wound ones) and its coupling to the neck. Paté, Le Carrou &
Fabre [PLF14] measured them on an electric guitar and found:

- the bridge's conductance much smaller than the neck's (the bridge is
  nearly rigid, following Fleischer);
- the magnetic pickup adds no measurable damping;
- dead spots are specific to one polarisation.

Their damping model, with their G3 data, gives an isolated string's T60 of
about 22 s at its fundamental and 3.7 s at its tenth partial (**calc**).
Mounted on a guitar, the values are lower, and much lower at a dead spot.
Fleischer & Zwicker [FZ99] show decay time falling as the neck's
conductance rises. No paper I could read gives a table of T60s for
electric-guitar notes, so the model's are **inf**, bracketed by [PLF14].

Välimäki et al. [VHKJ96] fit the loop filter as a one-pole low-pass,
H(z) = g(1 + a)/(1 + a·z⁻¹), from a note's decay at its fundamental and
its upper partials. The model does the same, from a T60 at the fundamental
and one at 3 kHz.

### Stiffness and inharmonicity

A stiff string's partials are stretched:

    f_n = n·f0·√(1 + B·n²),   B = π³·E·d⁴ / (64·T·L²)

This is Fletcher's [F64], for a solid wire of diameter d. Steel has
E = 2×10¹¹ Pa [PLF14].

With D'Addario's published tensions for an EXL110 set (10–46) at 25.5"
[DAD] (16.2, 15.4, 16.6, 18.4, 19.5 and 17.5 lb, confirmed against their
unit weights), the plain strings at 24.75" have:

| String | B |
|---|---|
| E4 | 1.5×10⁻⁵ |
| B3 | 4.5×10⁻⁵ |
| G3 | 1.2×10⁻⁴ |

All are **calc** ([R] gives 1.3×10⁻⁵, 4×10⁻⁵ and 1.1×10⁻⁴ at 25.5"). The
plain G is the stiffest string on the guitar, which is why it is
notoriously hard to intonate.

B rises as the square of the fretted shortening, 1/L², so at the 12th fret
it is four times the open string's. Murray & Whitfield [MW22] measured B
on guitar strings and found Fletcher's formula holds well for plain
strings and "rather poor[ly]" for wound ones. D'Addario publishes no core
sizes for its hex-cored wound strings. The model takes a wound string's
stiffness from a core of 0.29, 0.39 and 0.48 mm (**inf**, the usual
40–45 % of the gauge).

The stretch is synthesised with first-order allpasses in cascade [VDS94];
[RV06] and [AVS10] tune such cascades to a target B. Järveläinen, Välimäki
& Karjalainen [JVK01] show inharmonicity is easier to hear the lower the
note.

### Two polarisations

A string swings in two planes: parallel to the fretboard and toward it.
They are tuned a hair apart and lose energy at different rates. Coupling
at the support (torsion at the saddle, in Brauchler, Ziegler & Eberhard's
two-vibrometer measurement [BZE20]) trades energy between them, so the
partials beat and decay in two stages. [PLF14] found the pickup far more
sensitive to the swing toward it. The size of the split and the coupling
rate are **not published for the electric guitar**; the model's (0.15 %,
1.2 rad/s, the vertical decaying in 0.55 of the horizontal's time) are
**inf**.

### Tension modulation

A string swung wide is stretched by its own swing, so its pitch rises with
its amplitude and falls back as it dies [TVK00]. The tension rises by
ΔT = EA·⟨y′²⟩/2 (⟨y′²⟩ the mean square slope).

Bank [B09] calls large glides "not typical" of the electric guitar. The
research notes estimate a few cents on a plain string and up to about 10
on a hard pluck of a wound bass string (**calc**). The model computes the
stretch from the loop's own waves.

## 2. The pick

A plectrum pushes the string aside with a force and lets it go. The string
is left in a triangle with its apex at the pick, whose spectrum is
sin(nπβ)/n², β the pick's place along the string [KVT98, VHKJ96]. Held by
a force F at x from the bridge, the string gives δ = F·x(L − x)/(T·L): a
pick near the bridge moves the string less, and the stroke is thinner and
brighter.

Evangelista & Eckerholm [EE10] model the pick's contact and release. The
plectrum's edge and how fast it slips off round the triangle's corner,
more for a softer stroke. Pick forces of a quarter to two newtons are
**inf**, consistent with deflections of a fraction of a millimetre to about
2.5 mm. That is as far as a string can go before it buzzes on the frets.

## 3. The fret and the finger

The fret stops the string: L = L₀·2^(−fret/12). A slide crosses the frets
one by one, and the string snaps onto each in turn. A hammer-on drives the
string onto the next fret at once and taps it there. A pull-off plucks the
string sideways as the finger leaves it.

A bend pushes the string across the fretboard. It stretches the string and
raises its tension, f ∝ √T, so a whole tone needs 12 % more tension. The
added tension also lowers B (∝ 1/T). A finger's vibrato is a bend rocked
to and fro: it can only raise the tension, so it swings above the note. A
tremolo arm can lower it.

No measurement of electric-guitar finger vibrato was found. The closest is
a study of classical guitar (rates 1–5.4 Hz, [ROD14]). Moelants [M04]
gives 6.4 Hz for a medium vibrato across instruments. The model's 5.8 Hz
and up to about 0.35 semitone are **inf**.

A bend also moves the string sideways over the pickup's poles. The
deflection at the fret follows from the stretch the bend needs (**calc**).
At the pickup it is that deflection times d/L.

## 4. The pickup

### Where it listens

A pickup at d from the saddle hears the string's displacement there: a
comb, |sin(nπd/L)| [KVT98, T02, PPV12]. A humbucker's two coils sit about
18 mm apart (**inf**: the coil spacing was not found published). Their sum
is

    sin(nπd₁/L) + sin(nπd₂/L) = 2·sin(nπd̄/L)·cos(nπΔ/2L)

so its notches fall at the multiples of L/d̄ from the middle, and at odd
multiples of L/Δ from the spacing.

Tillman [T02] gives a Strat's pickups at 41, 98 and 162 mm from the bridge
(25.5"). No published figure was found for the Les Paul's. The model's
bridge humbucker at 42 mm (centre) is **inf**, by the same geometry.

Each coil also averages over its width. Tillman models a single coil as
about an inch wide; [PPV12] call this the pickup's sensing width.

### Its magnetic nonlinearity

The steel string sits in the field of the pole pieces. The flux it
modulates is not linear in its displacement: it grows faster as the string
nears the pole.

- Horton & Moore [HM09] model the magnet as charged disks and the string as
  small magnets. The vertical swing dominates; the horizontal gives a
  smaller, frequency-doubled signal.
- Paiva, Pakarinen & Välimäki [PPV12] and Novak et al. [N18] confirm the
  structure: a static curve Φ(x), then a derivative (Faraday). Novak's
  curves are independent of frequency and rest distance, and show more than
  20 harmonics for a humbucker at 3 mm swung ±2 mm. The amount falls
  steeply with distance.

The model uses the point-pole law Φ ∝ 1/r², with r² = (h − y)² + (x/w)²,
h the string's height over the pole (2.5 mm, setup guides give 1.6–4.7 mm
[N18 notes]). w is the field's lateral width (7.5 mm, **inf**), broader
than one pole because a humbucker's row of them is.

### Its electrical resonance

The coil is a source behind its inductance L and resistance R. It sits
across its own capacitance and the cable's, loaded by the volume and tone
pots and the amp's input:

    H(s) = 1/(L·C·s² + (L/R_load + R·C)·s + 1 + R/R_load)

The values found:

- A PAF-type humbucker has 2.8–4.6 H [MOJ].
- Seymour Duncan's '59 measures 7.43 kΩ (neck) and 8.13 kΩ (bridge).
- Cables run about 100 pF/m, between 50 and 200 [SOS].
- Lemme puts the loaded resonance of most pickups at 2–5 kHz with normal
  cables [LEM].

With 4.4 H, 8.2 kΩ, 130 pF of its own (**inf**: self-capacitance was not
found published) and 4.5 m of cable, the resonance is 3.1 kHz with a Q of
about 1.7 (**calc**).

## 5. The amp

### Which amp

The overdriven rock lead is, archetypally, a humbucker into a cranked
Marshall. The model follows Marshall's JCM800 2204 (50 W, master volume).
Its preamp is the 2203's, read from Marshall's 1981 drawing [MAR81] with
Robinette's [ROB] and Tone-Lizard's [TL] accounts:

1. **Input stage.** 100k plate, 2k7 cathode bypassed by 0.68 µF, 68k grid
   stopper, 1M grid leak.
2. **To the volume pot.** 22 nF, then 470k ∥ 470 pF into the 1M "preamp
   volume" pot, which has a 1 nF bright cap.
3. **The cold clipper.** 100k plate, a 10k cathode resistor, unbypassed.
4. **To the third stage.** 22 nF, then 470k ∥ 470 pF, with 470k to ground.
5. **Third stage.** 100k plate, 820 Ω cathode resistor, unbypassed.
6. **Cathode follower.** 100k cathode resistor, direct-coupled from the
   third stage; it drives the tone stack.
7. **Tone stack.** Treble 220k–250k, bass 1M, mid 25k, slope 33k (other
   amps: up to 56k), C1 470 pF, C2 and C3 22 nF.
8. **Master volume, then the power amp.** A long-tailed-pair phase
   inverter (82k/100k plates), 22 nF couplings to 220k bias feeds, 5k6 grid
   stoppers, and two EL34s into a 3.2 kΩ plate-to-plate output transformer
   [AMPB].

The drawing's table of preamp voltages (MV, 50 W) checks a model of the
valves on these load lines:

| Stage | Plate | Cathode |
|---|---|---|
| Input | 192 V | 1.6 V |
| Cold | 230 V | 2.4 V |
| Third | 150 V | 0.9 V |

The supply nodes are 256–264 V.

### The triode

Koren's model [K96] is

    E1 = (Vpk/kp)·ln(1 + exp(kp·(1/µ + Vgk/√(kvb + Vpk²))))
    Ip = (E1^ex/kg1)·(1 + sgn E1)

For the 12AX7, µ = 100, ex = 1.4, kg1 = 1060, kp = 600, kvb = 300. Its grid
conducts through about 2 kΩ (RGI). Pakarinen & Yeh's review [PY09] surveys
how such stages are simulated: static curves and tables, wave digital
filters, nodal methods. Yeh's thesis [Y09] and [YS08] cover the
nodal-K/DK approach and wave digital filters.

Two dynamic effects make a valve amp's distortion move with how it is
played [PY09, A06]:

- **Blocking distortion.** A grid driven past its cathode draws current,
  which charges its coupling capacitor. That shifts the bias down, so the
  stage clips differently, even cuts off, until the charge leaks away
  through the grid resistor.
- **Bias creep.** A bypassed cathode's capacitor charges with the average
  current, so its bias also follows the level history.

Asymmetric clipping is what makes even harmonics. The 2203's second stage
is biased cold (10k unbypassed): it cuts off on one side long before its
grid conducts on the other [ROB].

### The tone stack

Yeh & Smith [YS06] derived the Fender/Marshall stack's third-order
transfer function, H(s) = (b1·s + b2·s² + b3·s³)/(1 + a1·s + a2·s² +
a3·s³). Their coefficients are polynomials in the six parts and the three
pots (t, m, l). Their values are the Bassman's (250 pF, 20 nF, 20 nF;
250k, 1M, 25k, 56k); the Marshall's above come from the schematics, not
from the paper. The model computes the coefficients from the circuit's own
nodal equations (§ docs/guitar.md), which a test checks against a direct
solution of the circuit.

### The power amp and its supply

The JCM800s rectify with silicon, not a GZ34 [ROB, MAR81]. The 50 W's
supply:

- 345-0-345 VAC gives 488 V unloaded [AMPB].
- 100 µF either side of a 40 H choke [AMPB].
- About 460 V reaches the plates [AMPB]; one 1984 2203 measured 451 V at
  36 mA a valve.
- Ampbooks reckons 60–80 V of sag at full power [AMPB].

Aiken [A99] puts a power transformer's B+ winding at 50–300 Ω, and an
output transformer's primary at 80–200 Ω. Koren publishes no EL34
parameters; the model uses the pentode form of [K96] with a commonly
circulated EL34 fit (µ = 11, ex = 1.35, kg1 = 650, kp = 60, kvb = 24,
**inf** as to source). Biased for 36 mA at 460 V it sits at −40 V,
against the drawing's −42 V for the 100 W.

A push-pull pair's plates each see a quarter of the plate-to-plate load.
A pentode is a current source down to its knee, about 100 V at these
currents, where the plate swing saturates.

## 6. The speaker and the cab

The Celestion Vintage 30 [CEL]:

- resonance 75 Hz;
- range 70–5000 Hz;
- 100 dB sensitivity.

The G12M Greenback has the same Fs and range. Their published response
graphs show the cone's break-up as peaks and dips between about 1.5 and
5 kHz, and a steep fall beyond. The individual peak frequencies are only
in the graphs (**inf** as read off them).

A Shure SM57 close up adds proximity bass (+6 to +10 dB at 100 Hz) and a
presence rise [SHU]. In a closed-back box the speaker's resonance rises
and is damped by the air behind it (**inf**: no measured 4x12 impedance
was found). A valve amp's high output impedance lets the speaker's
impedance peak at resonance raise the bass there.

## 7. Feedback and sustain

At stage volume the speaker's sound reaches the guitar and drives its
strings, through the body and the air. Sullivan [S90] modelled this as the
amp's output fed back into the string loop through a gain and a delay. The
delay decides which partials the loop favours. Where the loop gain passes
one, the note sustains as a howl, and the amp's saturation sets its level.

Sustain at high gain is the same mechanism without the howl. The amp
compresses the decaying string so its output holds while the string falls
by tens of decibels.

## Sources

- [A99] R. Aiken, "What is 'sag'?", aikenamps.com.
- [A06] R. Aiken, on blocking distortion, aikenamps.com (cited by [PY09]).
- [AMPB] Ampbooks, "Marshall JCM800 2204 power amp" (calculated design values).
- [AVS10] J. Abel, V. Välimäki, J. O. Smith, "Robust, efficient design of allpass filters for dispersive string sound synthesis", IEEE Signal Processing Letters 17(4), 2010.
- [B09] B. Bank, "Energy-based synthesis of tension modulation in strings", DAFx-09.
- [BZE20] A. Brauchler, P. Ziegler, P. Eberhard, Acta Acustica 4, 2020, doi 10.1051/aacus/2020008: both polarisations of a plucked guitar string measured with two vibrometers; the coupling attributed to the string's torsion at the saddle (abstract read).
- [CEL] Celestion, Vintage 30 and G12M Greenback data sheets.
- [DAD] D'Addario, string tension chart and catalogue (EXL110: 103.6 lb total).
- [EE10] G. Evangelista, F. Eckerholm, "Player–instrument interaction models for digital waveguide synthesis of guitar: touch and collisions", IEEE TASLP 18(4), 2010.
- [F64] H. Fletcher, "Normal vibration frequencies of a stiff piano string", JASA 36(1), 203–209, 1964.
- [FZ99] H. Fleischer, T. Zwicker, "Investigating dead spots of electric guitars", Acustica 85(1), 1999.
- [HM09] N. G. Horton, T. R. Moore, "Modeling the magnetic pickup of an electric guitar", Am. J. Phys. 77(2), 144–150, 2009.
- [JVK01] H. Järveläinen, V. Välimäki, M. Karjalainen, "Audibility of the timbral effects of inharmonicity in stringed instrument tones", ARLO 2(3), 2001.
- [K96] N. Koren, "Improved vacuum tube models for SPICE simulations", Glass Audio 8(5), 1996.
- [KVT98] M. Karjalainen, V. Välimäki, T. Tolonen, "Plucked-string models: from the Karplus–Strong algorithm to digital waveguides and beyond", Computer Music Journal 22(3), 1998.
- [LEM] H. Lemme, pickup measuring techniques (planetz.com).
- [M04] D. Moelants, 2004, vibrato rates across instruments, as cited by [ROD14] (not read).
- [MAR81] Marshall, JCM800 Lead Series 50 W & 100 W preamp and power circuit drawings, 24-4-81.
- [MOJ] Mojotone, "Spec check"; Reverb, pickup primer (PAF inductance ranges).
- [MW22] R. Murray, S. Whitfield, "Inharmonicity in plucked guitar strings", Am. J. Phys. 90(7), 487–493, 2022.
- [N18] A. Novak, B. Lihoreau, P. Lotton, E. Brasseur, L. Simon, "Experimental study of guitar pickup nonlinearity", DAFx-18.
- [PLF14] A. Paté, J.-L. Le Carrou, B. Fabre, "Predicting the decay time of solid body electric guitar tones", JASA 135(5), 3045–3055, 2014.
- [PPV12] R. C. D. Paiva, J. Pakarinen, V. Välimäki, "Acoustics and modeling of pickups", JAES 60(10), 768–782, 2012.
- [PY09] J. Pakarinen, D. T. Yeh, "A review of digital techniques for modeling vacuum-tube guitar amplifiers", Computer Music Journal 33(2), 85–100, 2009.
- [PZL16] J. Parker, V. Zavalishin, E. Le Bivic, "Reducing the aliasing of nonlinear waveshaping using continuous-time convolution", DAFx-16 (antiderivative antialiasing).
- [R] My research notes' arithmetic at 25.5".
- [ROB] R. Robinette, "How the Marshall JCM800 works".
- [ROD14] M. A. Rodrigues, "O vibrato no violão", MSc thesis, UFMG, 2014.
- [RV06] J. Rauhala, V. Välimäki, "Tunable dispersion filter design for piano synthesis", IEEE Signal Processing Letters 13(5), 2006.
- [S90] C. R. Sullivan, "Extending the Karplus–Strong algorithm to synthesize electric guitar timbres with distortion and feedback", Computer Music Journal 14(3), 26–37, 1990.
- [SHU] Shure, SM57 user guide.
- [SOS] Sound On Sound, "Choosing guitar cables" (~30 pF/ft).
- [T02] R. Tillman, "Response effects of guitar pickup position and width", till.com.
- [TL] Tone-Lizard, "The ultimate JCM800".
- [TVK00] T. Tolonen, V. Välimäki, M. Karjalainen, "Modeling of tension modulation nonlinearity in plucked strings", IEEE TSAP 8(3), 300–310, 2000.
- [VDS94] S. Van Duyne, J. O. Smith, "A simplified approach to modeling dispersion caused by stiffness in strings and plates", ICMC 1994.
- [VHKJ96] V. Välimäki, J. Huopaniemi, M. Karjalainen, Z. Jánosy, "Physical modeling of plucked string instruments with application to real-time sound synthesis", JAES 44(5), 1996.
- [Y09] D. T. Yeh, "Digital implementation of musical distortion circuits by analysis and simulation", PhD thesis, Stanford, 2009.
- [YS06] D. T. Yeh, J. O. Smith, "Discretization of the '59 Fender Bassman tone stack", DAFx-06.
- [YS08] D. T. Yeh, J. O. Smith, "Simulating guitar distortion circuits using wave digital and nonlinear state-space formulations", DAFx-08.
