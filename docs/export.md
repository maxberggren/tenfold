# Export

EXPORT, beside Save song (or Ctrl+E), renders the song to a folder of files
another studio can use. The rendering happens offline, on a thread of its
own, while tenfold keeps playing. It runs in one pass through a studio and
session of its own, built from the song as Ctrl+S would save it (src/export.cpp).

## What is written

| File | What it is |
|---|---|
| `<prefix>00 Mix - 124 bpm A aeolian.wav` | the mix as tenfold plays it, master bus and all |
| `<prefix>01 Drums - …wav` … `09 Sampler - …wav` | a stem per part: its channel (level, pan, filter, pump, the Song page's mutes and moves), its own share of the room, and the master as chosen |
| `<prefix>01.01 Drums kick - …wav` … | voices, when *Individual voices* is on (see below) |
| `<prefix>00 MIDI - …mid` | a Standard MIDI File, type 1 |

The two digits sort the files the way the pages are numbered, with the voices
under their part. The name carries the tempo and the key, for people and for
tools that read names. The key uses plain `#` and `b`, as song files do.

### Settings

- **Length.** *Song* plays the Song page's arrangement once, from its first
  bar to its last. *Loops playing* plays whatever loops are playing now, for
  N bars, all started together on bar 1. With no song, it uses the loops.
- **Parts.** One WAV for each part that has loops; any part can be turned off.
- **Individual voices**, **Full mix** and **MIDI file** can each be turned
  on or off.
- **Tails.** On: rendering continues past the last bar until every file has
  been quiet (below −80 dBFS) for half a second, at most 20 s. Every file is
  then cut on the next bar line. Off: every file ends exactly on the song's
  last bar line, which suits loops.
- **Pre / post-fader.** Post-fader, the default, is the part as heard in the
  mix. Pre-fader is the engine's own dry sound before the channel: no level,
  mutes, pan, filter, pump, sends or room. Use it for re-mixing from scratch.
  Pre-fader stems no longer add up to the mix.
- **Master glue** (post-fader only). The master bus has a glue compressor, a
  low-end compressor and a look-ahead limiter. They react to the whole mix,
  so running each stem through its own copy would compress each one
  differently. With the setting on (the default), tenfold works out their
  gain at every sample from the whole mix and applies that same gain to every
  stem. The stems then add up to the mix file: the test measures the
  difference at about −90 dB. Off gives clean stems: the master's level and
  filter sweeps, no dynamics, and the headroom that leaves, for when you will
  glue them yourself.

The master's tape drive is the one piece that can't be shared this way: it
is a curve, not a gain. Songs leave it at 0. With it turned up, the stems add
up to the mix before the drive.

## Alignment

- Sample 0 of every file is beat 1 of bar 1. Nothing comes before it. A bar
  of lead-in is rendered and thrown away, because the Song page launches
  clips a beat ahead. The master's 63-sample look-ahead is taken off every
  file alike.
- Every file is the same length and a whole number of bars, tail included.
- Every file has the studio's sample rate (48 kHz), is 24-bit PCM stereo,
  and has TPDF dither.
- Each WAV carries two metadata chunks:
  - `acid`: the tempo, the number of beats, and 4/4. The one-shot flag is
    off and the stretch flag is on. The tonal parts and the mix also carry
    the key's root as a MIDI note (60 + its pitch class). Drums, FX and
    Sampler carry no root.
  - `bext` (Broadcast WAV, version 1): the description (part, tempo, key,
    beats), "tenfold" as the originator, the date, and a TimeReference of 0.
    Every file says it starts at the same moment, so a DAW that places files
    by their timestamp (Pro Tools, Logic, Reaper, Nuendo) lines them up by
    itself.

### In Ableton Live

1. Set Live's tempo to the one in the file names.
2. Drop the WAVs (all at once, onto new tracks) at 1.1.1 in the Arrangement.

They line up because each one starts on the downbeat and they are all the
same length. With *Auto-Warp Long Samples* on (Live's default), Live warps
each file and guesses its tempo. Starting on a downbeat with a whole number
of bars is what that guess needs. If Live guesses twice or half the tempo, use
the clip's ×2 or ÷2, or turn *Auto-Warp Long Samples* off (Preferences →
Record/Warp/Launch) and the files play unwarped at the set's tempo, sample for
sample. Short files, such as a few bars of loops, are taken as 1, 2, 4, 8 or
16 bars, which is exactly what they are.

## Individual voices

Each voice is a second copy ("twin") of the part's engine
(`PartEngine::twin`, `Studio::addVoice`). The twin hears every command the
part hears, but only the commands that belong to its voice. It plays in the
part's harmony and pump, and through the part's channel, into a room of its
own. The part itself still sets the chord the others follow and drives the
pump. So a voice sounds as it does inside the mix, and all of them come out
of the same single pass.

| Part | Voices |
|---|---|
| Drums | one per pad of each kit used: kick, snare, hat… (a hang's fields, a slit drum's tongues and so on, named as the page names them) |
| Sampler | the song split into its separated stems: drums, bass, vocals, other. This needs S (separate) to have been run, so the stems are cached. A stem file that comes out silent, because the stems were never separated, is left out. |
| Bass, Chords, Lead, Arp, Atmos, FX | the layers Space put on their loops (layer 1, 2…), when a loop has more than one. A layer keeps every knob move, since those shape the whole part. |
| Vocal | none: its takes share one tape, which the engine writes to, so a second copy can't safely play it |

Some splits can't be done at all:

- A chord split into its notes: the engine voices the chord itself.
- A lead's harmony voices split from the line.
- An arp split by step.
- The drums' internal links between pads, such as a closed hat choking an
  open one, or one kit chopping another, because each voice is its own drum
  machine.

The drum voices add up to the drums stem in level. Sample for sample they
differ wherever a drum is noise, because each copy draws its own noise.

## MIDI

The MIDI file is a type 1 Standard MIDI File at 480 PPQ:

- The first track holds the tempo, 4/4, and the key signature. For the
  seven diatonic modes that is the parent major key's sharps or flats, marked
  minor for aeolian. Other scales get no key signature.
- After that comes one track for each part whose notes mean something as
  notes. Tick 0 is bar 1, like sample 0 of the WAVs.

Each engine reports the notes it actually sounds (`AudioContext::note`), so
the MIDI follows the chord changes, the key moves and the voicings that
tenfold heard:

- **Drums** go on channel 10, as General MIDI drums by pad: kick 36, snare
  38, clap 39, closed hat 42, 808 35, rim 37, shaker 70, open hat 46, toms
  45/47/50, conga 63, crash 49, ride 51, cowbell 56, zap 81. A held 808 lasts
  until it is let go.
- **Bass**: each lane's note, from On to release. A followed chord change or
  a glide to a new degree starts a new note.
- **Chords**: the voicing's notes, from the moment the chord is played until
  it is let go. A stab's rhythm gate belongs to the sound, not to the notes.
- **Lead**: the line's notes and its harmony voices, to the nearest
  semitone. A slide becomes a run of semitones.
- **Arp**: every step as played, with its gate length.
- **Atmos, FX, Vocal and Sampler** get no track. Their sound is texture,
  noise, sweeps or recorded audio, and as notes it would mislead.

## Research notes

- **What Live does on import.** Live analyses every file it imports. It
  decides whether a file is short or long and applies one of two
  preferences. *Auto-Warp Long Samples* is on by default; Live then warps
  the file, estimates its tempo, and puts a warp marker on the first beat
  of each bar. *Loop/Warp Short Samples* defaults to Auto. Short loops of an
  even length are taken as 1, 2, 4, 8 or 16 bars, and the tempo is worked out
  from that. The estimate can come out at double or half the real tempo,
  which the ×2 and ÷2 buttons fix. Live saves its analysis, including warp
  markers, in an `.asd` file beside the sample.
  ([Live manual: Audio Clips, Tempo, and Warping](https://www.ableton.com/en/live-manual/12/audio-clips-tempo-and-warping/))
- **Stems in practice.** For stems, Ableton's help article recommends
  setting the project to the stems' tempo, selecting the clips, and choosing
  Reset Warp Markers. Experienced users turn *Auto-Warp Long Samples* off
  before importing stems that share a start point. Stems that start before
  bar 1 throw the grid off. So the files should start exactly on the
  downbeat, all be the same length, and say their tempo.
  ([Ableton: Importing and exporting stems](https://help.ableton.com/hc/en-us/articles/360000843404),
  [Ableton forum](https://forum.ableton.com/viewtopic.php?p=1820188))
- **ACID metadata.** The `acid` chunk is the Sony/Magix ACIDized-WAV
  extension; there is no public specification. Its layout is known from
  libsndfile's reader and from forum notes: flags (0x01 one-shot, 0x02 root
  set, 0x04 stretch, 0x08 disk-based), the root note, two unknown fields,
  the beats, the meter, and the tempo as a float. ACID, Sound Forge, FL
  Studio, Reaper and others read it. I found no source that says Live reads
  it, and old reports say it doesn't. tenfold writes it anyway, because it
  costs nothing and other tools use it. In Live, what makes the files line
  up is the downbeat start, the whole bars and the tempo in the names.
  ([libsndfile wav.c](https://github.com/libsndfile/libsndfile/blob/master/src/wav.c),
  [KVR: Acidized wav format](https://www.kvraudio.com/forum/viewtopic.php?p=3061898))
- **Broadcast WAV.** The `bext` chunk (EBU Tech 3285) has a TimeReference:
  the sample count since midnight at which the file starts. All of tenfold's
  files say 0, so DAWs that place files by timestamp line them up.
- **REX and Apple Loops** carry tempo and slice data too. Both are
  proprietary (REX through Propellerhead's SDK, Apple Loops in AIFF/CAF
  chunks), and neither suits whole stems. tenfold writes neither.
