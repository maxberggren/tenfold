# QA report: tenfold, 2026-11-27

I tested the app at `6f14619` (master as this branch began), on Linux with the offscreen platform. The dev test tier passed: 210 tests in 25 s.

## Status after the fixes

Each bug below is fixed on this branch, with a regression test, unless noted otherwise.

| Bug | Fix | Test |
|---|---|---|
| 1 ⏎ labels | The chips say ⇧⏎ / F10, and the help texts match | `FrameworkTests::returnKeysDoWhatThePagesSay` (also scans the sources for a bare ⏎/↵) |
| 2 Song page Shift+Space | Shift+Space no longer undoes the song | `ArrangeTests::shiftSpaceLeavesTheSongAndSpaceNeedsALoop` |
| 3 pause forgets loops | A pause keeps what it remembered of parts that aren't playing | `FrameworkTests::aLoopMadeWhilePausedKeepsThePausedLoops` |
| 4 paused song opens silent | The song file keeps each part's `paused` slot and opens paused; Ctrl+P plays it | `FrameworkTests::aSongSavedPausedOpensReadyToPlay` |
| 5 3-bar Shift+Space loop | A new loop takes only what was played since `m_loopSince`; loops are 1, 2, 4, 8 or 16 bars | `FrameworkTests::aNewLoopTakesOnlyThePlayingSinceTheLoop` |
| 6 FX help | Space and Shift+Space described the right way round | `RisersTests::spaceLayersAndShiftSpaceMakesANewLoop` |
| 7 Sampler slot keys | F1–F8, Shift+F1–F8 and Backspace now work | `SamplerTests::theSlotKeysStopLaunchAndClear` |
| 8 Lead F11 | Removed from the help (the window takes F11 for full screen) | text only |
| 9 bass LOOP marker | Moved to the foot of the column | screenshot |
| 10 start over | The cleared song is kept in `before-start-over.tenfold`; the notice names Ctrl+Shift+O, which opens the newer kept song | `FrameworkTests::startingOverKeepsTheOldSong` |
| 11 panel scroll bar | Readouts go compact while the pad isn't taken; the column keeps clear of the bar's width | screenshots at 1320×900 / 1320×800 |
| 12 band at the minimum size | **Partly fixed.** Text shrinks to ¾ before eliding (LATCH, FOLLOW… read at 800×500). At 640×440 the chips are still elided. The minimum size stays, because tiled window managers ignore it (see the "any size" notes); more needs a band redesign. | screenshots |
| 13 empty song | Space with no loops records nothing and says so; "1 bar" | in the bug 2 test |
| 14 stale "Paused" | Gives way to "Playing" once something plays | in the bug 4 test |
| 15 tempo error | **Not a bug.** My probe used `"bpm"`; the field is `"tempo"`. Out-of-range tempos open clamped. | — |
| 16 Shift accents | `console::unshifted` on press and release in Drums and Lead | `DrumsTests::shiftedPunctuationPlaysAndLetsGo` |
| 17 Bass `\` chip | No key shown for sounds past P | text only |
| 18 export of nothing | Fails, says why and removes its empty folder; a paused song exports its loops | `ExportTests::anExportThatWritesNothingFails` |

## How I tested

- **Driving the real session headlessly.** I wrote a throwaway QtTest suite with about 70 probes. It uses `testkit::play`, `watch` and `reopen`: scripted taps, keys, voice and `drop` lines go through the real `Session` and `Studio`. The suite then prints the clips, the notice and the pause state. The suite is not committed; a copy is in the session's shared notes folder as `qa_probe_tests.cpp`. To run it, add it to `tests/tenfold_tests.pro` and run `TENFOLD_TESTS=QaProbeTests,QaProbe2Tests,QaProbe3Tests build/tests/tenfold_tests`.
- **Screenshots.** I used `tenfold screenshot` on every page, at 1320×900, 1000×450, 800×500 and 640×400. I also captured the guide (`--help-page`), the export dialog (`--export`, full size and 800×500), tutorial step 3, and the pad in standby and fault states.
- **Reading the source.** I read the window (`Main.qml`, `HelpPage.qml`, `ExportDialog.qml`) and `Session::key`. A read-only pass over every page compared each page's help text, chips and README claims with its `key()` handler. Where an item below comes only from reading code, it says so.

In every repro below, `BEAT` means this script prefix. It plays a one-bar kick and snare on Drums and loops it at 120 BPM:

```
page 0 1
tap 0.5 0.05 0.5
tap 1.0 0.25 0.5
tap 1.5 0.05 0.5
tap 2.0 0.25 0.5
key 2.5 Space
```

and `BASS` adds a one-bar bass loop over it:

```
page 3.0 2
tap 4.55 0.1 0.5
tap 5.05 0.3 0.5
tap 5.55 0.2 0.5
tap 6.05 0.1 0.5
key 6.7 Space
```

---

## Bugs, most severe first

### 1. The ⏎ labels on Bass, Atmos and Arp predate the Enter = pause change; pressing ⏎ as labelled pauses the whole song

`Session::key` changes three keys before any page sees them (`src/session.cpp:708-724`):

- plain Enter pauses every loop;
- Shift+Enter reaches the page as plain Return;
- F10 reaches the page as Shift+Return.

Drums, Chords and Lead were relabelled ("HOLD ⇧↵", "OVERDUB F10"). These three pages were not:

| Page | Chip says | What the key actually does |
|---|---|---|
| Bass | `LATCH ⏎` (`src/bass.cpp:1840`) | Enter pauses everything |
| Bass | `OVERDUB ⇧⏎` (`src/bass.cpp:1841`) | Shift+Enter **arms a bar recording into slot 1** ("Bass records into slot 1 from the next bar…") |
| Atmos | `HOLD ⏎` (`src/atmos.cpp:5850`) | Enter pauses everything |
| Atmos | `OVERDUB ⇧⏎` (`src/atmos.cpp:5855`) | Shift+Enter turns **Hold** on |
| Arp | `OVERDUB ⇧⏎` (`src/arp.cpp:5127`) | Shift+Enter **latches** |

Bass help also says "Enter or F9" records a loop (`src/bass.cpp:736`). Drums help says "Shift+Enter overdub on or off" (`src/drums.cpp:726`); overdub there is F10.

- **Repro:**
  - Run `BEAT` + `page 3 2`, `down 3.2 1 0.3 0.5`, `key 3.4 Return`, `up 3.6 1`. Result: `paused=1`, notice "Paused · Ctrl+P plays it again".
  - Run `BEAT` + `page 3 6`, `key 3.4 Shift+Return`. Notice: "Hold: layers held a moment stay when lifted", and overdub stays off.
  - Run `BEAT` + `page 3 5`, `key 3.4 Shift+Return`. Notice: "Latched: the pattern runs on when you lift", and overdub stays off.
- **Expected:** the chips say `⇧↵` for hold or latch and `F10` for overdub, and the help text matches.
- **Actual:** a player who presses the labelled key stops the whole performance, or switches the wrong toggle.

### 2. Shift+Space on the Song page throws away the song

On every other page, Shift+Space means "a new loop" (HelpPage.qml lists it among the global keys). On the Song page, `case Qt::Key_Space` falls through into the `O` case, and Shift calls `undoSong()` (`src/arrange.cpp:431-437`). That stops the song and goes back to the previous version, and nothing redoes it. The Song page's help names only "Space or O" and "Shift+O".

- **Repro:** `BEAT` + `BASS` + `page 7 0`, `key 7.2 I`, `key 7.4 I` (a 40-bar song is written), `key 9 Shift+Space`. Notice: "No earlier song to go back to". With an earlier song, it would have gone back to that one.
- **Expected:** Shift+Space does nothing here, or the same as Space.
- **Actual:** it is a silent, one-way undo of the arrangement.

### 3. A loop made while paused makes the pause forget every other loop

- **Repro:** `BEAT` + `key 3 Ctrl+P`, `page 3.1 2`, four bass taps from 3.3 s to 4.8 s, `key 5.3 Space`, `key 7 Ctrl+P`, `key 8 Ctrl+P`.
- **Result:** `[p0 play=-1] [p1 play=0]`, `paused=0`, notice "Playing". The drums never come back. Only the new bass loop plays and is paused.
- **Expected:** the new loop joins what is paused, or starts with everything remembered.
- **Suspect:** `Session::toggleRecording` and `m_pausedSlots`. A take landing while paused should either clear the pause and restart the remembered slots, or join them (`src/session.cpp`, around `togglePause`).

### 4. A song saved while paused (or after Ctrl+Backspace) opens silent, and Play says there is nothing

- **Repro:** `testkit::reopen(BEAT + "key 3 Ctrl+P\nend 4\n", …)`, then press Ctrl+P in the opened session. The same happens with `key 3 Ctrl+Backspace`.
- **Result:** the drum clip is there (`s0:4b/4ev`), but `play=-1` and `paused=0`. Ctrl+P says "Nothing to play yet: play, then Space makes a loop".
- **Expected:** the saved slots come back paused, so that Ctrl+P plays them. At the least, the message should not say there is nothing when slots hold loops; the user has to find F1 on each page.
- **Suspect:** the pause memory is not saved (`Session::saveSong` / `openSong`), and the "Nothing to play" branch of `togglePause` checks the pause memory, not the slots.

### 5. Shift+Space just after a first loop makes a 3-bar loop that re-loops notes already in slot 1

- **Repro:**
  1. Play the drums script: `tap` at 0.5, 1.0, 1.5 and 2.0 (pads 0.05/0.25), then `key 2.52 Space`.
  2. Play the same pattern on other pads: `tap` at 3.1, 3.6, 4.1 and 4.6 (x 0.55/0.75), then `key 5.12 Shift+Space`.
- **Result:** `s1:12b/8ev`, notice "Looping 3 bars in slot 2". The new loop holds the four hits that went into slot 1 as well as the four new ones.
- **Expected:** a 1-bar loop of what was played after the first loop landed. Lengths should be 1, 2, 4, 8 or 16 bars, as the guide says.
- **Suspect:** Shift+Space (layer = false) does not respect `m_loopSince[part]` when it picks the window of hits, and the bar count is not snapped to a power of two (`src/session.cpp`, `toggleRecording` and `Cycle` detection). The probe that made ten Shift+Space loops in a row got the same 3-bar first result: `s1:12b/8ev`.

### 6. The FX page's help has Space and Shift+Space backwards

`risers.cpp:328` says "Shift+Space layers it onto the loop playing". The FX page does not handle Space, so the session's default applies: Space layers.

- **Repro:** `BEAT` + `page 3 7`, `tap 4.55 0.1 0.1`, `key 6.7 Space`, `tap 8.55 0.3 0.1`, `key 10.7 Space`. Notice: "Layered 1 bar onto slot 1".

### 7. The Sampler advertises slot keys it doesn't have (from reading code)

`src/sampler.cpp:1469` lists "F1–F8 · Shift+F1–F8 · Backspace". `SamplerControl::key` (`src/sampler.cpp:1147-1227`) handles none of them, and neither does the session, so all three keys do nothing on the Sampler.

### 8. Lead: F11 cannot toggle Follow

Lead help says "§ (` on US; or \, F11)" (`src/lead.cpp:3735`). The window's F11 full-screen shortcut (`src/Main.qml:138`) takes the key first, so F11 makes the window full screen instead.

### 9. The bass LOOP marker is drawn over the sound's description line

With a layered or looped bass lane, the word "LOOP" is drawn at `r.top() + 26` of the column (`src/bass.cpp:1497`). That lands on the line "ROLLING SAW, TECHNO, TRANCE · MORPH 30% · SLIDE 45 MS", and the two overlap ("TRANCE LOOP MORPH").

- **Seen in:** `tenfold screenshot p2.png <BEAT+BASS script> --at 7.5 --page 2`, at every window size.

### 10. Ctrl+N and the tutorial's second Ctrl+T destroy everything with no way back

Start over cannot be undone: after Ctrl+N Ctrl+N, Ctrl+Shift+Z says "Nothing on Drums to take back". It is not autosaved either; the autosave is written only on quit (`src/backend.cpp:240`). The tutorial's second Ctrl+T clears every loop the same way. Both ask first, but the warning is a status-line notice, which is easy to miss while playing.

- **Repro:** `BEAT` + `key 3 Ctrl+N`, `key 3.2 Ctrl+N`, `key 3.4 Ctrl+Shift+Z`. Result: no clips.
- **Suggestion:** autosave to `last.tenfold` (or a `before-start-over.tenfold`) before clearing, so Ctrl+Shift+O brings it back.

### 11. The right panel overflows at the default window size when the pad isn't taken

In the first screen a new user sees (`--pad standby`, 1320×900), the right column scrolls. Its scroll bar covers the right edge of QUANTISE, hiding its "CTRL G" hint, and of START OVER, EXPORT and SAVE SONG. The same happens at 800×500 and smaller, where the scroll bar also overlaps the TRANSPORT box.

- **Suspect:** `src/Main.qml:363-375` (`panelView.scrolls`).
- **Suggestion:** reserve the scroll bar's width, or shrink the button rows (`Layout.minimumHeight: 30`) before scrolling.

### 12. At the minimum window size the page band is unreadable

The window allows 640×440 (`src/Main.qml:16-17`). At 640×400–440, the Bass band's chips are cut to "OC…", "L…", "D…" and "FO…". The rail headings ("F…", "GR…") and the slot numbers overlap. Only the right panel reflows; the page's band does not.

- **Suggestion:** raise the minimum to about 800×500, where it still reads, or drop the chip captions in favour of their keys at narrow widths.

### 13. Song page: Space with nothing made records a 1-bar empty song, "Song: 1 bars"

- **Repro:** `page 0 0`, `key 0.5 Space`, `key 3 Space`. Result: `songBars()=1`, notice "Song: 1 bars · P plays it" (`src/arrange.cpp:2538-2539`).
- **Expected:** "Nothing to record: make a loop first", as I and B already say. The noun also needs singular/plural handling.

### 14. A stale "Paused" notice after launching a slot while paused

- **Repro:** `BEAT` + `key 3 Ctrl+P`, `key 3.2 F1`. The drums play (`paused=0`), but the notice still reads "Paused · Ctrl+P plays it again".

### 15. A song file with an out-of-range tempo says "This song has no tempo"

- **Repro:** open `{"tenfold":1,"bpm":100000}` (or `-5`). The error is "This song has no tempo" (`src/session.cpp:2099`).
- **Expected:** "Its tempo (100000 BPM) is outside 60–200". Tempo is clamped to 60–200 everywhere else (Ctrl+Shift+↑ ×60 → 200; `tempo 0 20` → 60).

### 16. Shift accents on punctuation keys don't play, and a note can stick (from reading code)

Drums ("M , . / and J K L ;", Shift accents, `src/drums.cpp:676`) and Lead (`src/lead.cpp:4196-4201`) map only the unshifted key codes. With Shift held, `,` `.` `/` `;` arrive as `<` `>` `?` `:`, so nothing plays, and Shift+/ opens the guide. If Shift goes down while such a key is held, the release comes back under the other code. The roll or note then sounds until the page is left. Bass maps them (`src/bass.cpp:1238`).

### 17. The Bass SQUARE chip shows `\`, which toggles Follow instead (from reading code)

The chip is at `src/bass.cpp:1833`; the handler is at `src/bass.cpp:518`, where Backslash is Follow.

### 18. Export can report success having written nothing (API only)

`exportSong` with `parts = {"Nope"}`, `mix = false`, `midi = false` returns `ok=1`, `files=[]`. Its note also says "for 1000 bars" when `bars = 1000`, and "for 0 bars" for `bars = 0`, though it rendered 1. The dialog's own guard (`src/backend.cpp`, "Nothing chosen to export") only checks an empty list, so this can't be reached from the UI today. It is cheap to guard in `exportSong` too: fail when `files` is empty, and clamp `bars` before the note is written.

---

## Unintuitive UX

1. **The Enter key changed meaning, but the pages' language didn't.** Beyond the wrong chips (bug 1), each page's help still says "Return" for hold, latch or drop. The Song page's DROP card says "RETURN" (`src/arrange.cpp:4196`), but Return pauses. A first-time user reads "RETURN" and presses Return.
   - **Suggestion:** one pass replacing "Return" with "⇧↵" in every page's help and card, plus a test that greps the chips for a bare `⏎` or `↵`.

2. **"§" is printed on keys most users don't have.** FOLLOW §, KEY § and the tutorial's "Press § (the key left of 1)" all assume a Nordic or Mac keyboard. The handlers also accept `` ` `` and `\`, but no label says so.
   - **Suggestion:** draw the key by the layout (`` ` `` on US/UK), or write "§/`".

3. **Ctrl+Z works per page, which isn't what the button or the comment suggest.** The panel button says just "UNDO LOOP", and `session.h`'s header comment says "take back the newest loop (on any page)". In practice, Ctrl+Z on Bass right after making a Drums loop says "Nothing on Bass to take back"; on the Song page it takes the newest anywhere. The in-app guide is right, but the button is what people click.
   - **Suggestion:** label it "UNDO <PAGE> LOOP", or make the button always take the newest anywhere. Fix the header comment either way.

4. **Quitting doesn't ask, even with unsaved loops.** Ctrl+Q and the window's close button quit at once unless the mix is recording (`src/Main.qml:84-95`). The work is autosaved and Ctrl+Shift+O brings it back, but nothing on quit or start says so.
   - **Suggestion:** on the first start after an unsaved quit, show "Your last session is kept: Ctrl+Shift+O".

5. **Two RECORD buttons on the Sampler page.** The page's RECORD (the microphone into the sampler) sits in the middle, and the window's RECORD (the mix to a file) sits in the side panel, in different colours but with the same word.
   - **Suggestion:** "RECORD A SOUND" on the Sampler, or "RECORD MIX" in the panel.

6. **OPEN FILE and RECORD on an empty Sampler look like empty boxes.** Their captions sit top-left in a tall box with nothing in it.
   - **Suggestion:** centre the captions and add a one-line hint, such as "a WAV, MP3, FLAC…" or "from the microphone".

7. **The export dialog's − and + look like empty boxes.** Their glyphs sit in the top-left corner of tall buttons. CLOSE and EXPORT sit on the bottom edge of their buttons. At 800×500 the scroll bar touches CHOOSE….
   - **Suggestion:** centre the glyphs and give the bar some inner padding.

8. **"LOOPS PLAYING · 4 BARS" in the export dialog** is the export length, but it reads as a description of the loops (which are 1 bar here).
   - **Suggestion:** "LOOPS PLAYING, EXPORTED FOR 4 BARS".

9. **Tempo from very slow tapping.** Four alternating hits 3 s apart set 160 BPM: each hit becomes half a bar, and the loop is 4 bars with 2 hits. A player tapping a slow pulse would more likely expect 80 or 40 BPM.
   - **Suggestion:** below about 40 BPM per hit, prefer the slower tempo, or let ↑↓ step the tempo, as they step the length.

10. **Space with only one or two hits** says "Press Space where the phrase starts again", which is good. But it gives no hint about how many hits are needed.
    - **Suggestion:** "Play the phrase at least twice, then Space."

11. **The guide's global list skips the Sampler.** "1 – 8: the parts…" never mentions Shift+1 (`src/HelpPage.qml:15`).

12. **The README's counts are out of date.**
    - Lead: "fifteen sounds", but there are 24 (`src/lead.h:290`).
    - Drums: "fifteen kits", but there are 22.
    - Drums help: "five world kits", but there are 12.
    - Vocal: "five from further afield", but it describes 7.

13. **F10 is not overdub everywhere,** though the README's "same keys on every page" table says it is:
    - FX and Song: F10 drops.
    - Vocal: F10 only prints a notice.
    - Visual: F10 opens the second window.

14. **Atmos's band at the default size is very dense.** Ten scene chips with 7-px captions and five Shift-scene chips sit under the slots. It is the hardest page to read at a glance.
    - **Suggestion:** move the scene picker into the pad area (like the kit row on Drums), or onto a second band page.

15. **The status line is cut off on Vocal** ("…FINGERS ADD COPIES OF IT AT THEIR NOTES" ends in "FI…" at 1320 px). The "LATCH ⇧…" chip is cut too. The vocal status is three sentences long; the first clause is enough there.

---

## Tested and working

- **Pages.** Every page draws at 1320×900 with a loop playing: Drums, Bass, Chords, Lead, Arp, Atmos, FX, Vocal (no-mic card and demo singer), Sampler (empty and loaded), Song and Visual. Tab and Shift+Tab cycle through all 11, including Sampler and Visual.
- **Loops.**
  - Space with nothing played: "Play something, then Space loops it", and no loop.
  - The first loop sets the tempo (120 BPM).
  - Over a loop, Space takes the bars just played.
  - Layering: "Layered 1 bar onto slot 1".
  - Shift+Space with all eight slots full: "Every slot is full: clear one first".
  - ↑ eight times after Space stops at the longest ("as long as it goes"), and ↓ at the shortest.
  - After three seconds, ↑ is the octave again on Bass ("Bass octave +1").
- **Undo and redo.**
  - 30 × Ctrl+Z and 30 × Ctrl+Shift+Z on an empty studio: no crash, and "Nothing taken back to bring again".
  - An undo, redo and undo chain on Drums ends in the right state.
  - Undo while paused works.
- **Tempo, key and mode.**
  - Tempo is clamped to 60–200 from keys and scripts.
  - Ctrl+← → wraps around the circle of fifths.
  - Ctrl+[ goes darker (Phrygian), Ctrl+] brighter (Dorian), and twelve presses stop at Lydian.
- **Metronome and Quantise.** Both keep their state through save and reopen, and so do key and mode.
- **Pause.**
  - Ctrl+P and Enter pause and play.
  - Esc pauses when the tutorial and pad aren't in the way, and Esc leaves the tutorial first.
  - Ctrl+Backspace followed by Ctrl+P plays the loops again.
- **Start over and the tutorial.**
  - Ctrl+N asks once and clears on the second press. The ask doesn't expire after 6 s; that's fine.
  - Ctrl+T over loops asks first, then clears and starts the tutorial.
- **Save and reopen.** Loops come back with the same slot, length and events for Drums, Bass, Chords, Lead, Arp (8 beats/7 events), Atmos, FX and Vocal (a sung loop and the demo singer's).
- **Opening bad files.** `{}`, `[]` and `"tenfold":1.5` give "This isn't a tenfold song". A newer format gives "saved by a newer tenfold (format 999…)". Nothing in the studio changes on failure.
- **Export.**
  - An empty studio gives "Nothing is playing to export…".
  - An unwritable folder gives "Can't write to /proc/nope".
  - Loops with no song fall back to "the loops playing".
  - The dialog lists only the parts that have clips.
- **Song page.**
  - I on an empty studio: "Nothing to drop: no part has played yet".
  - I twice with loops writes a 40-bar track, and P and Return pause and play it.
  - Q mutes the drums.
- **Vocal feedback guard.**
  - On speakers, the monitor is muted with the Shift+M hint.
  - A howl through an unknown output mutes it: "Feedback: tenfold muted your monitor".
  - Headphones: "the monitor is back".
- **Sampler.**
  - A dropped WAV is read (119.97 BPM detected) and the studio takes its tempo.
  - Dropped over playing loops, it keeps the studio's tempo.
  - A junk file: "Not read: … Invalid data". A folder: "Not a sound file or a link to a song".
- **The rendered mix.** A 20 s render peaks at −1.0 dBFS with 3 % mean callback load.

### Not covered

- The real trackpad, PipeWire output and the microphone: everything ran offscreen.
- The OPEN FILE dialog and the Sampler's RECORD from a real microphone.
- Stem separation (S), which is slow.
- The visualizer on a GPU.
- macOS.
- I did not get a Sampler loop from scripted taps (my tap coordinates likely missed the chop rows), so the Sampler's Space, F-keys and save/reopen are only covered by the existing `SamplerTests`.
