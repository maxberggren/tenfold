# Riot: a scream from across a field

RIOT is the Vocal page's last style. You sing or speak, and what comes back
is your words screamed the way a riot-grrrl singer screams: high, pressed
and raw, heard from a few metres away.

It is not a chain of effects on your voice. The voice is made again
(`src/scream.h`): your mouth is kept and your throat is replaced. This page
covers what it was measured against, how it works and how it was checked.

## Playing it

- **Choosing it:** ← → to RIOT (after SCREWED). Songs keep it by name
  (`"style": "riot"`).
- **B, where the singer is** (saved as `riotPlace`):
  - **FIELD** (the default, the reference): three metres off on grass, in
    open air.
  - **ROOM:** three metres off in a small room.
  - **CLOSE:** the scream with nothing around it.
- **Fingers:** a lane held is another screamer at that harmony, through the
  same mouth: a gang shout. Up to four are heard.
- **Effort:** sing louder and the throat strains more. The false folds come
  in, with more rasp and more noise.
- **Pitch:**
  - Each phrase is screamed at the octave of your note nearest 540 Hz, and
    keeps that octave to its end.
  - A man's voice goes up two octaves, a woman's one.
  - Nothing is screamed over 1.3 kHz or under 150 Hz.
- **Recording:** Space loops what you hear, so a loop of a riot phrase is the
  scream.

## The reference

The reference is a 2.75 s phone video: 668 × 968 at 60 fps, with AAC audio
at 48 kHz stereo, 129 kb/s.

- **The setting:** outdoors on grass. Two players and a frame of copper
  pipes slapped as drums sit about 2–3 m from the camera, with a voice
  wailing over them.
- **The method:** every number below comes from the clip's audio.
- **The drums get in the way:**
  - They fill everything below 600 Hz and add broadband slaps every
    ~0.4 s.
  - So the voice is measured above 900 Hz or between the slaps.
  - Its noise figures are upper bounds.

| Trait | Measured | How |
|---|---|---|
| Pitch | f0 506–564 Hz (B4–C♯5), median 542 Hz | harmonic comb over 80 ms frames |
| Contour | scoops up into notes (~3 semitones in 100 ms), falls ~5 semitones at the end | H2 track |
| Steadiness | 5 ms pitch steps sd 2.3–2.8 c | a held, belted wail, not chaos |
| Harmonics (dB re H2) | H1 −7, H3 −14, **H4 −7**, H5 −13, H6 −21, H7 −18, H8 −27, H9 −26, H10 −33 | median of 30 voiced frames |
| Resonances | F1 ≈ 1.0–1.1 kHz, on **H2**; F2 ≈ 2.0–2.2 kHz, on H4 | peaks of the harmonic profile |
| Slope | −12.3 dB/oct, H2 to H10 | a pressed, bright source |
| Noise | harmonics 16.6 dB over the level half way between them (1–4.5 kHz) | an upper bound (drums included) |
| Subharmonics | half-harmonics +2 dB over quarter-harmonics; 6 % of frames period-doubled | almost none |
| Band, voiced frames, re 1–2 kHz | 4–6 kHz −17, 6–8 kHz −29, 8–12 kHz −28 dB | above ~5.5 kHz the harmonics stop but the breath goes on (it follows the voice: correlation 0.66) |
| Top | 40 dB down by 11 kHz in the mix; the AAC cut at ~16.5 kHz | long-term spectrum |
| After an offset | −9, −16, −24 dB at +10, +20, +40 ms | open air: no tail to speak of |
| Stereo | L/R correlation 0.996 | a phone: mono |

**What the voice is.** H2 leads, with H1 7–16 dB under it, and the vowel's
first resonance sits right on it. That is the belter's strategy: F1 tuned
to 2·f0, as found in belting and shouting. The vowel is an open front one:
F1 at about 1 kHz and F2 at about 2.1 kHz, an /æ/ with the jaw dropped, a
screamed "yeah" or "aah". The pitch is high and fairly steady, the highs
are strong for its level, there is real noise between the harmonics, and
there is no lasting subharmonic regime.

**Where its distance comes from:**
- the voice sits far under the drums;
- its band is a phone's;
- it is in open air, so almost nothing comes back after it stops.

## How it is made

### The rates

The voice runs at a third of the studio's rate (16 kHz), sample by sample:
- in through a twelfth-order Butterworth, so an /s/ at 10 kHz folds back
  46 dB down;
- out through an eighth-order one;
- the band above it is made at the full rate.

There is no look-ahead. The filters put the scream about 0.3 ms behind the
microphone.

### The mouth

Every 4 ms, the last 25 ms of your voice is analysed:
1. It is windowed (Hann), pre-emphasised (0.92) and turned into a 512-point
   power spectrum.
2. **The singer's glottis out.** A first-order fit of the spectrum (the
   first step of IAIF, Alku 1992) is divided out, capped at ±0.7 so a
   closed vowel's low F1 isn't taken for tilt.
3. **The smoothing.** Bandwidths are widened by 60 Hz, or a third of your
   pitch if that is more, so a high voice's harmonics are never taken for
   formants. There is also a −40 dB floor.
4. **The poles.** Sixteen poles come from Levinson–Durbin. These are your
   mouth: a lattice that inverse-filters your voice.

### The scream's mouth

Your spectrum is moved before its poles are found. A moved spectrum is
still a spectrum, so the poles are always stable.

- **The larynx raised and the tract shortened.** Every resonance moves up
  by α:
  - 1.08 at your own octave;
  - +0.06 for each octave you are screamed up.
- **The jaw dropped.** Your first resonance (the lowest peak of a ten-pole
  fit, 200 Hz–1.4 kHz) is carried, as a block of ±δ around it, onto the
  second harmonic: the belter's F1 ≈ 2·f0. Over 650 Hz it goes just above
  the first harmonic, as a soprano's does. Between it and the knee the
  spectrum is stretched to fit. F2 and above move by α only, and F1's width
  grows by α only.
- **The level, vowel by vowel.** From the moved poles and the throat's own
  measured harmonics (H1 −1, H2 0, H3 −6, H4 −9 dB …), what the scream will
  carry through this mouth is computed every 4 ms and set to your level in
  the same window. The voice's level over the last 8 ms corrects it by up to
  12 dB, so a vowel comes in as fast as it is sung. A one-second match takes
  out what is left. Without this, an /u/ would be 10 dB quieter than the /u/
  you sang: the pre-emphasised level counts low resonances for little.

### The throat

Your source is replaced by a model in SI units, driven by your pitch and
loudness.

**Pitch.** It is the tracker's reading, but the tracker's octave is not
trusted.
- **The octave check.** Each reading is checked against your spectrum by
  subharmonic summation:
  - if the odd multiples of half the reading are there, the tracker read
    your second harmonic;
  - if the reading's own odd harmonics are missing, it read half of you.
- **Starting a phrase.** A phrase starts on a reading of clarity ≥ 0.7 and
  takes the octave nearest 540 Hz.
- **Octave slips.** A reading an octave or two off the held note is folded
  back onto it, so a slip changes nothing.
- **Other leaps.** A leap of more than a fourth counts at once if the
  tracker is sure (clarity ≥ 0.93), and otherwise once it has lasted 15 ms.
  Until then the throat is quiet and the consonant goes on.
- **Re-choosing the octave.** In a phrase's first 150 ms, a confirmed new
  note chooses the octave again, because a phrase's first reading is often
  a guess.

**When it sounds.**
- **Voicing.** The throat sounds while the tracker says you are voicing.
- **Before the tracker.** Inside a phrase it also sounds as soon as your
  voice is up, its energy is low (under 900 Hz) and it is periodic, with at
  least half its energy on the harmonics of a pitch within a tone of your
  last note (found to a quarter tone). It starts on that pitch, so a
  repeated or nearby note after a consonant is screamed from its start.
  A leap waits for the tracker, so it starts on its own note.
- **Never on noise.** It never sounds on a hiss or burst that the tracker
  calls voiced. Harmonicity has to be over 0.4: a vowel is 0.85–0.97, an /h/
  0.36–0.42.
- **The attack.** It comes in over 2–3 ms (a screamer attacks hard) and goes
  over 10–16 ms.

**The glottis.** A 1 cm slit opening to 0.14 cm², with a 0.01 cm² leak.
- It is open 42 % of the cycle, opening on a raised cosine and closing on a
  quarter cosine, so it is still closing when it shuts (Rosenberg). That is
  the collision of a pressed voice.
- Each cycle's length and height wander: jitter 0.35 %, shimmer 0.06 neper.

**The false folds.** A second slit in series.
- In a soft voice it is wide open: 2.4 × the glottis's widest.
- Effort draws it in to 0.75 × that, where it caps the flow.
- It flutters at half the glottal rate, 3–17 % deep, in bursts that come
  and go over a few hundred milliseconds. This is the supraglottal
  vibration high-speed imaging shows in rock "distortion" tones (Borch,
  Sundberg, Lindestad & Thalén 2004), and the ventricular period doubling
  of Bailly et al. (2010).
- It is kept shallow because the reference shows almost no lasting
  subharmonics.

**The flow.** The lungs' pressure (0.7 kPa soft to 3 kPa, from your level)
is spent in two places:
- the jet through both slits, by Bernoulli: ½ρU²(1/Ag² + 1/As²);
- accelerating the epilarynx's air: inertance ρ·2.5 cm/0.7 cm², times
  dU/dt.

It is solved implicitly each sample, as the positive root of a quadratic.
- The inertance skews each pulse late and stops it abruptly (Rothenberg).
  That is where a pressed voice's highs come from.
- The false folds' cap is the distortion: no curve chosen by ear.
- Alternating caps give the subharmonics.

**The turbulence.** Each slit's jet makes noise ∝ (Re² − Re_c²) above a
critical Reynolds number (Meyer-Eppler 1953; Flanagan & Cherry 1969). For
a slit, Re = 2U/(νL) whatever its width.
- The false folds' jet counts only as far as they are a constriction
  (Ag/As).
- The noise rides on each pulse, and the rasp grows with the strain.
- The constants in the literature are cgs and vary: Re_c from 1800 to 2700.
  So the scale (2·10⁻⁶ per Re², Re_c 2700) is set to the reference: the
  scream's harmonics stand 14–18 dB over the noise between them, against
  its 16.6.

**Radiation.** Noise pressure enters as a flow through the tract's impedance
ρc/A, and the lips radiate dU/dt.

**The air above the band.** The jets' noise is broadband, so above the
inner band it is made at the full rate, at its own level. A flow's
derivative goes through the mouth's gain, then:
- down at −6 dB/oct past 8 kHz (off the voice's axis);
- cut by the phone's codec near 16 kHz.

### Your consonants

Consonants are made in the mouth, by turbulence. They come from noise:
- shaped by your own mouth (its poles, not the scream's: a hiss is shaped
  where it is made);
- de-emphasised back to your tilt;
- held to your own level above 600 Hz every 2 ms.

Above the inner band, your own consonant's top is let through as it is (it
has no pitch). Your buzz, at your pitch, never gets through.

### The place (B)

At 16 kHz, mono in and stereo out.

| | Direct | Reflections | Tail |
|---|---|---|---|
| **FIELD** (default, the reference) | 0.9; off-axis low-pass at 5 kHz | ground at 3.1 ms, 0.52 (mouth 1.6 m up, phone 1.2 m up, 3 m apart; the path 1.07 m longer; 0.74 by spreading × ~0.7 for grass, dulled at 2.2 kHz); then bodies and the frame at 7–39 ms, −16 to −26 dB either side | almost none: T60 0.22 s at a low send |
| **ROOM** | 0.75; low-pass at 4.5 kHz | image sources of a 5 × 4 × 2.7 m room, 3 m off: floor 4.4 ms, walls, ceiling, their second bounces to 34 ms (each tap's spreading × 0.9 of the pressure back: mean absorption 0.2, as Sabine has for T60 0.5 s) | four lines of 25–46 ms through a Householder matrix and two diffusers; measured T60 0.53 s. The room's critical distance, 0.057·√(V/T60), is 0.6 m, so 3 m off the reflected sound outweighs the direct, here by 5 dB. Wide stereo. |
| **CLOSE** | 1.0; 7 kHz | none | none |

- **Air absorption** over 3 m is a few tenths of a dB at 8 kHz, nothing next
  to the voice's directivity, so it is folded into the low-pass.
- **A fourth-order low cut at 250 Hz** stands for the phone, and for the
  distance: no proximity bass.

### A guard

A voice's mouth is broad. A pure tone's analysed "mouth" is one sharp pole,
and a scream harmonic on it, or the tone on one of the room's modes, would
ring far louder than what went in. So the output's power over 20 ms is held
to at most 18 dB over the singer's level, with that level held as it falls.

Voices measure at most 11 dB over, plus 5 dB in ROOM, so the guard never
touches them. A tone sweeping through the room's modes can still peak at
16× (its crest factor), and the Vocal page's ceiling after the style takes
that.

## How it was checked

The same measurements as the reference's were taken on renders through the
real studio (`tenfold render`, with `mic` feeding a WAV into the microphone)
and through a harness that copies the page's front end (gate, lift,
compressor and tracker). The inputs were:
- **a test singer:** Rosenberg pulses with jitter, shimmer, vibrato and
  breath, through five moving formants with Klatt's higher-pole
  correction, with fricatives and plosives. A man at about 130 Hz, a woman
  at about 262 Hz, the woman on /æ/ throughout (`wail`), and shouted
  speech;
- **a real woman's speech** at about 245 Hz;
- **a high singer** at 820 and 975 Hz.

| | Reference | Riot wail /æ/ | Riot male | Riot real speech |
|---|---|---|---|---|
| f0 median (IQR) | 542 (510–558) | 625 (522–749; the melody spans a fifth) | 615 (521–709) | 494 (420–552) |
| H1 / H3 / H4 / H5 re H2 | −7 / −14 / −7 / −13 | −14 / −14 / −7 / −12 | −8 / −19 / −15 / −22 | −7 / −8 / −13 / −11 |
| Slope H2..H10 | −12.3 dB/oct | −15.9 | −17.2 (an /a/ man: dark above F2) | −12.7 |
| Harmonics over half-way | 16.6 dB | 17.5 | 16.5 | 14.2 |
| Period-doubled frames | 6 % | 3 % | 16 % (louder: more strain) | 9 % |
| Voiced bands re 1–2 kHz: 4–6 / 6–8 / 8–12 kHz | −17 / −29 / −28 | −17 / −32 / −30 | | −17 / −31 / −27 |
| After an offset, +10 / +20 / +40 ms | −9 / −16 / −24 | −13 / −20 / −34 | | |
| L/R correlation | 0.996 | 0.993 | 0.994 | 0.995 |

Against the NATURAL style on the same inputs:

| Check | Result |
|---|---|
| **Level** (active, 200 Hz–6 kHz) | within ±2 dB on all six inputs |
| **Per-vowel level** | spread 7.7–7.9 dB across /a e ʌ æ i o u/ (the singer's own vowels differ by about 5 dB). /i/ is within 1 dB of the vowels' mean, /u/ −5 |
| **Consonants** | burst or hiss re the following vowel within 5 dB of NATURAL's ratio for all twelve, /r b l g s t n w ʃ k d h/ |
| **Onset** (input voiced → scream within 6 dB of its steady level) | median 20–25 ms and at most 35–50 ms, against NATURAL's 5. The tracker's first reading of a phrase, or of a leap, takes 17–25 ms. A nearby note inside a phrase starts on the vowel's periodicity. |
| **Pitch at a syllable's edge** | the throat's own pitch in a syllable's first 40 ms against the 70 ms after: median 56–135 cents on the test singers (the man's p90 426). On real speech the median is 251, much of it the speech's own glide. Octave relation to the sung note: 0–4 % of frames off on the test singers, 11–17 % on real speech and the high singer, where the reference pitch estimator itself is unsure. |
| **Register** | real speech p10/p50/p90 395/493/565 Hz; the man and the woman 521/692/795 Hz (twice the sung note, and four times for the man) |

**Cost.** One screamer is 0.5 % of a core, and four more add 0.25 %
(best of five, on a machine shared with seven other builds). In the app, a
128-frame callback costs about 0.8 percentage points more than NATURAL.

The review's verdict and the deviations it accepted are below.
