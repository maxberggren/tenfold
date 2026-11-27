# BEAUTIFY

The Vocal page's style for someone who can't sing. Sing, and what comes back
in the headphones, while you sing, is you as a record would have you:
- on the notes of the song's key (or the Chords page's chord, with follow);
- steady on a held note, which blooms into a gentle vibrato;
- breaths down, the tone polished, the esses tamed;
- doubled, in a plate.

Why it is built this way, and what else was weighed, is in
`docs/beautify-research.md`. The code is `src/beautify.h`; the engine
calls it from `src/vocal.cpp` (search for `Beautify`).

## Playing it

- **Left / Right** to BEAUTIFY, the last of the styles. Its line says: "you,
  in tune and steady, doubled, in a plate".
- **B** chooses what sings with you:
  - DOUBLED (as it comes): two more of you, a few cents apart, left and
    right;
  - HARMONY: the doubles, and a third over your note in the key;
  - SOLO: you alone.

  A song keeps the choice as `beautifyLayers`.
- **Fingers** on the lanes add harmonies, as in hard tune, round the note
  you are being sung onto.
- **§** (follow): the notes are the Chords page's chord tones.
- **− / +** move your throat, as in every style.
- **The pitch trail** rings the note you are on. The line over the lanes
  names it, and says how far off you sang it: "ON F4 · YOU SANG IT −38
  CENTS FLAT · DOUBLED".
- **Space** loops what was heard: the beautified voice, doubles and plate
  included.

## How it works

`Beautifier::process`, once per block (32 samples or fewer):

1. **The pitch.**
   - The page's YIN (`PitchTracker`) says whether there is a pitch, and
     roughly where.
   - An octave slip of its (a jump to twice or half the period) is folded
     back unless it lasts 160 ms.
   - `refine()` reads the pitch again at the full rate, over the last
     period alone: the normalised cross-correlation within three
     semitones, every 64 samples. It is ~10 ms ahead of YIN.
   - A 4 ms average takes out a reading's own jitter.
   - The grains' period moves at most 3 % a block.
2. **Moving or settled.**
   - The voice is moving from note to note while it goes faster than 10
     semitones a second (4 in a phrase's first 150 ms, where a scoop is
     slower than a step), the speed read over 12 ms.
   - Nothing is decided while it moves; the time since it settled is
     counted.
3. **The note.**
   - **Young** (the first 0.3 s after settling): from 25 ms in, the key's
     (or chord's) note nearest where the voice is (a 12 ms average). A
     phrase's first note, halfway between two, goes to the one the voice
     was heading for (up: a scoop comes from below). A young note then
     changes only to a neighbour the voice is clearly nearer (under 42 %
     of the step from it) for 40 ms.
   - **Held**: kept until a quarter-second centre of the voice is under
     35 % of the step from the next note for 80 ms. A wobble never gets
     there.
4. **How much to correct**, `m_amt`, the least of:
   - the speed: fully under 6 st/s, not at all over 18 (in a phrase's
     first 150 ms: under 2, over 6);
   - the time settled: none before 25 ms, all by 55 ms;
   - for a note's first 120 ms, how sure the note is: none within 0.04
     semitone of halfway to the neighbour, all from 0.14.

   It falls in 6 ms and rises in 10 ms.
5. **The pitch it comes out at.** It is
   `p + m_amt · (G + life + vibrato − p)`, where:
   - `p` is the voice's pitch;
   - `G` is the note, reached by an S-curve (two poles of 16 ms), or at
     once while the voice is hardly being corrected;
   - `life` is 15 % of the voice's movement between ~3 and ~10 Hz, or, if
     the voice has a vibrato of its own (a 4.5–7.5 Hz band at over ±7
     cents rms and 1.5× the 2–4 Hz wobble under it), 85 % of that
     vibrato;
   - the vibrato is 5.4 Hz, ±16 cents, from 0.28 s into a note and full by
     0.78 s; its rate wanders 4 % at 0.3 Hz and its depth 12 % at 0.7 Hz.
     It is left out as far as the voice has its own.
6. **The voice moved**: TD-PSOLA (`vocal::Shifter`), the singer's formants
   kept. Where there is no pitch (and after 40 ms of none), the voice goes
   straight through instead, crossfaded over 6 ms. A grain whose window
   would have opened in the past (the period having grown) opens now
   instead: no step.
7. **The polish**, per sample:
   - **Breaths** (no pitch, under the singing by 4 dB and more by its
     peaks, and darker than a consonant: under 75 % of the envelope over
     4 kHz) come down by up to 12 dB.
   - **Doubles** (DOUBLED and HARMONY), two more shifters, each its own
     singer:
     - 11–25 and 18–34 ms behind, wandering at 0.11 and 0.07 Hz;
     - +7 and −6 cents, each drifting ±7 cents;
     - throats ×0.98 and ×1.02;
     - darkened over 7 kHz;
     - −7 dB, panned almost hard left and right (0.99/0.15);
     - only while there is a pitch.
   - **Harmony** (HARMONY): a fourth shifter, a third over the note in the
     key, 8 ms behind, −7 dB, right of centre.
   - **EQ**, per channel: −2.5 dB at 280 Hz (Q 1), +1.5 dB at 3 kHz (Q 0.8),
     and a +4 dB shelf over 10 kHz.
   - **De-esser**: the band over 5.5 kHz (a complementary split), turned
     down when its envelope is over 45 % of the whole's, by
     (over)^−0.8, down to −12 dB; attack about 0.4 ms, release about
     35 ms.
   - **Plate**: the studio's FDN `Reverb` at 0.33 (t60 ≈ 1.6 s, damped
     over ~7.6 kHz, 18 ms pre-delay), sent from the mix high-passed at
     220 Hz, returned at 0.45.
   - **Level** ×1.1: within 1.5 dB of the natural voice.

## Measured

All of it from `tests/vocal_tests.cpp`. The bad singer is `badSinger()`:
- a synthetic voice (glottal harmonics at −6 dB/oct, four gliding vowel
  formants, pulsed breath noise, shimmer);
- each note 25–45 cents off;
- scooped into from 60 cents under, or slid from the last over 90 ms;
- drifting ±10 cents, wobbling at 2.7 Hz (±22 cents) and 4.1 Hz
  (±14 cents), with a random walk;
- breaths, an "s" and a "t", and a room's hiss.

It is sung into the page's microphone as a WAV and played through the
whole studio.

| What | Before | After |
|---|---|---|
| A phrase (8 notes, A minor), every 5 ms from 80 ms into each note | 40.1 cents rms | 17.5 |
| The same, from 150 ms in (once the singer has arrived) | 40.0 | 9.1 (the long notes' vibrato included) |
| Each note's average from 150 ms in, rms over the notes | 35.4 | 3.5 (worst note 6.5) |
| A tune of 22 notes, low and an octave up, notes on the one nearest what was sung | | 20 of 22 both (the other two were sung nearer their neighbour); 1.8 % and 3.2 % of held moments off their note |
| F4 held 2.5 s, sung −40.8 cents with a wobble | −40.8 average | −0.9; never off F; 5 ms steps 1.8 cents rms, 4.7 at most; vibrato ±15.6 cents at 5.4 Hz |
| A legato line, at each change | | never turned back on the way (6 cents at most), never past where the singer went (+2), on the note within 135 ms of the singer's arrival, level within ±3.2 dB of its median |
| F4 +20 cents, held | | on F in A minor (64.99), F♯ after Ctrl+Right to E minor (65.99), E on A minor's chord with follow (63.99) |
| A loop of the phrase (Space): pitched moments within 15 cents of the key | 18 % (natural) | 76 % |
| A breath under the singing | 10.7 dB (natural) | 20.5 dB |
| An "s" against the vowel (over 5 kHz) | +10.6 dB (natural) | +4.2 dB |
| Level against the natural voice | | −1 to −2.6 dB |
| Left against right (correlation) | | 0.88 doubled, 0.98 solo |
| HARMONY: E4 over a sung C4 | | +38 dB |
| Leaps the tracker loses: energy over 12 kHz | −52.8 dB (natural) | −52.9 dB |
| Times the feedback guard dipped the monitor on the phrase | 3 (natural) | 3 |

The reviewer's own measure, over its renders (within ±25 cents of the note
meant / 60 cents or more off): the bad singer's microphone 33 % / 13–15 %;
BEAUTIFY 77 % / 12 % (low), 75 % / 13 % (an octave up); a held note 94 % /
1.6 %; a clear legato line 81 % with 0.2 % on a wrong semitone; the same
synthetic voice singing the tune well, untreated, 92 % / 4.7 %.

### Latency

The voice moved by grains is late by however far back its newest grain
reads: from 2 samples to one period plus 2.
- **Measured grain by grain**, the shifter's own account of a bad singer's
  phrase:
  - round A3 (175–350 Hz): 2.2 ms on average, 4.6 ms at most;
  - round A2 (88–175 Hz): 3.6 ms on average, 9.0 ms at most.

  At most one period: under 10 ms for any voice over 100 Hz.
- **End to end**, the loudness of a held note sung 40 cents flat, wavering,
  against what comes out of the studio: 3.75 ms at 110 Hz and 3.0 ms at
  220 Hz.
- **Consonants and breaths** go straight through: a hiss comes out 0
  samples late.
- **The pitch tracking** never delays the audio. It makes a correction
  late, by about 5 ms with `refine()` (YIN alone: ~15 ms).

### Cost

The beautifier at its dearest (HARMONY: four shifters, the plate) with
the pitch tracker that feeds it: **1.7 % of a core** on the development
machine (`beautifyStaysInsideBudget`, by the thread's own CPU time).

## Review

Three rounds of critique by a reviewer that was given the code and the
renders (before and after, natural and beautified), not these numbers, with
the bar "be wowed". Its verdict after the third: **not wowed**.

Fixed along the way:
- notes passed through on a slide taken as the note;
- scoops snapped to the note below;
- grains that opened part way through their window (clicks on a real
  voice);
- the tracker's octave slips;
- the singer's own vibrato thrown away;
- doubles too close to the lead;
- ticks from the studio's limiter;
- a de-esser that couldn't reach its target;
- the air shelf too high to be heard;
- the harmony collapsing to unison between notes.

Its praise: held notes "lovely", clear notes "pro-grade", the engine clean,
the polish working, the stereo wide, the latency excellent.

## Open points

- **Overshoots and small slow steps.** A voice that arrives past its note
  and comes back is heard past it for up to ~100 ms. A step slower than 10
  semitones a second to a semitone neighbour can stay on the old note for
  ~0.3 s.
  - A quicker displacement rule was tried: on the 12 ms average, 30 % of
    the step, 40 ms. It flipped held notes on a wobbling voice, so it was
    taken out.
  - Live, the arrival and a wobble look alike. Telling them apart needs
    look-ahead or a better model of the singer.
- **Phrase starts sung near halfway** between two notes are left up to a
  quarter tone out for 100–200 ms while the note is in doubt. That was
  chosen over snapping to the wrong note and then the right one.
- **The doubles** still read partly as a chorus: their level swings 8.6 dB
  on a held note against 6.3 natural. Their grains could be decorrelated
  further (jitter, formant offsets), or they could be ducked in
  transitions.
- **4–7 kHz** is 2–3 dB under natural. `Source::at` interpolates linearly; a
  4-point Hermite read would serve every style that shifts.
- **The first ~100 ms of a breath** isn't brought down. On speech, a voiced
  onset right after a consonant dips for 10–20 ms.
- **Untested on real singing.** The singer in the tests and the review is
  synthetic. The only real voice on the development machine is a speech
  sample, which BEAUTIFY passes cleanly but which says nothing about
  intonation. Detecting a real singer's own vibrato is untested. The
  doubles, 11–34 ms behind, may read as slapback in a singer's own
  headphones.
- **The feedback guard on pure vowels.** The guard (`src/howl.h`) briefly
  dips the monitor on a sung "ee" or "oo" whose first formant sits on one
  harmonic. It does this in every style, natural included, and BEAUTIFY
  makes it no more likely (3 dips on the test phrase either way).
