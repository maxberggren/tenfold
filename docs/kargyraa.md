# Kargyraa: the Tuvan growl

KARGYRAA is the Vocal page's last style. You sing or speak low, and what
comes back is your voice made the way a kargyraa singer's larynx makes it:
- a growl an octave under the note you sing;
- over it, a melody made of that growl's own harmonics, steered by your
  vowel.

It is not an effect on your voice. The voice is made again
(`src/kargyraa.h`): your throat is replaced and your vowel is kept. The
physiology behind it is in `docs/kargyraa-research.md`. This page maps
each constant to what it comes from, and says how it was checked.

## Playing it

- **Choosing it:** ← → to KARGYRAA (after RIOT). Songs keep it by name
  (`"style": "kargyraa"`).
- **Singing:** sing a held note, low.
  - **Your pitch.** The vocal folds take your note, moved by octaves to
    the one nearest 140 Hz, a kargyraa singer's. The undertone sounds an
    octave under that.
  - **Your phrase.** A phrase keeps its octave unless its note leaves
    80–230 Hz. A man's voice usually stays where it is, and a woman's
    goes down one octave.
  - **Your vowel.** Move it (oo, oh, aw, ah: open the jaw, round the lips)
    and the first resonance steps from one harmonic of the undertone to the
    next. That is the melody, as in kargyraa.
- **Fingers:** a lane held steers the melody instead. The resonance goes
  to the harmonic of the undertone nearest the lane's note, folded into
  the 6th to 11th harmonic, with F2 an octave above it. The highest finger
  wins.
- **B (saved as `kargyraaRatio`):**
  - **2:1** (the default): the false folds close every second cycle;
  - **3:1**: they close every third cycle, an octave and a fifth under
    you, as some singers do.
- **Consonants:** a hiss or a burst is let through as you made it,
  whenever no vowel is sounding.
- **Recording:** Space loops what you hear.

## The model

Everything below runs at the studio's rate, sample by sample.

### The larynx (`Larynx`)

The larynx is two slits in series, in SI units:
- the glottis, area Ag(t);
- the false folds, area As(t).

The lungs' pressure P is spent on two things:
- Bernoulli losses through both slits, ½ρU²(1/Ag² + 1/As²);
- accelerating the air above them, L·dU/dt.

This is solved implicitly for U each sample, as the positive root of a
quadratic. The lips radiate dU/dt.

| Constant | Value | Set from |
|---|---|---|
| Register | a phrase starts in the octave of its note nearest 140 Hz (99–198 Hz); inside a phrase the octave moves only if the note leaves 80–230 Hz, so a note near an edge, or a vibrato across one, never flips | Lindestad 2001: 70 Hz against 140 modal. Castellengo & Henrich 2014: 61–92 Hz in eleven recordings. Fuks 1998: an octave from 49 Hz |
| Fold length | 1.4 cm | Sakakibara 2002's model, l_g |
| Glottal peak area `kAmax` | 0.10 cm², chink 0.005 cm² | a strong pulse at the full voice's 2.2 kPa peaks at about 520 mL/s through the inertance. Fuks: 460 mL/s in this mode, 280 modal |
| Open quotient, skew | 0.45, opening 70 % of it (Rosenberg; modal is about 62 %). Each wanders slowly, ±6 % (sd) over 0.3 s, and only ±0.5 % cycle to cycle | pressed (Lindestad: "slightly pressed"; Fuks: closed phase 1.50 ms against 1.63, and the flow's decreasing phase faster in this mode). A rigid pulse puts its spectral zeros on the same harmonics at every pitch, and those sank melody harmonics (review, round 1). Varied fast cycle to cycle, the voice turned rough (round 2). The contact cycle's open quotient comes out higher by itself (below) |
| Return phase | the closing area, once down to 5 % of its widest, falls exponentially over 0.05 ms | the LF model's Ta, short for a pressed voice |
| Lungs `kPmin`–`kPmax` | 1.4 → 2.4 kPa with your level | Fuks: 21.9 cmH₂O (2.15 kPa) in this mode against 18 modal, "consistently higher" |
| Inertance | ρ · 3 cm / 0.7 cm², the epilarynx and ventricle | Rothenberg's skewing; the length allows for the lowered larynx (Esling 2002) |
| False folds at rest | 0.5 cm², wide open | not engaged in modal voice |
| False folds' widest, vibrating `kVenPeak` | 0.15 cm², 1.5 × the glottis | Bailly 2014, throat-singing case: h_venf/h_vof 0.9–2 |
| Their motion `kVenUnder` | an approach every glottal cycle, plus a deeper one at the undertone, weighted 0.15 / 0.85 | Bailly 2010: they approach every cycle and touch only every second |
| Contact | tight: 0.002 cm² plus a few thousandths more, varying from closure to closure (their opening ±4 %). It starts in the weak cycle's open phase (the lock centres it at 0.42 of that cycle) and ends at that cycle's glottal closure, where they start to open | Lindestad 2001: "complete but short" closures. Bailly 2010 (p. 3217): contact starts in the open phase and stops during the closing phase, and "the ventricular opening is initiated at the time of glottal closure". No contact quotient is published; here it is at most 22 % of their cycle |
| Lock | an Adler phase oscillator: dφ/dt = f + K·sin 2π(φ_lock − φ), K = 0.25 of its rate | the 1:2 entrainment of two self-sustained oscillators (Bailly 2014 gives the false folds their own resonance, about 70 Hz, Q ≈ 3) |
| Their natural rate | the singer's tension: f0/R, followed over 0.3 s, set off it by ±3 % (sd) at each phrase | so the phase of the lock differs from phrase to phrase and slips at a leap. It is not their free resonance: a fixed 70 Hz could not lock across the 49–95 Hz kargyraa is sung over, which the singer's tension evidently can |
| Back pressure `kBackPressure` | during contact the folds' open phase runs at 1 − 0.47 × its speed, the 0.47 wandering slowly, ±5 % (sd) over 0.3 s | Bailly 2010: the contact cycle is 1.19 × the other, its open phase 1.5 ms longer, its open quotient +11 %; four consecutive cycles apart by a steady 39–42 Hz. Their model (ventricle at the lungs' pressure, so no glottal drop) does not produce the lengthening, so it is set to the measurement. Measured here: 19 % longer. Varied more, or from closure to closure, the half-harmonics' timing scatters and the top turns to noise (round 2: HNR 2–3 dB over 2 kHz) |
| The pair's period | held at exactly R × the sung period | so the undertone is the sung note's half, in tune with the music |
| The weak pulse | the series flow, not a constant | measured here: 0.28 of the strong (−11 dB). Fuks −13 dB; Lindestad less |
| Onset | each phrase starts rough: for 50–300 ms (different each phrase) the false folds approach on every cycle without touching, jitter and shimmer × 5; then the doubling comes in over about 40 ms | Castellengo & Henrich 2014: a song starts on a plain pitch and switches to the doubling. Bailly 2010: a rough, same-period stage before it |
| Jitter, shimmer | 0.2 %, 0.025 neper (0.2 dB) per cycle. With the false folds' own variation, the undertone's period jitter comes to 0.44 % | none published. Lindestad: "sonorous"; Grawunder 1999: "more uniform" than pulse register; Fuks: "clear harmonic partials up to about 4 kHz". At 0.5 % the 20th harmonic's phase is already scrambled |
| Turbulence | (Re² − Re_c²) above Re_c 2700, at both jets; the false folds' jet counts as far as they are the narrower. Scale 1.5·10⁻⁶ per Re² | Meyer-Eppler 1953; Flanagan & Cherry 1969. None is published for kargyraa (heard as "hoarse"). This is 0.75 × RIOT's. Harmonics-to-noise: 38, 26, 13 and 8.5 dB at 0–1, 1–2, 2–3 and 3–4 kHz (mean of five pitches, /a/ and /o/) |

### The tract

There are twelve Klatt resonators in cascade, each with unity gain at DC,
so the formants come out at their right relative levels by themselves.

| Constant | Value | Set from |
|---|---|---|
| F1, F2 | yours. Found every 8 ms by twelve-pole linear prediction at 8 kHz on 32 ms; the lag window widens each pole by a third of your pitch. F1 is the first peak under 1.1 kHz. F2 is chosen from the peaks and shoulders (where F2 merges into F1's skirt, as on /a/) 150 Hz to 2.8 kHz over F1: the one nearest the F2 already followed, so F3 is not taken for F2. The median of three readings is then followed over two hops | |
| Normalised | × 0.96 at your own octave, −0.08 per octave moved down (a woman's tract to a man's) | |
| F1's range | 300–950 Hz | Castellengo & Henrich: 320–920 Hz |
| On harmonics | F1 and F2 each on their nearest harmonic of the undertone. F1 is held until another harmonic is 0.75 of a harmonic nearer, F2 until one is 1 nearer (its estimate wanders a few tens of hertz on a steady vowel). They glide 15 ms between harmonics; a phrase's first vowel is placed at once | Castellengo & Henrich: kargyraa's melody is F1's (or F1 and F2), on H6–H12 |
| A finger | its note folded into 5.7–11.4 × the undertone (so a harmonic is never on the band's edge), F1 on that harmonic (H6–H11), F2 = 2·F1 | the singers' "formant octave" on /o ɔ ɑ a/ (Castellengo & Henrich) |
| Bandwidths | B1 25, B2 45 Hz | none measured for kargyraa. This is a stylisation, and it is the overtone singer's: closed-glottis F1 bandwidths in speech are 40 Hz or more, but Klingholz measured 20 Hz for a formant tuned to a harmonic in overtone singing, and kargyraa is sung that way. At 25 Hz the tuned harmonic stands 5.5–16 dB over its neighbours across H6–H11. At 45 Hz H6, H7 and H10 sank to 0–4 dB |
| F3–F12 | 2.35, 3.15, 3.75 kHz (a man's), then 4.5–10.5 kHz a kilohertz apart, at 110–900 Hz | an 18 cm tube's formants. Five poles alone lose the gain the poles above them add at the top (Klatt's higher-pole correction), and left no energy above 4 kHz (review, round 1) |

### Around it

- **Level.** The growl's own power and yours are each read over the same
  40 ms window, both started when the growl starts, and the growl's is
  held to yours. So the vowel, the melody harmonic, the doubling coming in
  and the pressure change its colour, not its level.
  - A phrase starts on the gain the last one settled on, and the gain never
    rises past twice that, so the growl's first milliseconds are not lifted
    to meet a level they have not reached.
  - This is a loudness follower, by design.
  - Two earlier versions failed. A model of the tract's gain (round 2)
    missed by up to 6 dB, vowel by vowel, as the doubling came in. A
    follower quicker on your side than on the growl's (round 3) made every
    attack burst by 7–8 dB.
- **A guard.** Over 20 ms the output is held to at most 6 dB over your
  level, and to 2 dB over it in a phrase's first 150 ms, while your vowel
  is still being found. On a steady voice it is never reached. It stops a pure tone or a
  buzz from ringing far over what went in. Peaks can still stand several
  times over the level: a kargyraa has one strong pulse every two periods,
  rung by narrow resonances, so its crest factor is high. The page's
  limiter takes the rest.
- **Consonants.** Your own high band (over 1.8 kHz) passes while no vowel
  sounds and most of your energy is over 2 kHz.
- **The page.** Its instruments follow the undertone, the pitch that is
  heard.

## How it was checked

The checks use the test singer (`tests/kargyraa_tests.cpp`: a darkened
saw through three formants, with vibrato and breath) and the page's real
front end (`tests/vocal_tests.cpp`).

| Check | Result | Against |
|---|---|---|
| Undertone | C3 sung, undertone 65.4 Hz. Half-harmonics −1.7 dB against their neighbours (the singer: −43); the undertone's own partial −6 dB against the sung note's | inferred about −4 dB at the source. Lindestad: subharmonics up to 1 kHz |
| Alternate pulses | weak/strong 0.27 (−11 dB); the weak cycle 19 % longer | Fuks −13 dB; Lindestad less; Bailly 1.19 |
| Closure | the false folds under 0.01 cm² in 78 of 78 of their cycles, reopening at glottal closure | Lindestad: complete closures; Bailly: opening at glottal closure |
| Register | 220 Hz sung gives a 55 Hz undertone; G2 (98 Hz) with vibrato stays within 2 % (no octave flips) | |
| 3:1 | third-harmonics present, 4 dB under the harmonics | Fuks, Sakakibara |
| Vowel steering | /u/: F1 on H5 (325 Hz), 16 dB over its neighbours. /a/: H11 (714 Hz), 16 dB over | |
| A finger, each of H6–H11 | 8.3, 5.5, 12.5, 16.0, 7.2 and 14.0 dB over the louder neighbour | |
| Regularity | undertone period jitter 0.44 %; harmonics-to-noise 38 / 26 / 13 / 8.5 dB at 0–1 / 1–2 / 2–3 / 3–4 kHz | Fuks: clear partials to 4 kHz; Lindestad: "sonorous" |
| The top, against modal | against the same engine without the false folds and at modal pressure (formants on its own harmonics), 2–4 kHz is +8 dB on /a/ and /o/ and +15 on /u/; 4–6 kHz is +5, +5 and +9 | Fuks: more energy at 2–6 kHz than modal, on [a] |
| Level | on a steady vowel within 0.3 dB of the singer's; 100 ms levels within ±1 dB; on the page 0.1 dB from the natural voice | |
| Onset | within 6 dB of steady 39 ms after the vowel starts (the tracker's first readings, then the rough start). An attack's loudest 25 ms is −0.8 to +2.9 dB against the steady part's loudest | |
| Silence, loud input | silent; a full-scale buzz, square, noise, tone and clicks are all finite and held to 6 dB over the input's level | |
| Cost | 0.36 % of a core (best of three, 10 s, on a loaded machine); 0.53 % in the harness | |
| Sanitizers | the engine and the page's kargyraa tests under `-fsanitize=address,undefined`, gcc and clang: clean | |

## The review

A reviewer was briefed to be hard to impress, and to sign off only if
WOWed, as a voice scientist who knows the throat-singing literature and as
an audio-DSP engineer. It judged:
- the physics: the subharmonic ratio and its phase, the alternate pulses,
  the formant placement, the jitter;
- the realism: renders compared with the published spectra and waveforms
  (no recording was downloaded);
- the cost and the latency.

It measured everything with its own tools over three rounds, the most the
work was given.

**Round one** found:
- octave flips near the register's edge;
- no energy above 4 kHz (five poles, nothing above);
- F3 taken for F2 on /a/;
- fixed spectral holes in a rigid pulse, sinking melody harmonics;
- level overshoot and pumping;
- false folds that never quite closed, and closed past the glottis's
  closure;
- clockwork alternation;
- a "lock" that was really a gate.

All of these were fixed as described above.

**Round two** confirmed those fixes. It found the cure for the clockwork
had overshot: varied fast, from cycle to cycle, the voice turned rough,
with harmonics-to-noise 2–9 dB and the undertone's jitter at 4 %. It also
found:
- the return phase doubling the area for a sample at every closure;
- contact sometimes outlasting the glottal closure;
- the kargyraa duller than modal voice at 2–6 kHz, where Fuks measured it
  brighter.

The variation is now slow, the return and the contact are fixed, and the
closing phase is quicker, as Fuks measured. Against a fair modal reference
the kargyraa is now brighter.

**Round three** passed:
- the physics against the literature: the undertone exact; the weak cycle
  1.20–1.22 × the strong; the weak flow pulse −11.4 to −12.1 dB; contact
  ending within 0.1 ms of glottal closure;
- harmonics-to-noise 33–37 / 22 / 9 / 4–5 dB at 0–1 / 1–2 / 2–3 /
  3–4 kHz, and undertone jitter 0.36–0.47 %;
- the level on a steady vowel, within ±1 dB;
- the top against modal voice, +7 dB at 2–4 kHz and +4 dB at 4–6 kHz.

It signed off on one condition: every attack burst by 7–8 dB. That is now
fixed by the matched level windows and the onset guard described above. An
attack's loudest 25 ms now stands −0.8 to +2.9 dB over the steady part's
(a test checks it). This last fix was not re-reviewed: the third round was
the last.

**Still open:**
- **A finger on the 7th harmonic.** Under /a/ with vibrato it can stand
  only 1–5 dB over its neighbours, where the 6th and 8th to 11th stand
  6–17 dB. H7 is a half-harmonic between two of the folds' own, which are
  stronger at the source. The other odd ones (H9, H11) stand clear.
- **The top, in absolute terms.** 4–6 kHz is −48 dB re the whole on /a/,
  10 dB under the test singer's. Against modal voice it is brighter, as
  measured, but the overall tract is a dark male one.
- **3:1.** Its weak pulse is about −21 dB, damped more than 2:1's −11.
  Fuks's wording suggests similar damping; there is no measurement to tune
  it to.
- **Melody latency.** 100–235 ms from a vowel change to the melody
  settling on its new harmonic: the analysis window, the median of three,
  and the hysteresis that stops a wavering vowel from flipping harmonics.
- **Onset.** About 40 ms from the vowel's start, the tracker's latency.
  There is no early start, unlike RIOT.

**Fitted or chosen, not measured:**
- **The lock.** The Adler lock strength, and the false folds' natural rate
  following the folds' (their tension is not modelled). The aerodynamic
  model that explains the doubling (Bailly 2010) does not produce the
  measured lengthening, so `kBackPressure` is set to the measurement.
- **B1 25 Hz.** A stylisation, the overtone singer's.
- **Jitter, shimmer, noise and the slow drifts.** Nothing published
  measures them for kargyraa. They are chosen so that the measured
  qualities hold: "sonorous", "clear partials to 4 kHz", "more uniform".
- **The level loop.** It holds the growl to the singer's level; that is
  not physiology.

**Outside this change:**
- **THROAT's own KARGYRAA style** (an effect on the voice) is still on the
  page. The two now sit side by side; THROAT's could be pointed at this
  engine.
- **The page's pitch tracker at 96 kHz** reads 135 Hz as 139.5 and loses
  110 Hz, which this style (and others) inherit.

