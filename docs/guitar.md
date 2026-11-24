# The guitar

The Lead page's **Guitar** (rock) is an overdriven electric guitar,
simulated as it is built (`src/guitar.h`) and played as a lead
(`src/lead.cpp`, `guitarPick` and the Guitar block of `renderVoice`).

It is a Les Paul–style guitar strung 10–46. Its bridge humbucker feeds a
Marshall JCM800 2204 turned up, into a closed 4x12 of Vintage-30-type
speakers, close-mic'd.

Every voice of the line, melody and harmony, is a guitar of its own (a
twin-guitar harmony). Each of those players has one rig, an amp and a cab,
that all their notes go through. A released note's tail and the next
note's attack meet in the same valves, and the supply's sag and the grids'
charge carry from note to note.

## Playing it

- **Where you play:** the neck picks the note. A note goes on the string
  that puts it nearest the seventh fret, where lead lines live.
- **How hard:** the press as the finger lands is the pick's force, from a
  quarter of a newton to two. The pad's touch, which lands as a velocity
  between 0.6 and 1 for every sound, is spread over the pick's whole range;
  the keys pick at 0.375, or fully when accented. A harder stroke moves the string further,
  digs toward the body, releases quicker and scrapes. Into the cranked amp:
  - a hard stroke is a few decibels louder while it sounds, and holds up
    for seconds;
  - a light one cleans up and fades, and chimes rather than growls.

  The page adds no swell of its own: the guitar's dynamics are the amp's.
- **How high:** where along the string you pick. Low on the pad is over
  the neck pickup (16 cm off the saddle); high is 4 cm from the bridge,
  thinner and brighter, the string moved less for the same force.
- **A flatter finger:** a palm resting on the strings by the bridge. The
  note becomes a muted chug: a thump that holds for some 70 ms and dies
  within a few tenths, its top gone in tens of milliseconds. The palm dulls
  the pick too.
- **A second finger:** a hammer-on. No new pick: the string rings on at its
  new fret at once, tapped there by the finger. Lifting the second finger
  is a pull-off, which plucks the string sideways back to the first note.
- **Sliding:** the finger crosses the frets. More than half a semitone from
  where it rests, the pitch moves a fret at a time, the string snapping
  onto each.
- **Bends:** the twist and the bend wheel bend the string. They raise its
  tension, its pitch (as √T) and, a little, its stiffness and its place
  over the pickup's poles. Bent down, it is a tremolo arm let back.
- **Vibrato:** a held note's vibrato comes late (0.6 s), at about 5.8 a
  second, and only ever above the note, as a finger's is: it is a bend
  rocked to and fro, lingering at the top of each push.
  - About half a semitone peak to peak at an ordinary press; leaning in
    widens it to some 70 cents and no further, since its middle stands
    above the note and a wider one is heard sharp.
  - No two cycles are alike: each has its own speed (±7 %) and width
    (±18 %).
- **Holding:** the amp's sustain carries the note. Pressing hard while
  held (past 0.7) moves the player toward the amp, over most of a second,
  from 3 m to 0.6 m, where they sway a little. The note then feeds back,
  coming and going with the sway: held up by its own sound through the guitar's
  body, on the note or its octave (now and then its twelfth or double
  octave). A palm on the strings or the fretting hand lifting stops it.
- **Lifting:** the fretting hand mutes the string, in about 0.12 s.
- **Tone keys (− =):** the amp's treble.

## The model

Sources are in `docs/guitar-research.md`. **inf** marks a value I
estimated where nothing is published; **calc** one worked out from
published inputs.

### The string

| Part | What it is | Set from |
|---|---|---|
| Strings | 10–46 (D'Addario EXL110) on a 628 mm scale. Tensions: 67.9, 64.5, 69.5, 77.1, 81.7 and 73.3 N, D'Addario's 25.5" figures times (24.75/25.5)². Wound strings' stiffness from cores of 0.29, 0.39 and 0.48 mm (**inf**) | [DAD] (confirmed from the unit weights) |
| Stiffness | B = π³E·d⁴/(64·T·L²), E = 200 GPa. Open strings: E4 1.5e-5, B3 4.5e-5, G3 1.2e-4. B rises with the fret as 1/L² and falls with a bend as 1/T | [F64], [PLF14]; tested |
| Dispersion | M first-order allpasses (M = 4, 8 or 16 by how much stiffness the note has), one coefficient fitted at start-up by least squares, weighted as 1/ω, to the stiff string's phase delay τ(ω) ≈ τ0 − κω² (κ = B·P³/8π²) over the partials up to the twentieth (or 4.6 kHz). Measured: every string within ±9 cents of Fletcher's law to the 20th partial (the low E ±4.5); tested to the 8th on the plain G | [VDS94], [RV06] |
| Losses | One-pole loop filter g(1 + a)/(1 + a·z⁻¹) set from a T60 at the fundamental and one at 3 kHz [VHKJ96]. Open strings, fundamental: 18, 16.5, 15, 13.5, 12, 10.5 s (E2 to E4); at 3 kHz: 2.2, 2.3, 2.4, 3.6, 3.8, 4.1 s (wound strings lose their top sooner). Fretted: times √(L/L₀). All **inf**, under [PLF14]'s isolated G3 (22 s at the fundamental, 3.7 s at the 10th partial, **calc**), since the neck takes some. Through the amp a medium A3 is some 12 dB down at 2 s, a light one 18 | [PLF14], [FZ99] |
| Polarisations | Two loops: across the fretboard and toward the pickup. The second, which is heard and so what the string is tuned by, is on the note; the first 0.15 % flat. The second decays in 0.85 of the first's time. The bridge passes energy between them, a rotation of 1.2 rad/s. All **inf** (no published split for the electric guitar) | [BZE20] (the mechanism), [PLF14] (the pickup hears the second) |
| Tension modulation | ΔT/T = (EA/T)·⟨u̇²⟩/c², from the loop's own waves (both polarisations) smoothed over 20 ms; f = f₀·√(1 + ΔT/T). A mezzo-forte stroke rings 1–3 cents sharp | [TVK00], [B09] |

### The pick, the fret and the finger

| Part | What it is | Set from |
|---|---|---|
| Pick | A force F = 0.25 + 1.75·vel N at x off the saddle (16 cm to 4 cm by the finger's height) leaves the string δ = F·x(L − x)/(T·L) aside: 2–2.5 mm for a hard stroke mid-string. The shape is a triangle with its apex there, fed into each loop over one round trip | [KVT98], [EE10]; force **inf** |
| Its angle | From 14° to 40° off the fretboard as the stroke hardens: a hard stroke digs toward the body | **inf** |
| Its release | The plectrum's edge rounds the corner: a two-pole low-pass at 3 + 9·vel² kHz | **inf** |
| A pick on a ringing string | Catches it over 1.5 ms, taking 85 % of its swing, then lets the new shape go | **inf** |
| Scrape | The plectrum's edge on the string as it leaves: a 1.5 ms burst of noise band-passed at 3.5 kHz into the string's motion, 0.25·vel² mm | **inf** |
| Fret | L = L₀·2^(−fret/12) on the string chosen for the note (nearest the 7th fret, never under the open string, 22 frets) | |
| Slides | More than half a semitone from the resting note the fretted pitch moves in whole frets, relative to where the finger rests, so it lands on the note in any tuning | |
| Hammer-on | The string's length changes at once (the lead's glide for this sound is 4 ms). A tap at the new fret, 0.12 + 0.35·vel mm toward the board | **inf** |
| Pull-off | A pluck by the finger at the fret, sideways, 0.2 + 0.6·vel mm | **inf** |
| Bend | Tension (2^(semitones/6)); the string pushed sideways at the fret by δ = √(2ΔL/(1/a + 1/b)) (ΔL the stretch the tension needs, a and b the lengths either side of the fret), at the pickup times d/L, eased over 15 ms (a step would ring the coil) | **calc** |
| Vibrato | Above the note only: 2·0.35·shape·(1 − (1 − w)^1.6) semitones, w = (1 − cos)/2, shape 0.3 + 0.7·lean, so about 50 cents peak to peak at an ordinary press, 80 leaning in; 5.8 Hz; each cycle's rate ±7 % and width ±18 % at random, changing at its foot; from 0.6 s | **inf**; [ROD14], [M04] |
| Palm mute | Fundamental T60 down to 0.8 s, 3 kHz to 0.05 s (smoothly with the flat finger, from the first sample for a note picked under it); the pick's release 60 % lower | **inf** |
| Release | The hand: fundamental T60 0.12 s, the top 0.036 s, over 8 ms | **inf** |

### The pickup

| Part | What it is | Set from |
|---|---|---|
| Place | Humbucker, its middle 42 mm from the saddle, the coils 18 mm apart (**inf**: the Les Paul's positions and the coil spacing were not found published; a Strat's bridge pickup is at 41 mm [T02]). Each coil reads the string's displacement where it sits; both are read at one instant. Tested: notches at the partials L/d̄ predicts | [KVT98], [T02], [PPV12] |
| Field | Novak et al.'s measured curve for a Seymour Duncan SH-2N, Φ(x) = (x + Leq)/∛(req² + (x + Leq)²) − x/∛(req² + x²), Leq 9.86 mm, req 1.34 mm, of the distance x from the string to the pole: x² = (2.5 − y)² + (0.8·(0.6 + z))² mm² (y the swing toward the pole, z sideways; 0.6 mm off the poles' axis; the sideways weight 0.8 **inf**, so the horizontal swing is heard some 15 dB under the vertical, and mostly doubled). Tabulated against x². EMF = dΦ/dt | [N18], [HM09], [PPV12] |
| Level | 7e-5 V·s per unit of Φ (scaled to fall a unit per mm where the string rests): a hard stroke peaks at 0.2–0.6 V, a light one at 20–110 mV | **inf**, within what is quoted for PAF-type humbuckers |
| Resonance | 4.4 H, 8.2 kΩ, 130 pF, 4.5 m of cable at 100 pF/m, into 200 kΩ (500k volume ∥ 500k tone ∥ 1M input): 3.1 kHz, Q ≈ 1.7. Bilinear, warped to the peak | [MOJ], [SOS], [LEM]; self-C **inf** |

### The amp

| Part | What it is | Set from |
|---|---|---|
| Triodes | Koren's 12AX7 (µ 100, ex 1.4, kg1 1060, kp 600, kvb 300, RGI 2 kΩ), each stage's plate current solved on its load line once at start-up, tabulated against its drive w. Tested: the plates rest at 196 / 239 / 155 V and the cathodes at 1.62 / 2.5 / 0.9 V, against the drawing's 192 / 230 / 150 and 1.6 / 2.4 / 0.9 | [K96], [MAR81] |
| Input stage | 256 V node, 100k plate, 2k7 cathode bypassed by 0.68 µF (its bias follows the average current: τ 1.8 ms), 68k + the pickup's 8k to the grid, which conducts when positive | [MAR81] |
| To the volume | 470k ∥ 470 pF into the 1M pot; the pot at 0.75 (an audio taper, (10^(2x) − 1)/99: 31 % passes), up as a lead player has it, its upper part bypassed by 1 nF (bright): a(1 + sτ)/(1 + a·sτ), τ = R_upper·1 nF | [MAR81], [TL] |
| Cold stage | 264 V, 100k plate, 10k unbypassed (inside the table: its own feedback); 22 nF coupling, 1M; grid current through 470k. Cuts off long before its grid conducts: even harmonics. Tested | [MAR81], [ROB] |
| To the third | 470k ∥ 470 pF into 470k to ground and the next grid's 65 pF | [MAR81] |
| Third stage | 264 V, 100k plate, 820 Ω unbypassed; 22 nF coupling, 470k; grid current through 560k. Its grid conducts on every peak: its coupling capacitor charges, the bias moves down (blocking) and recovers in a few tenths of a second. Tested | [MAR81], [PY09] |
| Input capacitances | Cgk + (1 + A)·Cgp (1.6 and 1.7 pF) against each source: 22, 30 and 25 kHz | **calc** |
| Cathode follower | Follows, until the swing up drives its grid past its cathode: above 25 V the top squashes, x' = 25 + e/(1 + e/30) | **inf** as to the numbers; the mechanism [ROB] |
| Tone stack | Marshall values (470 pF, 22 nF, 22 nF; 220k, 1M, 25k, 33k), treble 0.6 (± 0.1 per tone step), middle 0.55, bass 0.45. Third order from the circuit's nodal determinants, coefficients by four evaluations on a circle in the s-plane, then bilinear (in double precision). Tested against a direct solution of the circuit: within 0.2 dB | [YS06], [MAR81] |
| Presence | The negative feedback's lift: +7 dB from 2.5 kHz, at noon. The feedback loop itself is not simulated | **inf** |
| Master and phase inverter | The master a 1M audio-taper pot at 0.35 (4.2 % passes), down as a lead player has it; the inverter's gain 18, saturating at ±70 V (tanh). From 30 mV to 1 V of pickup voltage the output rises by under 4 dB | **inf** |
| EL34s | Koren's pentode form, a commonly circulated EL34 fit (µ 11, ex 1.35, kg1 650, kp 60), screen at the supply. Biased for 36 mA each at 460 V: −40 V (the drawing: −42 V for the 100 W). Each driven through 22 nF and 220k; its grid conducts through the inverter's 40k once past its cathode, charging the capacitor (blocking). Tabulated per supply voltage, 360–500 V in eight rows | [K96], [MAR81], [AMPB]; the fit **inf** as to source |
| Plates | Each half of a 3.2 kΩ primary sees a quarter of it; the swing saturates (tanh) toward a 100 V knee | [AMPB]; knee **inf** |
| Supply | 488 V unloaded, 90 Ω source (the windings and the choke), 100 µF; the valves and screens draw 1.15× their plate current. Idle about 480 V; played hard it sags to about 446 V (7 %, the 5–8 % a solid-state 2204 is expected to). Tested: 15–100 V, recovering within a second | [AMPB], [A99] |
| Transformer | 3.2k to 16 Ω (14.1:1), primary inductance a high-pass at 32 Hz, leakage a low-pass at 19 kHz | **inf** |

### The cab and the feedback

| Part | What it is | Set from |
|---|---|---|
| Speaker and box | High-pass at 70 Hz (Fs 75 Hz in a closed box), the impedance peak's lift +3.5 dB at 110 Hz, a dip of 3 dB at 450 Hz; the cone's break-up: +1.5 dB at 1.2 kHz, −2.5 at 1.9, +5 at 2.6, +3 at 3.4, −6 at 4.2, +2 at 4.9; then a sixth-order fall from 5 kHz (about −15 dB under 1 kHz at 7 kHz, −25 at 9, −35 at 12) | [CEL] (range 70–5000 Hz; the peaks **inf** as read off its graph) |
| Mic | Folded into the above (close-mic'd off the cap's edge) | [SHU] |
| Feedback | The mic'd sound, 8.7 ms to 1.7 ms later (3 m to 0.6 m as the player leans in, swaying a third of a metre every five seconds), pushes on the guitar's body and neck: a fourth-order low-pass at 600 Hz, their low modes (+12 dB at 180 Hz, +9 at 420, Q 3, **inf**), a second-order low-pass at 400 Hz, then integrated (a force moves the string as its integral; leaky under 25 Hz) into both polarisations (toward the board fully, along it at 0.35), at 2.4e-4 × (how near)², nearness easing in over 0.8 s. Scaled by (1 − palm)² and by the hand's lift. The amp's saturation sets the level it holds the string at. The push enters at the saddle; the pickup hears it only once it has gone round the string (the wave's reflection there is read without it). Tested: at the nearest, held still with no vibrato, the string still swings within 25 dB six seconds on, and over 60 % of what the pickup and the mic hear is in the first three partials | [S90]; strengths **inf** |
| Level | A fortieth of the speaker's volts, times 0.85 × 0.25 on the page, whatever the touch (as loud as the other leads) | |

## Keeping it in real time

- **Oversampling.** The amp runs at four times the rate. Two polyphase
  half-band stages carry it there and back: 47 taps, 80 dB down, then 19.
  Every nonlinearity is antialiased to first order by its antiderivative
  ([PZL16]): the triodes, the follower, the inverter, the EL34s (per supply
  row) and the plates' knee. Each is a table built at start-up, so nothing
  transcendental runs per sample.
- **What it leaves.** With a full-scale sine through amp and cab, what lands
  between the harmonics is −63 dB at 330 Hz, −53 dB at 667 Hz and −48 dB
  at 1.33 kHz. Each doubling of the rate buys about 10 dB at twice the cost
  (the test checks 16× does better than 4×).
- **Cost.** A rig (amp and cab) costs about 2.5–3 % of a core; a string
  and its pickup about a quarter of a per cent. A line has one rig per
  voice of it (the melody and each harmony), however many notes ring
  through it, and a rig left alone sleeps once it has rung down. A
  released note is muted and sleeps within about half a second.
- **Built once.** The tables (three triodes, four curves, eight EL34 rows,
  the pickup's flux, the dispersion fits) are built when the Lead's engine
  starts, in about 0.1 s.

## How it was checked

`tests/guitar_tests.cpp`:

- Fletcher's B for the plain strings;
- every note on its pitch from E2 to E6;
- the partials' stretch on the plain G;
- the pickup's comb notches, moving with the fret;
- the amp's resting voltages against Marshall's drawing;
- the cold stage's lopsided clipping;
- even harmonics and compression;
- the supply's sag and the grid's blocking, and their recovery;
- the tone stack against the circuit;
- aliasing at 4× against 16×;
- feedback sustaining a held note on its first partials, at the pickup and
  at the mic, and a palm stopping it;
- the touch heard in level and in timbre, and reached from the pad;
- stability under hard playing;
- on the page: selected and saved by name, in tune, muted on release, the
  hammer-on, the palm mute;
- the budget, alone and in a run.

## The review

A critic briefed to be hard to impress, an acoustician who has built
guitar and amp models, judged it in three rounds, measuring the model
directly and the app's renders. What it found, and what was done:

- **Round 1.**
  - Feedback whistled at 2–3 kHz on every held note: the return path rose
    with frequency. It is now a force through the body, integrated, with
    low-passes.
  - The amp was saturated by everything, so picking had no dynamics; and
    the bright-cap formula was wrong.
  - The cab was 10 dB too bright from 6 to 10 kHz.
  - The dispersion fit was 3–5 times too stretched on the wound low strings.
  - Each note had its own amp, reset at the pick: now one rig per player.
  - The pickup's nonlinearity was too strong: now Novak's measured curve.
  - The sag was twice too deep, and the vibrato too narrow and too regular.
- **Round 2.**
  - Touch was still inaudible at the mic: the master's taper was not an
    audio pot's, and a pick contact and scrape were missing.
  - Sustain was too short.
  - The whistle was latent with no vibrato: the pickup heard the
    feedback's push before it had gone round the string.
  - The leaned vibrato sat a quarter tone sharp.
- **Round 3.**
  - The pad's velocity spans only 0.6–1, so the touch was out of reach:
    now spread over the pick's range.
  - A4 at full lean sat in a dead spot of the loop's phase: now the player
    leans in from 3 m and sways.
  - The palm mute was a tick, not a chug.
  - A bend let go stepped the flux.

Signed off in the end: the string, the pick, the pickup, the amp, the
cab, aliasing and the cost. Feedback was "close" and playing not signed off
before round 3's fixes, which there was no fourth round to judge.

## Open points

- **Plain strings saturate in the first second.** E4 and A4 picked lightly
  and hard sustain within about a decibel of each other for their first
  second (A3 3 dB apart). The touch is heard in how long a note holds up
  (11 dB apart at 2 s on E4) and in its attack, not in its first second.
  That is a cranked amp's nature, but a player may want more.
- **Feedback comes and goes.** Held at full lean, every note holds within
  about 6 dB for three seconds. After that, as the player sways, some dip
  by 10 dB and come back.
- **Bright upper notes.** At the mic, A4's fundamental stands some 20 dB
  under its strongest harmonic, and held notes centre near 2–2.5 kHz. It
  comes from the chain after the clipping: the stack's mid scoop, the
  presence lift (+7 dB, standing in for the feedback loop that is not
  simulated) and the speaker's 2.6 kHz peak. It wants checking against a
  re-amped recording before retuning.
- **Pick position at the mic.** Picking near the bridge is brighter at the
  pickup (about 15 %), but not at the mic, where the saturated amp evens
  it out.
- **Not simulated:**
  - the amp's hiss and hum, and the string and pick noise between notes;
  - the power amp's negative feedback loop (folded into the presence
    shelf);
  - the output transformer's saturation;
  - the screen grids' own supply;
  - the coils' width (the two coils stand for the aperture);
  - the guitar's other strings ringing in sympathy.
- **Aliasing.** At 4× with antiderivative antialiasing, a full-scale sine
  leaves −48 to −63 dB between its harmonics. In real notes this is masked:
  4× and 16× measure the same. Each doubling buys about 10 dB at twice the
  amp's cost.
- **Block size.** The output depends slightly on the block size (block-rate
  parameter updates): about −27 dB of waveform difference between blocks of
  16 and 256, inaudible.
- **Estimates.** Many constants are **inf** (above): the decay times, the
  polarisations' split, the pick's force and angle, the feedback's
  strengths, the cab's peaks, the coil spacing and the Les Paul's pickup
  position. They are what measurements of a particular guitar would pin
  down.
