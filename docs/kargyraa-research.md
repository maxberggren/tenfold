# Kargyraa: the physiology

Kargyraa is the low, growled style of Tuvan throat singing. The singer's
vocal folds sound a fairly ordinary low male pitch. The ventricular (false)
folds above them vibrate too, at half that rate, and close once every two
glottal cycles. What the listener hears is a pitch an octave under the
sung one. Over it, the singer moves the vowel so that a resonance picks out
one harmonic after another of that low pitch, and those harmonics make a
melody.

This page sets out what has been measured, and where. `docs/kargyraa.md`
maps each constant of the synthesis (`src/kargyraa.h`) to these numbers.

Each figure here is labelled by how it was read:
- **primary:** from the full text;
- **abstract:** from the official abstract only;
- **secondary:** from another author's summary of a paper that could not be
  read;
- **inferred:** worked out here, with the working shown.

Lindestad et al. (2001) and Fuks et al. (1998) were not available in full,
so most of their numbers come through Bailly's 2009 thesis, which
summarises both.

## 1. Two pairs of folds

### Which folds

Every imaging study of kargyraa, or of singing that sounds like it, shows
the ventricular folds vibrating at a fraction of the vocal folds' rate:
- **Lindestad, Södersten, Merker & Granqvist (2001):** a Mongolian singer,
  filmed at 1904 frames/s with high-speed imaging. The ventricular folds
  vibrate at half the vocal folds' frequency, with "complete but short"
  closures "covering every second vocal fold closure" (abstract). The
  vocal folds keep the same rate in modal voice and in kargyraa; the
  perceived pitch falls an octave, from 140 Hz to 70 Hz (secondary, via
  Bailly 2009).
- **Fuks, Hammarberg & Sundberg (1998):** Fuks's own "vocal–ventricular
  mode", a Tibetan-chant-like voice. The ratio was 2:1, "or, in some
  cases, 3:1" (thesis conclusions, primary), and phonation at f0/3 "was
  also possible" (abstract).
- **Sakakibara et al. (2001):** two singers trained in Tuva and Mongolia,
  filmed at 4501 frames/s. In kargyraa the false folds "close once for
  every two periods of closure" of the vocal folds. In the pressed drone of
  khöömei they move at the same period, in antiphase (primary).
- **Bailly, Henrich & Pelorson (2010):** a professional singer, filmed at
  2000 frames/s, with EGG. The ventricular folds approach on **every**
  glottal cycle, out of phase with glottal closing, but touch only on
  every second one. The perceived pitch is theirs (primary, pp. 3216–3217).
- **Levin & Edgerton (1999):** kargyraa "generally, though not always,
  assumes a 2:1 frequency ratio". Their example is vocal folds at 130.8 Hz
  (C3) and false folds at 65.4 Hz.

How often 3:1 occurs is not reported anywhere: it appears only as "in some
cases" or "possible". Sakakibara et al.'s (2002) 2×2-mass model gives 2:1
at moderate adduction of the false folds and 3:1 at others.

**The aryepiglottic view.** Some writers say the second vibrator is the
aryepiglottic folds rather than the ventricular folds:
- Grawunder's fieldwork (the 2009 book, read only in its blurb);
- Levin & Edgerton's list of possible structures.

Two things weigh against it for kargyraa:
- Esling's own laboratory files kargyraa under ventricular vibration
  (Moisik 2008, Table 2.6; Esling 2002 found Tibetan deep chant
  ventricular, with no trilling).
- Aryepiglottic trills, filmed, run at 42–101 Hz with heavy jitter and do
  not follow the glottal pitch (Moisik, Esling & Crevier-Buchman 2010).
  Kargyraa's undertone does follow it, locked at 2:1.

The synthesis follows the imaging: ventricular folds. A narrowed epilarynx
above them is assumed, but it does not vibrate.

### The phase

Bailly et al. (2010, p. 3217) and Bailly's thesis (2009, §7.2.1) give
the timing within the cycle (primary):
1. **Contact starts** during the open phase of one glottal cycle, after the
   glottis has opened.
2. **Contact ends** in that cycle's closing or closed phase.
3. **The false folds start to separate** at glottal closure.
4. Over the other glottal cycle they **approach without touching**.

Bailly's thesis (Table 7.1) splits the false folds' cycle in two:
- the approach that ends in contact, at 132 Hz;
- the approach without contact, at 177 Hz;
- the whole cycle, at 75 Hz.

Lindestad saw the ventricular closure come before the vocal folds' closure
(secondary).

No contact quotient for the false folds has been published. From the
phase description, the contact runs from about the middle of one glottal
open phase to just after its closure. That is about a third of a glottal
cycle, or **15–35 % of the false folds' own cycle** (inferred).

### What the false folds do to the flow

- **Fuks (1998):** inverse filtering through a Rothenberg mask. Every
  other flow pulse is about **13 dB** down, in time with the false folds'
  adduction. Peak flow is 460 mL/s in this mode against 280 modal, and
  the closed phase is 1.50 ms against 1.63 (secondary, via Bailly 2009,
  p. 66).
- **Lindestad (2001):** every second airflow pulse "considerably lowered"
  (abstract), though less than in Fuks's case (secondary).
- **Sakakibara (2001):** two flow peaks per period. In one of the two, the
  vocal folds do not close completely (primary).
- **Bailly (2010), the timing.** The glottal cycle in which the false folds
  close is the **longer** one:
  - its mean is 133 Hz against 158 Hz, a ratio of **1.19**;
  - its open phase is 1.5 ms longer on average (2.6 ms at most);
  - its open quotient is 11 % higher on average (23 % at most);
  - the DEGG closing peaks alternate too: the slower closing falls during
    contact (primary, p. 3218).
- **Bailly (2010), the aerodynamic model.** When the false folds touch,
  the pressure in the ventricle rises to the lungs' pressure and cancels
  the drop across the glottis. That alone makes the glottis open wider in
  that cycle and doubles the period. The model does **not** reproduce the
  longer cycle, which is measured but unexplained (primary, pp.
  3219–3220).

**Onset.** A kargyraa song often starts on a plain pitch (about 130 Hz)
and then switches to the doubling (Castellengo & Henrich 2014, p. 436).
Before the doubling settles, Bailly's singer made a rough sound in which
the false folds vibrated at the glottal period (Bailly 2010, p. 3218).

## 2. Pitch

| Source | Perceived (undertone) | Vocal folds |
|---|---|---|
| Lindestad 2001 (secondary) | 70 Hz | 140 Hz |
| Castellengo & Henrich 2014, Table 1, eleven recordings (Tuva, Mongolia, Tibet) | 61–92 Hz | 122–184 Hz (inferred: × 2) |
| Fuks 1998 | about an octave, from 49 Hz | |
| Bailly 2010 | 65–80 Hz, mean 72 | cycles of 133 and 158 Hz |
| Levin & Edgerton 1999 | 65.4 Hz | 130.8 Hz |

## 3. The tract and the melody

**Castellengo & Henrich (2014)** measured eleven recordings (primary):
- **The melody is F1's.** Kargyraa songs are "F1-based" or "F1–F2", unlike
  sygyt and khöömei, which are F2-based.
- **Which harmonics.** The melody harmonics are mostly the **6th to the
  12th** of the perceived pitch.
- **F1's range.** Per recording it covers about 320–920 Hz.
- **Two strategies for F2:**
  - some singers keep **F2 = 2·F1** (a "formant octave"), using only open
    vowels: /o ɔ ɑ a/;
  - others hold F2 at **1600–1800 Hz** with /ɛ ə a/ while F1 moves.
- **How it is tuned.** The mouth opens wider than in the F2 styles, the
  tongue is flat with its front still, and the tuning is done with **the
  jaw and the lips**. Levin & Edgerton (1999) describe the same: the mouth
  widened in steps, which shortens the tract and raises F1, isolating
  harmonics up to the 12th.

Other tract evidence:
- **Esling (2002), Tibetan deep chant** (which Moisik 2008 equates with
  kargyraa): a lowered larynx, an open pharynx, and a constricted ring at
  the ventricular level.
- **Milstein & Shipp (1994), Tibetan low chant:** F1 and F2 tuned to H5
  and H9 (abstract).

**Bandwidths.** No formant bandwidth has been measured in kargyraa. The
one figure in the overtone-singing literature is about 20 Hz for a tuned
formant, from Western overtone singers (Klingholz 1993, abstract). It is
not a kargyraa measurement.

## 4. Pressed phonation

**What was measured:**
- **Pressure.** Oesophageal (subglottal) pressure is **21.9 cmH₂O** in
  Fuks's vocal–ventricular mode against 18.0 modal (secondary), and
  "consistently higher" than modal and pulse register (primary).
- **Loudness.** 64–88 dBA at 0.3 m. The voice is "of strong intensity,
  sonorous and slightly pressed" (Lindestad, secondary).
- **Constriction.** Sakakibara (2001) found kargyraa's supraglottal
  constriction looser than the pressed khöömei drone's. No open quotient
  could be estimated, because the false folds contaminate the EGG and the
  flow.

**Jitter, shimmer and noise** have not been published for kargyraa or the
vocal–ventricular mode. Grawunder (1999), on his own non-native kargyraa,
found its cycles "more uniform" in length and amplitude than the pulse
register's. In his case, alternate cycles differed in length by only
0.2 ms.

## 5. The spectrum

- **Subharmonics:** added relative to modal voice, visible up to about
  1 kHz (Lindestad, abstract and secondary).
- **Partial density and envelope:** "Clear harmonic partials up to about
  4 kHz" with twice the partial density, the same envelope as modal voice
  on the same vowel, and more energy at 2–6 kHz (Fuks, abstract and
  secondary).
- **Flow spectra:** more power at 1–3 kHz (Sakakibara 2001).

**The level of the half-harmonics** has not been published. Suppose two
pulses per period have heights A and B. The harmonics of the vocal folds'
rate then scale with A + B, and the half-harmonics with A − B. Fuks's
13 dB puts B/A at 0.22, so the half-harmonics are about **4 dB under**
their neighbours at the source. Lindestad's smaller attenuation would put
them lower; Bailly's unequal cycle lengths raise them higher up (inferred).

## References

1. Bailly, L., Henrich, N. & Pelorson, X. (2010). Vocal fold and ventricular fold vibration in period-doubling phonation: physiological description and aerodynamic modeling. *J. Acoust. Soc. Am.* 127(5), 3212–3222. doi:10.1121/1.3365220
2. Bailly, L. (2009). *Interaction entre cordes vocales et bandes ventriculaires en phonation.* PhD thesis, Université du Maine. HAL tel-00416587.
3. Bailly, L., Henrich Bernardoni, N., Müller, F., Rohlfs, A.-K. & Hess, M. (2014). Ventricular-fold dynamics in human phonation. *JSLHR* 57, 1219–1242. doi:10.1044/2014_JSLHR-S-12-0418
4. Lindestad, P.-Å., Södersten, M., Merker, B. & Granqvist, S. (2001). Voice source characteristics in Mongolian "throat singing" studied with high-speed imaging technique, acoustic spectra, and inverse filtering. *J. Voice* 15(1), 78–85. doi:10.1016/S0892-1997(01)00008-X
5. Fuks, L., Hammarberg, B. & Sundberg, J. (1998). A self-sustained vocal-ventricular phonation mode: acoustical, aerodynamic and glottographic evidences. *KTH TMH-QPSR* 39(3), 49–59.
6. Fuks, L. (1998). *From Air to Music.* PhD thesis, KTH Stockholm.
7. Sakakibara, K.-I. et al. (2001). Vocal fold and false vocal fold vibrations and synthesis of khöömei. *Proc. ICMC 2001*, 135–138.
8. Sakakibara, K.-I., Imagawa, H., Niimi, S. & Osaka, N. (2002). Synthesis of the laryngeal source of throat singing using a 2×2-mass model. *Proc. ICMC 2002*, 5–8.
9. Castellengo, M. & Henrich Bernardoni, N. (2014). Interplay between harmonics and formants in singing: when vowels become music. *Proc. ISMA 2014*, Le Mans, 433–438.
10. Levin, T. C. & Edgerton, M. E. (1999). The throat singers of Tuva. *Scientific American* 281(3), 80–87.
11. Grawunder, S. (1999). *Obertongesang versus Kehlgesang.* Diplomarbeit, Halle-Wittenberg. Grawunder, S. (2009). *On the Physiology of Voice Production in South-Siberian Throat Singing.* Frank & Timme.
12. Esling, J. H. (2002). Laryngoscopic analysis of Tibetan chanting modes and their relationship to register in Sino-Tibetan. *Proc. ICSLP 2002*, 1081–1084.
13. Moisik, S. R., Esling, J. H. & Crevier-Buchman, L. (2010). A high-speed laryngoscopic investigation of aryepiglottic trilling. *J. Acoust. Soc. Am.* 127(3), 1548–1558. Moisik, S. R. (2008). MA thesis, University of Victoria.
14. Edmondson, J. A. & Esling, J. H. (2006). The valves of the throat and their functioning in tone, vocal register and stress. *Phonology* 23(2), 157–191 (abstract only).
15. Klingholz, F. (1993). Overtone singing: productive mechanisms and acoustic data. *J. Voice* 7(2), 118–122.
16. Milstein, C. F. & Shipp, T. (1994). Multiphonic vocalizations: a multifactor analysis of Tibetan monk chanting. ASA 128th meeting, 5aSP6.
17. Meyer-Eppler, W. (1953). Zum Erzeugungsmechanismus der Geräuschlaute. *Z. Phonetik* 7, 196–212. Flanagan, J. L. & Cherry, L. (1969). Excitation of vocal-tract synthesizers. *J. Acoust. Soc. Am.* 45, 764–769.
18. Klatt, D. H. (1980). Software for a cascade/parallel formant synthesizer. *J. Acoust. Soc. Am.* 67, 971–995.
