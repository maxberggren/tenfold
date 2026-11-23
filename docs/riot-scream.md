# Riot: a scream from across a field

The Vocal page's last style. You sing or speak, and what comes back is your
words screamed the way a riot-grrrl singer screams: high, pressed and raw,
heard from a few metres away. It is not a chain of effects on your voice.
The voice is made again (`src/scream.h`), with your mouth kept and your
throat replaced. This page says what it was measured against, how it works
and how it was checked.

## The reference

The reference is a 2.75 s phone video (668 × 968, 60 fps, AAC 48 kHz
stereo at 129 kb/s). It was filmed outdoors on grass: two players and a
frame of copper pipes slapped as drums, about 2–3 m from the camera. Under
the drums a voice wails. All the numbers below come from the clip's audio
(`ffmpeg … -c:a pcm_f32le`). The drums sit under the voice everywhere
below 600 Hz and in broadband slaps every ~0.4 s, so every voice
measurement is taken above 900 Hz or between the slaps. Each one is an
upper bound on noise and a lower bound on purity.

### What the voice is

| Trait | Measured | How |
|---|---|---|
| Pitch | f0 506–564 Hz (B4–C♯5), median 542 Hz | harmonic comb over 80 ms frames, refined on H2 |
| Contour | scoops up into notes (~3 semitones in 100 ms) and falls ~5 semitones at the end | H2 track |
| Steadiness | 5 ms pitch steps sd 2.3–2.8 c; residual after 60 ms smoothing 1.5–3 c | not a chaotic scream: a held, belted wail |
| Harmonics (dB re H2) | H1 −7, H3 −14, **H4 −7**, H5 −13, H6 −21, H7 −18, H8 −27, H9 −26, H10 −33 | median over 30 voiced frames |
| Resonances | F1 ≈ 1.0–1.1 kHz (on **H2**), F2 ≈ 2.0–2.2 kHz (on H4), F3 ≈ 3.5–3.9 kHz | the peaks of the harmonic profile |
| Slope | −12.3 dB/oct, H2 to H10 | a pressed, bright source |
| Noise | harmonics 16.6 dB over the level half way between them (1–4.5 kHz) | an upper bound on noise (drums included) |
| Subharmonics | half-harmonics only +2 dB over quarter-harmonics | almost none: no sustained period doubling |
| Band | voice harmonics visible from ~1 kHz to ~5.5 kHz; the mix is −40 dB by 11 kHz and cut at ~16.5 kHz (AAC) | spectrogram, long-term spectrum |
| Room | after an offset the 0.8–3 kHz band falls −9, −16, −24 dB at +10, +20, +40 ms | open air: no tail to speak of |
| Stereo | L/R correlation 0.996, side −27 dB | a phone: mono |

H2 dominates (H1 −7 to −16 dB under it), and the vowel's F1 sits right
on it. That is the belter's resonance strategy (F1 tuned to 2·f0) that
voice science finds in belting and shouting. The open, front vowel
(F1 ~1 kHz, F2 ~2.1 kHz, an /æ/ with the jaw dropped) is a screamed
"yeah"/"aah". Pitch is high and fairly steady, with high harmonics strong
for its level, real noise between the harmonics and no lasting
subharmonic regime. Distance shows in three ways:
- the voice is far under the drums;
- its band is a phone's, and its top falls away;
- it is open air, where almost nothing comes back after it stops.

## How it is made

All of it runs at a third of the studio's rate (16 kHz), sample by sample:

1. Down to 16 kHz through an eighth-order Butterworth.
2. Up again through two more.

There is no look-ahead, so the scream is about a millisecond behind the
microphone.

**The mouth.** Every 4 ms, 25 ms of your voice is windowed (Hann),
pre-emphasised (0.92) and turned into a 512-point power spectrum.

- **The singer's glottis out.** A first-order fit of the spectrum is divided
  out, as IAIF does (Alku 1992). What is left is the vocal tract alone, so a
  soft or breathy singer doesn't make the scream dull.
- **The tract moved.** It is shifted up by α as the spectrum P′(f) =
  P(f/α), which can never go negative, so the poles stay stable. This is the
  raised larynx and shorter, more open tract of a screamer:
  - α = 1.08 at your own octave;
  - +0.06 for each octave the scream moves you up.
- **The poles.** Sixteen poles come from Levinson–Durbin, with bandwidths
  widened by 60 Hz and a −40 dB floor. The analysis's own poles (unmoved)
  inverse-filter your voice in a lattice, giving the residual. The moved
  ones are the new mouth, also a lattice. Coefficients glide across each
  4 ms hop in reflection form, which is always stable.

**The throat.** Your source is replaced by a model in SI units, run by your
pitch and loudness:

- **Pitch.** The note you sing, moved by whole octaves into a screamer's
  register (the octave nearest 540 Hz), chosen at the start of each phrase
  and kept to its end so a melody keeps its shape.
  - The tracker is followed only where it is sure (clarity ≥ 0.75).
  - A leap of more than a fourth counts only once it lasts 30 ms. Until
    then the throat is quiet and the consonant and breath go on.
- **Glottis.** A 1 cm slit opening to 0.14 cm², with a 0.01 cm² leak. It is
  open 42 % of the cycle, opening on a raised cosine and closing on a
  quarter cosine, so it is still closing when it shuts (Rosenberg): the
  collision of a pressed voice. Each cycle's length and height wander:
  jitter 0.35 %, shimmer 0.06 neper.
- **False folds.** A second slit in series. In a soft voice it is wide
  open (2.4 × the glottis's widest). Effort draws it in to 0.75 × that, so
  it caps the flow, and it flutters at half the glottal rate. The flutter
  depth (4–32 %) comes in bursts that wax and wane over a few hundred
  milliseconds. This is the supraglottal vibration high-speed imaging shows
  in rock "distortion" tones (Borch, Sundberg, Lindestad & Thalén 2004)
  and the ventricular period doubling of Bailly et al. (2010).
- **Flow.** The lungs' pressure is spent on the jet through both slits
  (½ρU²(1/Ag² + 1/As²), Bernoulli) and on accelerating the air of the
  epilarynx above them (inertance ρ·2.5 cm/0.7 cm², L·dU/dt). It is solved
  implicitly each sample, as the positive root of a quadratic.
  - The inertance skews each pulse late and stops it abruptly (Rothenberg),
    which is where a pressed voice's highs come from.
  - The false folds' cap is the distortion, a soft limit on every pulse with
    no curve chosen by ear.
  - With alternating caps from the flutter come subharmonics.
- **Turbulence.** Each slit's jet makes noise ∝ (Re² − Re_c²) above a
  critical Reynolds number (Meyer-Eppler; Flanagan & Cherry 1969). For a
  slit Re = 2U/(νL), whatever its width.
  - The false folds' jet only counts as far as they are a constriction
    (Ag/As).
  - So the noise rides on each pulse, louder as the flow is, and the rasp
    grows with the strain.
  - We use Re_c = 2700 and 2·10⁻⁶ Pa per Re², the values Flanagan-type
    synthesisers use. We could not check them against the 1969 paper
    itself, so the scale was checked against the reference instead: our
    harmonics-to-noise lands at 15–18 dB, against its 16.6.
- **Radiation.** The noise pressure enters as a flow through the tract's
  impedance ρc/A. The lips radiate dU/dt.
- **Pressure.** The voice's level sets the lungs' pressure, 0.7 to 3 kPa
  (soft speech to a scream). The false folds come in from 1.4 kPa.
- **Your consonants.** These are made in the mouth, not the throat, and
  they pass from your residual high-passed at 1.2 kHz. The residual's buzz
  would otherwise be your own voice at your own pitch.

**The jaw.** F1 rides on the second harmonic: a resonance at 2·f0
(650–1300 Hz, Q 3.5) on top of the moved mouth, as the jaw drops and the
belter's F1 tracks H2.

**The place** (B), at 16 kHz, mono in and stereo out:

| | Direct | Reflections | Tail |
|---|---|---|---|
| **FIELD** (default, the reference) | 0.9, off-axis low-pass 5 kHz | ground at 3.1 ms, 0.52: mouth 1.6 m and phone 1.2 m up, 3 m apart, path 1.07 m longer, 0.74 by spreading × ~0.7 for grass, dulled at 2.2 kHz. Then bodies and the frame at 7–39 ms, −16 to −26 dB, either side | almost none: T60 0.22 s at −24 dB |
| **ROOM** | 0.75, low-pass 4.5 kHz | image sources of a 5 × 4 × 2.7 m room 3 m off: floor 4.4 ms, walls, ceiling, their seconds to 34 ms, 0.15–0.55 | four lines 25–46 ms through a Householder matrix and two diffusers, T60 0.6 s, about as loud as the direct sound (3 m is about a small room's critical distance) |
| **CLOSE** | 1.0, 7 kHz | none | none |

Air absorption over 3 m is a few tenths of a dB at 8 kHz, nothing next to
the voice's directivity, so it is folded into the low-pass. A low cut at
260 Hz is the phone and the distance (no proximity bass).

**The fingers.** A lane held is another screamer at that harmony, through
the same mouth: a gang shout. Up to four are heard, each a throat of its
own (its own jitter and flutter) on the shared tract.

## How it was checked

The same measurements as the reference's (`features.py`, `tail.py` in the
work notes) were taken on renders through the real studio. The inputs were:

- a test singer: Rosenberg pulses with jitter, shimmer, vibrato and breath,
  through five moving formants with Klatt's higher-pole correction, and
  fricatives and plosives;
- a man around 130 Hz, a woman around 262 Hz, the same woman on /æ/
  throughout (`wail`), and shouted speech.

| | Reference | Riot (wail, FIELD) | Riot (male, FIELD) |
|---|---|---|---|
| f0 median | 542 Hz | 527 Hz | 522 Hz |
| H2..H10 slope | −12.3 dB/oct | −13.9 dB/oct | −19.1 dB/oct (an /a/: dark above F2) |
| H3 / H4 / H5 re H2 | −14 / −7 / −13 | −17 / −8 / −13 | −22 / −19 / −29 |
| harmonics over half-way | 16.6 dB | 16.9 dB | 15.0 dB |
| subharmonic index | +2.0 dB | +2.6 dB | +5.6 dB (male, louder: more strain) |
| after an offset, +10/+20/+40 ms | −9 / −16 / −24 dB | −14 / −22 / −30 dB | |
| L/R correlation | 0.996 | 0.996 | 0.993 |

Cost: one screamer is about 0.3 % of a core and four more add about
0.25 %, measured on a loaded machine.

The review's verdict and the deviations it accepted are below.
