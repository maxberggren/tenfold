# Building a part

tenfold is a studio of pages. Each part of an electronic track has its own
page and is played with up to ten fingers on the trackpad, plus the keyboard
where that helps. The pages are 1 Drums, 2 Bass, 3 Chords, 4 Lead, 5 Arp,
6 Atmos, 7 FX, 8 Vocal, Shift+1 Sampler (the ninth part: the digits ran out),
and 0 Arrange, where the parts' clips become a track. Each page
is a different instrument, designed from scratch for what that part needs to
do in a great track.

This file is the contract between a part and the studio around it. Read
`src/part.h` next: the interface is documented there in full.

## The shape of a part

A part is one file, `src/<part>.cpp`, with a `make<Part>()` that returns a
`PartSpec`: name, a few words for the tab, the colour, default mixer channel
settings, a `PartControl` and a `PartEngine`.

- **PartControl** runs on the touch and UI threads, always under the session's
  lock. It turns touch frames and keys into `Command`s for its engine, keeps
  the page's state, and paints the page. It never needs locks of its own.
- **PartEngine** runs on the audio thread. It hears Commands, live or replayed
  from a clip, and renders the part's dry stereo signal, at most `kBlock` (32)
  frames at a time. It never allocates, locks, blocks or does I/O.

They share no memory. The only paths between them are Commands (control to
engine) and `publish(float *data)`, which is up to 256 floats the engine fills
about 60 times a second and the page reads as `pc.view->data`.

## What the studio gives you

- **One clock.** `ctx.transport()` on the audio side: bpm, beat position,
  samples per beat. On the control side, `ctx.beatAt(t)` / `ctx.timeAt(beat)`.
  4/4, beats counted since the studio started.
- **One key.** `ctx.harmony().tuning` (root, scale, mode) and `harmony.midi(degree, octave)`.
  Send scale **degrees** in Commands, not pitches, and resolve pitch in the
  engine at the moment you play. That way clips follow key changes.
- **The chord of the moment.** The Chords part calls `ctx.setChord(root, tones, n)`
  whenever it plays a chord (live or from a clip). Every engine can read
  `harmony().chordRoot / chordTones / chordSeq`, and every page reads
  `studio().chordRoot` and so on. Bass, Arp and Lead should be able to follow it.
- **The pump.** Drums call `ctx.pump(offset, strength)` on every kick. The
  mixer ducks every channel by its `PumpDepth`. `ctx.duck()` is the envelope for
  this block, for a part that wants to pump something of its own.
- **Clips: eight slots per part.** Everything a part sends is remembered for a
  few bars:
  - `ctx.capture(slot, t0, t1)` turns what was played between t0 and t1 into a
    loop and plays it at once. If nothing else is playing, that first loop sets
    the studio's tempo. Loops must be deliberate and visible: the player asked
    "what is triggering the loop in slot?" when timing alone (the tap-then-hold
    rule in `src/phrase.h`) made them. Make a loop only from an explicit act
    (Space, which the session handles), show a cue before it happens, and say
    what was captured.
  - `ctx.record(slot, maxBars)` / `ctx.stopRecording()` record armed from the
    next bar.
  - `ctx.overdub(on)` adds to the playing clip.
  - `ctx.launch(slot)`, `ctx.stop()` and `ctx.clear(slot)` work at the next
    bar unless `now`.

  Clips replay your own Commands through `command()`, with ids rewritten
  (bits above 1 << 24) and exact times. The studio sends Offs for anything a
  clip left on when it stops. Everything plays where it was heard: an onset
  laid back or pushed stays so, and one that reached the studio late (it
  plays at once) is kept where it played, not at the earlier time it
  carries. `Move` and `Param` too, so knob sweeps and slides loop.
- **A mixer channel per part:** level, pan, DJ filter, reverb and echo sends,
  pump depth, mute and solo. Set the defaults in `PartSpec::channel`. Any page
  can change any channel with `ctx.setChannel(part, Channel, value)`. The room
  (reverb, tape echo) and the master bus (filter, drive, glue, limiter) are
  shared, set with `ctx.setMaster(Master, value)`.
- **The console look.** `src/console.h` gives you the pad, tags, gauges,
  buttons, fingers, grid, the bar's beats and the clip strip. Use it, so every
  page reads as one panel: black glass, phosphor lines in your part's colour,
  condensed capitals.
- **Live input and audio loops.** `ctx.input()` on the audio side is the
  microphone for the block (null when there is none); `StudioView::input`
  and its latencies say how it is. A part that monitors it sets
  `PartSpec::monitors`, and the studio mixes it after the master limiter, so
  the voice isn't delayed by the limiter's look-ahead. A part whose loops
  are recorded audio rather than its Commands handles Space in `key()` and
  calls `ctx.takeAudio(…)`: the clip is one On flagged `Command::Sampled`,
  which the engine plays from audio it keeps, and everything else (slots,
  launch, layers, Ctrl+Z, the Song page) works as for any clip. It answers
  `loopBeats` / `resizeLoop` for the window's loop buttons. The Vocal page
  (`src/vocal.cpp`, `src/vocal.h`) is the example.
- **Files and links.** A file dragged onto the window or text pasted into
  it reaches `PartControl::dropped(what, ctx)`: the showing page's first, then
  any page that takes such things, which is then shown. Work that takes
  time (reading a song, fetching one) goes on a thread of the page's own,
  its result taken in `background()`; when `ctx.scripted()` says the
  session is played by a script, it is done before returning instead, so
  renders and tests hear it. A page that hands its engine memory the engine
  did not make (a loaded song) publishes it by an atomic pointer and frees
  it only once no voice plays it. The Sampler (`src/sampler.cpp`,
  `src/sampler.h`) is the example.
- **Background time.** `PartControl::background(t, ctx)` reaches every page,
  showing or not, with every frame and idle tick (about 60–125 Hz). Use it for
  anything that must keep going while another page is showing, such as an
  arrangement launching clips bar by bar.
- **Examples.** Every page in `src/` is a full working part; `src/drums.cpp`
  and `src/lead.cpp` show the framework in use. `src/electro.h` (supersaw,
  ladder filter, ADSR, drum voices) and `src/fx.h` (reverb, tape echo,
  saturator, grain cloud) are there to reuse or outgrow.

## Rules

1. **Own only your files:** `src/<part>.cpp`, and if you need one, a header
   `src/<part>.h` included only by it. Your tests go in `tests/<part>_tests.cpp`.
   Don't edit anything else: `part.h`, `studio.*`, `session.*`, `console.*`,
   `phrase.h`, `electro.h`, `fx.h`, `dsp.h`, the QML, or the `.pro`
   files. Copy what you need into your own file. If the framework truly lacks
   something, work around it and say so in your report.
2. **Ten fingers, all of them meaningful.** Design the page so one finger is
   instantly musical, and every further finger adds a dimension. The meaning
   can depend on landing order, on where a finger lands, or on what the others
   are doing. Use slide, pressure, contact size, twist (`Touch::angle`) and the
   click (`Frame::button`) where they help.
3. **Musical and pleasant first.** Everything must sit in the key and in time
   by default. Electronic, big, modern, polished: a producer should want to
   use it in a real track.
4. **The keyboard is yours,** except the digits, Escape, Tab, `?` and every
   Ctrl combination (the session's). **Space loops what was just played**, the
   same on every page, and the session does it for you. Don't handle Space.
   The player plays first, then presses Space. With nothing else playing they
   press it where the phrase would start again, and that loop sets the tempo.
   Over a playing loop, the bars that hold the phrase become a loop from the
   bar line. Over the page's own playing loop the take layers on top of it,
   as one loop; Shift+Space makes a new loop in its place instead. Space again
   while a take waits calls it off. The session finds
   the phrase from your onsets, so send every onset as `On` or flagged
   `Command::Quantize`, and flag an `On` that only carries on a sound already
   heard (a bar-line handover, a re-asserted latched layer) `Command::Continue`. Show it on your page from `pc.view->recording` (a take
   waiting) and the clip strip. F1–F8 launch your slots, Backspace stops your
   clip.
4b. **The same keys mean the same thing on every part page.** Letters are
   the page's own (notes, pads, chords, scenes, sounds by name). The rest is
   shared, so a player who learned one page knows the others:

   | Key | On every part page |
   |---|---|
   | ← → | the sound: kit, preset, scene, FX set (step through them) |
   | ↑ ↓ | octave or register (FX: riser length). For three seconds after Space the session takes them for the loop's length; Drums, with no octave, take them for it at any time (`ctx.resizeLoop`), and for the roll rate only while keys hold a roll |
   | Shift+← → (or [ ]) | the rhythm: swing, gate pattern, arp pattern, trance gate, FX drop style |
   | - and + (or =) | tone: darker, brighter (the page's filter or brightness) |
   | Enter | play and pause everything: the session's (it never reaches a page) |
   | Shift+Enter | hold or latch (FX: drop on the next bar; Drums: keep what rolls). The page hears it as plain Return: the session translates |
   | F10 | overdub on or off. The page hears it as Shift+Return |
   | the key left of 1 (§ on Nordic and Mac layouts, ` on US; or \\) | follow the Chords page, on or off |
   | Delete | let go or reset (latched layers, held chords, drum sounds) |
   | F1–F8 · Shift+F1–F8 · Backspace | launch a slot · clear it · stop the loop at the bar |

   The primary keys work without AltGr on Nordic layouts too (on a Swedish
   keyboard [ ] \\ need AltGr, = needs Shift and ` is dead): match
   Qt::Key_Minus and Qt::Key_Plus (and Key_Equal), Qt::Key_section and
   Qt::Key_QuoteLeft (and Key_Backslash), Shift with Key_Left/Key_Right
   (and Key_BracketLeft/Right). Chips show the primary key.

   Space, Shift+Space and every Ctrl key are the session's. Old keys for
   the same things may stay as second keys, but help() lists the shared
   ones first.

4c. **The mouse can do everything the keys do.** A setting with only a key
   gets a control: on the pad where a finger would use it, or as a chip in
   the band under the pad (`console::bandRow(pc, 0|1, x0, x1)`: row 0 the
   clips and the page's main choice, row 1 its settings). Draw chips with
   `console::chip` / `console::chips` into a `console::Hotspots` cleared at
   the top of paint(), and answer `PartControl::click(at, ctx)` from it;
   `console::clips(…, &spots)` and `console::clickClip` make the clip slots
   clickable. A chip shows its key (`SWING +  ]`). Scripts click with
   `button T X Y` (pad coordinates, y below 0 is the band) and right-click
   with `rbutton T X Y`. Drums is the example. A page may also open a menu
   of its own on a right click (`PartControl::menu`, `menuClick`,
   `closeMenu`, drawn by paint()); Arrange's clip and section menu is the
   example.

5. **Clips stay lean:** send `Move`/`Param` only on real change (more than about
   0.5%) and at most about 60 Hz per finger. A clip holds 2048 events.
6. **Ids** stay below `1 << 24`.
7. **`leave()`** ends every sound a finger or key holds. Clips keep playing.
7b. **Songs.** `save(out)` writes the page's own settings for Ctrl+S, and
   `load(in, ctx)` takes them back on Ctrl+O, every value checked, a missing
   one the default. The studio saves the clips. A page whose clips play
   audio it keeps itself writes that audio with `saveAudio` and takes it
   back with `openAudio`, which may change the clips' commands to name it
   anew. The Vocal page is the example.
8. **CPU:** the whole studio runs in one 128-frame callback (2.7 ms). Keep your
   part under about 6% of that at its busiest (`tenfold bench` and your own
   renders report load). No allocation on the audio thread.
9. **Paint** inside `pc.pad`, plus the band just below it (about 44 px from
   `pad.bottom()`) for your clip strip and readouts. `help()` returns one
   `gesture\twhat it does` per line: it is the page's guide (the `?` key).
   `status()` is one short line on what the fingers are doing now.
10. Comments explain why, in full sentences. Commit with short imperative
    messages.

## Trying it

```sh
bin/build                      # build/tenfold
bin/test                       # every test, yours included
build/tenfold render script.tf out.wav     # a scripted performance; see src/offline.h
build/tenfold screenshot out.png script.tf --page N --at SECONDS
build/tenfold bench
uv run --with numpy --with matplotlib python tools/analyze.py out.wav   # spectrum and levels
```

Scripts drive the real session: `page`, `down`, `move`, `glide`, `up`, `tap`,
`click`, `key` (Qt key names: `A`, `F1`, `Shift+Left`, with optional `down` or
`up`), `tempo` and `end`. A render reports the tempo and what every slot
holds. Listen with your analysis: levels, spectra, timing against the beat
grid, clipping. A screenshot shows the page as the player sees it.
