# Beautify: what was weighed, and why it is built the way it is

BEAUTIFY is for someone who can't sing. They sing into the microphone, and
what comes back in their headphones, at once, is their own voice as a
record would have it:
- in tune, on the notes of the song's key;
- steady on a held note, which blooms into a gentle vibrato;
- its breaths quieter, its tone polished, its esses tamed;
- doubled, in a plate.

The whole problem is shaped by the headphones. A singer hears their own
voice twice: through their skull at once, and through the monitor a moment
later. Past about 10 ms the two come apart: first as a comb-filtered,
phasey self, then as an echo that pulls the timing. Every choice below is
first a latency choice.

The figures here come from the literature as it is generally known. They
were not re-read for this page, so treat them as orientation, not
citation. What tenfold itself measures is in `docs/beautify.md`.

## 1. How much delay a singer stands

- **Lester & Boley (2007)**, with in-ear and wedge monitors: singers
  notice from about 3–5 ms in in-ears, where the bone-conducted voice and
  the monitored one comb-filter, and find it objectionable somewhere past
  10–15 ms. Vocalists are the most sensitive of the players tested.
- Studio practice puts round-trip monitoring "under 10 ms" as the bar for
  a vocal booth.

**Target:** under 10 ms of algorithmic delay, the lower the better. The
studio's own buffering comes on top, and the Vocal page already sends the
voice round the master limiter's look-ahead.

## 2. Hearing the pitch

| Method | What it needs | Latency | For this |
|---|---|---|---|
| Autocorrelation / AMDF (Auto-Tune's original, Hildebrand's patent US 5,973,252) | ~2 periods | ~10–20 ms | Works; octave errors on breathy voices |
| **YIN** (de Cheveigné & Kawahara 2002) | ~2 periods + the longest lag | 15–30 ms window | Robust; the page already runs one |
| **pYIN** (Mauch & Dixon 2014) | YIN's candidates, then a Viterbi pass over an HMM of notes | Smoothing needs future frames: ~100 ms+ | The best offline; the look-ahead rules it out live |
| **MPM / NSDF** (McLeod & Wyvill 2005) | Normalised square difference, "key maxima" | ~2 periods | As robust as YIN, slightly cheaper |
| CREPE and other neural trackers (Kim et al. 2018) | 64 ms frames, a CNN | 32 ms+ and a GPU's worth of work | Too slow and too heavy for a monitor |

**Choice:** two stages.
1. The page's YIN, which runs anyway, on a quarter-rate copy (70 Hz–1 kHz,
   30 ms) decides whether there is a pitch and roughly where. That guards
   the octave.
2. A full-rate normalised cross-correlation over the last period alone
   (MPM's idea, searched only within three semitones of the YIN reading),
   every 64 samples. It reads the pitch about 10 ms sooner than YIN does,
   and to a fraction of a cent.

The tracker's lag never delays the audio. It only makes a correction a
little late. Halving it is what lets note changes land cleanly (see 4).

## 3. Moving the pitch

| Method | Formants | Latency | Sound |
|---|---|---|---|
| Resampling / varispeed | Moved with the pitch (chipmunk) | Small | Wrong vowels past a few semitones |
| Delay-line (two read heads, crossfaded) | Moved | ~20–50 ms window | Warbles; phasey |
| Phase vocoder (Laroche & Dolson 1999) | Moved, unless corrected by a spectral envelope (cepstrum or LPC) and re-imposed | One FFT frame: 1024–2048 samples, 21–43 ms at 48 kHz | Smooth, but "phasey"/smeared transients unless phase-locked; the frame alone breaks the latency budget |
| **TD-PSOLA** (Moulines & Charpentier 1990) | **Kept**: each grain is one glottal pulse with its vocal-tract response, laid out at the new period | Half a period on average, one at most: 2–5 ms for a singing voice | The standard for monophonic voice; clean for corrections of under a few semitones |
| Neural voice conversion / vocoders (WORLD, DDSP, HiFi-GAN) | Resynthesised | Frames of 5–20 ms plus model latency; heavy | Can sound lovely; the cost, latency and risk are all wrong for a live monitor on a laptop |

**Choice: TD-PSOLA**, the page's own `Shifter`. A correction is a fraction
of a semitone, where PSOLA is at its best. It keeps the singer's vowels
without any envelope estimation. Its delay is half a period on average:
1.9 ms for a voice around A3, 3.8 ms around A2, and never more than one
period, 9.1 ms at 110 Hz. Nothing else in the table comes near that and
keeps the formants.

The cost is the trouble PSOLA has with what has no pitch. A consonant
or a breath cut into period-long grains buzzes. So whatever the
tracker hears no pitch in goes straight through, unshifted and undelayed,
crossfaded in 6 ms. That costs nothing: a consonant has no intonation to
correct.

## 4. Correcting: what makes it sound natural

What the commercial correctors do, as documented by their makers:
- **Retune speed** (Auto-Tune). The correction follows the target through
  a time constant. 0 ms gives the robot (the page's HARD TUNE). 20–80 ms
  is "natural".
- **Humanize** (Auto-Tune). Longer notes get a slower retune, so the
  singer's own movement survives on them.
- **Flex-Tune** (Auto-Tune 8). Correct only near the target note; a
  singer's slide, scoop or fall between notes is left as sung. That
  avoids the classic artefact, a slide turned into a staircase or a
  transition held back and then let go.
- **Melodyne**, offline. It separates a note's centre ("pitch centre")
  from its drift and its vibrato and edits them apart. "Correct pitch
  centre 100 %, drift 50 %, vibrato kept" is a common "natural" recipe.
- **Note decision with hysteresis**. Snapping a voice that sits near the
  midpoint between two notes flips it back and forth: the warble every
  cheap tuner has. A held note must be kept against wobble.

A voice that can't sing has particular faults:
- the centre off by 30–50 cents, and drifting;
- a wobble of 2–4 Hz, slower and less regular than a vibrato (5–7 Hz:
  Sundberg; Prame 1994 measured 5.5–6.5 Hz in professional singers, with
  an extent of ±0.5–1 semitone in opera and much less in pop);
- scoops into notes from below;
- no vibrato at all on a long note.

**Choice:**
- **The centre goes onto the note.** It is decided from a 12 ms average of
  the pitch. While the voice moves, or is hardly being corrected yet, the
  note is simply the nearest one. Once a note settles, it is kept unless
  the voice's wobble-free centre is well over the halfway mark: under
  35 % of the step from the next note for 60 ms. In its first 0.3 s a
  note may still be revised, from just past halfway: a voice that scooped
  up from E and settled nearer F meant F.
- **Flex-Tune's rule for movement.** How much the voice is corrected
  depends on how fast it is moving: fully under 6 semitones a second,
  not at all over 18. A step between notes is the singer's own, drawn
  onto the new note as it slows, so it never turns back on the way. In a
  phrase's first 150 ms the threshold is stricter (2–6 st/s), so a scoop
  into the first note is heard and then lands, instead of first snapping
  to the note below.
- **Drift and wobble out, a little life kept.** Only 15 % of the voice's
  movement between ~3 and ~10 Hz is kept. A bad singer's movement is
  wobble.
- **A vibrato given back.** A held note blooms, from 0.28 s, into a 5.4 Hz
  vibrato of ±16 cents, full by 0.8 s: a pop singer's, not an opera
  singer's. Its rate wanders 4 % and its depth 12 %, so it is never
  machine-even. A trained singer's sustained note does exactly this. An
  untrained one doesn't, and it is much of what "sounds like a singer"
  means.
- **The key, or the chord.** The notes are the song's key and scale as
  the other pages play them, or, with follow (§), the Chords page's
  chord tones. A key change is followed at once.

## 5. The polish

What a mix engineer puts on a pop vocal, kept to what costs no latency:
- **Breath gate.** Not a hard gate, which chops word tails. A breath is
  detected as no pitch, quieter than the singing (by its peaks), and
  darker than a consonant (an s, a sh or a t is nearly all over 4 kHz).
  It comes down by up to 12 dB, and the consonants stay.
- **EQ.** Mud cut at 280 Hz (−2.5 dB), presence lifted at 3 kHz (+1.5 dB),
  air lifted over 8 kHz (+5 dB, a shelf). These are the usual moves for
  a close-miked untrained voice.
- **De-esser.** A split band over 5.5 kHz, turned down by as much as the
  band stands out of the sound, or of the singing's own level (to
  −12 dB), at once, and let back over 40 ms. The air lift would otherwise make the esses spit.
- **Compression.** The page already levels the microphone: a lift for a
  soft voice and 3:1 from −20 dBFS. A second compressor would only pump.
- **Doubles** (B: DOUBLED, the default). Two more of the singer, through
  their own shifters:
  - 11–34 ms late, each wandering on its own, so they never smear the
    lead's timing;
  - +9 and −8 cents, each drifting by up to ±9 cents, with a vibrato of its
    own (5.0 and 5.9 Hz) rather than the lead's;
  - their throats 2 % apart, darkened over 7 kHz;
  - 7 dB under, nearly hard left and right;
  - only where there is a pitch, so the esses aren't tripled.

  This is the "stacked" pop vocal, the width a doubled take gives. It is
  generated rather than sung, but at a few cents of drift it reads as a
  second take rather than a chorus effect.
- **Harmony** (B: HARMONY). A third over the note in the key, a fixed
  interval over the lead, about 4 dB under it.
- **Plate.** The studio's 16-line FDN, set small: about 1.6 s, damped
  over ~7.6 kHz, 18 ms before it. The send is cut under 220 Hz, so the
  tail never muddies the voice.

Each of these runs at the studio rate, sample by sample.

## 6. What was not done, and why

- **Formant correction beyond PSOLA's own.** PSOLA keeps the vowels at
  the small shifts a correction makes. An LPC re-imposition (the talk
  box's machinery) would cost an analysis frame of latency for nothing
  audible.
- **Look-ahead note decisions** (pYIN's HMM, Melodyne's note objects).
  These are better at deciding what a scoop meant, but they need the
  future. The 0.3 s "young note" revision is the live stand-in.
- **A neural "make it sound like a good singer".** It could change the
  timbre, which is the one thing PSOLA can't, but not inside a 10 ms
  monitor on a laptop.
