# tenfold, page 0 Arrange: a whole track from seven loops
#
# Seven loops (two drum grooves, bass, chords, atmos, lead, arp) and one key: I.
# The page writes intro, groove, build, drop, breakdown, build, drop and
# outro from them (76 bars), with the producer's moves: drums rising out of
# a high-pass, builds with an echo roll and a breath before each drop, a
# breakdown with no pump, lead throws, an echo-out ending.
#
# The loops are made on the part pages the way every page makes them: play,
# then Space loops what was just played (the first, with nothing playing,
# closed on its next ONE, sets the tempo). They are single hits and notes, so
# the first block leans as little as it can on the pages' layouts: the
# drums' near row of pads, the lead's A row of keys, single notes on the
# scale columns of pages 2, 3, 5 and 6 (still starters). Render it:
#   build/tenfold render docs/parts/arrange/a-track-from-loops.tf out.wav
#   build/tenfold screenshot shot.png docs/parts/arrange/a-track-from-loops.tf --page 0 --at 93.5

# --- Loops, made on the parts' pages: play, then Space loops what was just played ---
# 1 Drums (page 1, the near row of pads: kick, snare, clap, hat), single hits: two bars,
# kick on the beats, hats off the beat, clap on two and four; Space on the next ONE sets the tempo.
page 0.1 1
tap 0.500 0.12 0.12 p=0.75 len=0.06
tap 0.750 0.88 0.12 p=0.45 len=0.06
tap 1.000 0.12 0.12 p=0.75 len=0.06
tap 1.010 0.62 0.12 p=0.65 len=0.06
tap 1.250 0.88 0.12 p=0.45 len=0.06
tap 1.500 0.12 0.12 p=0.75 len=0.06
tap 1.750 0.88 0.12 p=0.45 len=0.06
tap 2.000 0.12 0.12 p=0.75 len=0.06
tap 2.010 0.62 0.12 p=0.65 len=0.06
tap 2.250 0.88 0.12 p=0.45 len=0.06
tap 2.500 0.12 0.12 p=0.75 len=0.06
tap 2.750 0.88 0.12 p=0.45 len=0.06
tap 3.000 0.12 0.12 p=0.75 len=0.06
tap 3.010 0.62 0.12 p=0.65 len=0.06
tap 3.250 0.88 0.12 p=0.45 len=0.06
tap 3.500 0.12 0.12 p=0.75 len=0.06
tap 3.750 0.88 0.12 p=0.45 len=0.06
tap 4.000 0.12 0.12 p=0.75 len=0.06
tap 4.010 0.62 0.12 p=0.65 len=0.06
tap 4.250 0.88 0.12 p=0.45 len=0.06
key 4.500 Space
# 2 Bass (scale columns): four bars of i-VI-III-VII, the root held on the one, then the off-beats.
page 5.500 2
tap 6.500 0.0333 0.5 p=0.7 len=0.4
tap 7.250 0.0333 0.5 p=0.7 len=0.18
tap 7.750 0.0333 0.5 p=0.7 len=0.18
tap 8.250 0.0333 0.5 p=0.7 len=0.18
tap 8.500 0.3667 0.5 p=0.7 len=0.4
tap 9.250 0.3667 0.5 p=0.7 len=0.18
tap 9.750 0.3667 0.5 p=0.7 len=0.18
tap 10.250 0.3667 0.5 p=0.7 len=0.18
tap 10.500 0.1667 0.5 p=0.7 len=0.4
tap 11.250 0.1667 0.5 p=0.7 len=0.18
tap 11.750 0.1667 0.5 p=0.7 len=0.18
tap 12.250 0.1667 0.5 p=0.7 len=0.18
tap 12.500 0.4333 0.5 p=0.7 len=0.4
tap 13.250 0.4333 0.5 p=0.7 len=0.18
tap 13.750 0.4333 0.5 p=0.7 len=0.18
tap 14.250 0.4333 0.5 p=0.7 len=0.18
key 14.650 Space
# 3 Chords: four bars, a chord on the one, a stab on the and of two, a push on four.
page 15.500 3
tap 16.500 0.0333 0.6 p=0.6 len=0.6
tap 17.250 0.0333 0.7 p=0.6 len=0.2
tap 18.000 0.0333 0.65 p=0.55 len=0.35
tap 18.500 0.3667 0.6 p=0.6 len=0.6
tap 19.250 0.3667 0.7 p=0.6 len=0.2
tap 20.000 0.3667 0.65 p=0.55 len=0.35
tap 20.500 0.1667 0.6 p=0.6 len=0.6
tap 21.250 0.1667 0.7 p=0.6 len=0.2
tap 22.000 0.1667 0.65 p=0.55 len=0.35
tap 22.500 0.4333 0.6 p=0.6 len=0.6
tap 23.250 0.4333 0.7 p=0.6 len=0.2
tap 24.000 0.4333 0.65 p=0.55 len=0.35
key 24.650 Space
# 6 Atmos: four bars of wide chords, held.
page 25.500 6
down 26.500 60 0.0333 0.3 p=0.5
up 28.400 60
down 28.500 61 0.3667 0.3 p=0.5
up 30.400 61
down 30.500 62 0.1667 0.3 p=0.5
up 32.400 62
down 32.500 63 0.4333 0.3 p=0.5
up 34.400 63
key 34.650 Space
# 4 Lead (page 4): a two-bar hook typed on the A row of keys (the scale, an octave up).
page 35.500 4
key 36.500 K down
key 36.900 K up
key 36.750 J down
key 36.950 J up
key 37.250 G down
key 37.450 G up
key 37.500 J down
key 37.900 J up
key 38.000 D down
key 38.400 D up
key 38.500 K down
key 38.900 K up
key 38.750 J down
key 38.950 J up
key 39.250 G down
key 39.450 G up
key 39.500 J down
key 39.900 J up
key 40.000 L down
key 40.400 L up
key 40.650 Space
# 5 Arp: two bars of running eighths.
page 41.500 5
tap 42.500 0.0333 0.5 p=0.5 len=0.12
tap 42.750 0.1667 0.5 p=0.5 len=0.12
tap 43.000 0.3000 0.5 p=0.5 len=0.12
tap 43.250 0.5000 0.5 p=0.5 len=0.12
tap 43.500 0.3000 0.5 p=0.5 len=0.12
tap 43.750 0.1667 0.5 p=0.5 len=0.12
tap 44.000 0.3000 0.5 p=0.5 len=0.12
tap 44.250 0.5000 0.5 p=0.5 len=0.12
tap 44.500 0.1667 0.5 p=0.5 len=0.12
tap 44.750 0.3000 0.5 p=0.5 len=0.12
tap 45.000 0.4333 0.5 p=0.5 len=0.12
tap 45.250 0.6333 0.5 p=0.5 len=0.12
tap 45.500 0.4333 0.5 p=0.5 len=0.12
tap 45.750 0.3000 0.5 p=0.5 len=0.12
tap 46.000 0.4333 0.5 p=0.5 len=0.12
tap 46.250 0.6333 0.5 p=0.5 len=0.12
key 46.650 Space
# 1 Drums again: four on the floor, open hats off the beat, closed hats before the beat and a
# snare pickup, single hits again: for the second drop. Shift+Space makes it a new loop, not a layer.
page 47.500 1
tap 48.500 0.12 0.12 p=0.8 len=0.06
tap 48.750 0.88 0.38 p=0.5 len=0.06
tap 49.000 0.12 0.12 p=0.8 len=0.06
tap 48.875 0.88 0.12 p=0.35 len=0.05
tap 49.250 0.88 0.38 p=0.5 len=0.06
tap 49.500 0.12 0.12 p=0.8 len=0.06
tap 49.375 0.88 0.12 p=0.35 len=0.05
tap 49.750 0.88 0.38 p=0.5 len=0.06
tap 50.000 0.12 0.12 p=0.8 len=0.06
tap 49.875 0.88 0.12 p=0.35 len=0.05
tap 50.250 0.88 0.38 p=0.5 len=0.06
tap 50.375 0.38 0.12 p=0.5 len=0.05
tap 50.500 0.12 0.12 p=0.8 len=0.06
tap 50.375 0.88 0.12 p=0.35 len=0.05
tap 50.750 0.88 0.38 p=0.5 len=0.06
tap 51.000 0.12 0.12 p=0.8 len=0.06
tap 50.875 0.88 0.12 p=0.35 len=0.05
tap 51.250 0.88 0.38 p=0.5 len=0.06
tap 51.500 0.12 0.12 p=0.8 len=0.06
tap 51.375 0.88 0.12 p=0.35 len=0.05
tap 51.750 0.88 0.38 p=0.5 len=0.06
tap 51.875 0.38 0.12 p=0.5 len=0.05
tap 52.000 0.12 0.12 p=0.8 len=0.06
tap 51.875 0.88 0.12 p=0.35 len=0.05
tap 52.250 0.88 0.38 p=0.5 len=0.06
key 52.650 Shift+Space
page 53.000 0
# I writes a whole track from the loops and plays it from the next bar (twice, as clips play).
key 53.250 I
key 53.500 I
end 214.500
