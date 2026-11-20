# tenfold, page 0 Arrange: a whole track from six loops
#
# Six loops (two drum grooves, bass, chords, atmos, lead, arp) and one key: I.
# The page writes intro, groove, build, drop, breakdown, build, drop and
# outro from them (74 bars), with the producer's moves: drums rising out of
# a high-pass, builds with an echo roll and a breath before each drop, a
# breakdown with no pump, lead throws, an echo-out ending.
#
# The loops are made on the starter part pages (tap a rhythm, hold the next
# beat); when those pages get their own designs, only the first block of this
# script changes. Render it:
#   build/tenfold render docs/parts/arrange/a-track-from-loops.tf out.wav
#   build/tenfold screenshot shot.png docs/parts/arrange/a-track-from-loops.tf --page 0 --at 95

# --- Loops, made on the parts' pages (starter layouts: drums are six zones, notes are scale columns) ---
# 1 Drums: two bars at 120: kick on the beats, hats off the beat, clap on two and four.
page 0.1 1
tap 0.500 0.08 0.5 p=0.8
tap 0.750 0.58 0.5
tap 1.000 0.08 0.5 p=0.8
tap 1.000 0.42 0.5 p=0.7
tap 1.250 0.58 0.5
tap 1.500 0.08 0.5 p=0.8
tap 1.750 0.58 0.5
tap 2.000 0.08 0.5 p=0.8
tap 2.000 0.42 0.5 p=0.7
tap 2.250 0.58 0.5
tap 2.500 0.08 0.5 p=0.8
tap 2.750 0.58 0.5
tap 3.000 0.08 0.5 p=0.8
tap 3.000 0.42 0.5 p=0.7
tap 3.250 0.58 0.5
tap 3.500 0.08 0.5 p=0.8
tap 3.750 0.58 0.5
tap 4.000 0.08 0.5 p=0.8
tap 4.000 0.42 0.5 p=0.7
tap 4.250 0.58 0.5
down 4.500 1 0.08 0.5 p=0.8
up 5.000 1
# 2 Bass: four bars, i-VI-III-VII, the root on the one and the off-beats.
page 5.500 2
tap 6.500 0.0333 0.5 p=0.7 len=0.18
tap 7.250 0.0333 0.5 p=0.7 len=0.18
tap 7.750 0.0333 0.5 p=0.7 len=0.18
tap 8.250 0.0333 0.5 p=0.7 len=0.18
tap 8.500 0.3667 0.5 p=0.7 len=0.18
tap 9.250 0.3667 0.5 p=0.7 len=0.18
tap 9.750 0.3667 0.5 p=0.7 len=0.18
tap 10.250 0.3667 0.5 p=0.7 len=0.18
tap 10.500 0.1667 0.5 p=0.7 len=0.18
tap 11.250 0.1667 0.5 p=0.7 len=0.18
tap 11.750 0.1667 0.5 p=0.7 len=0.18
tap 12.250 0.1667 0.5 p=0.7 len=0.18
tap 12.500 0.4333 0.5 p=0.7 len=0.18
tap 13.250 0.4333 0.5 p=0.7 len=0.18
tap 13.750 0.4333 0.5 p=0.7 len=0.18
tap 14.250 0.4333 0.5 p=0.7 len=0.18
down 14.500 2 0.0333 0.5
up 14.900 2
# 3 Chords: four bars of stabs on the same progression.
page 15.500 3
tap 16.500 0.0333 0.6 p=0.6 len=0.22
tap 17.250 0.0333 0.6 p=0.6 len=0.22
tap 18.000 0.0333 0.6 p=0.6 len=0.22
tap 18.500 0.3667 0.6 p=0.6 len=0.22
tap 19.250 0.3667 0.6 p=0.6 len=0.22
tap 20.000 0.3667 0.6 p=0.6 len=0.22
tap 20.500 0.1667 0.6 p=0.6 len=0.22
tap 21.250 0.1667 0.6 p=0.6 len=0.22
tap 22.000 0.1667 0.6 p=0.6 len=0.22
tap 22.500 0.4333 0.6 p=0.6 len=0.22
tap 23.250 0.4333 0.6 p=0.6 len=0.22
tap 24.000 0.4333 0.6 p=0.6 len=0.22
down 24.500 3 0.0333 0.6
up 24.900 3
# 6 Atmos: a wide chord on the one and the three.
page 25.500 6
tap 26.500 0.0333 0.3 p=0.5 len=0.24
tap 27.500 0.0333 0.3 p=0.5 len=0.24
tap 28.500 0.3667 0.3 p=0.5 len=0.24
tap 29.500 0.3667 0.3 p=0.5 len=0.24
tap 30.500 0.1667 0.3 p=0.5 len=0.24
tap 31.500 0.1667 0.3 p=0.5 len=0.24
tap 32.500 0.4333 0.3 p=0.5 len=0.24
tap 33.500 0.4333 0.3 p=0.5 len=0.24
down 34.500 6 0.0333 0.3
up 34.900 6
# 4 Lead: a two-bar hook.
page 35.500 4
tap 36.500 0.5000 0.7 p=0.6 len=0.16
tap 36.750 0.4333 0.7 p=0.6 len=0.16
tap 37.250 0.3000 0.7 p=0.6 len=0.16
tap 37.500 0.4333 0.7 p=0.6 len=0.16
tap 38.000 0.1667 0.7 p=0.6 len=0.16
tap 38.500 0.5000 0.7 p=0.6 len=0.16
tap 38.750 0.4333 0.7 p=0.6 len=0.16
tap 39.250 0.3000 0.7 p=0.6 len=0.16
tap 39.500 0.4333 0.7 p=0.6 len=0.16
tap 40.000 0.6333 0.7 p=0.6 len=0.16
down 40.500 4 0.5000 0.7
up 40.900 4
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
down 46.500 5 0.0333 0.5
up 46.900 5
# 1 Drums again: four on the floor with open hats off the beat and a snare pickup,
# for the second drop. (No two hits land together: the starter drums lose one of them
# in a second capture.)
page 47.500 1
tap 48.500 0.08 0.5 p=0.85
tap 48.750 0.75 0.5 p=0.6
tap 49.000 0.08 0.5 p=0.85
tap 48.875 0.58 0.5 p=0.4 len=0.05
tap 49.250 0.75 0.5 p=0.6
tap 49.500 0.08 0.5 p=0.85
tap 49.375 0.58 0.5 p=0.4 len=0.05
tap 49.750 0.75 0.5 p=0.6
tap 50.000 0.08 0.5 p=0.85
tap 49.875 0.58 0.5 p=0.4 len=0.05
tap 50.250 0.75 0.5 p=0.6
tap 50.375 0.25 0.5 p=0.55 len=0.05
tap 50.500 0.08 0.5 p=0.85
tap 50.375 0.58 0.5 p=0.4 len=0.05
tap 50.750 0.75 0.5 p=0.6
tap 51.000 0.08 0.5 p=0.85
tap 50.875 0.58 0.5 p=0.4 len=0.05
tap 51.250 0.75 0.5 p=0.6
tap 51.500 0.08 0.5 p=0.85
tap 51.375 0.58 0.5 p=0.4 len=0.05
tap 51.750 0.75 0.5 p=0.6
tap 52.000 0.08 0.5 p=0.85
tap 51.875 0.58 0.5 p=0.4 len=0.05
tap 52.250 0.75 0.5 p=0.6
tap 52.375 0.25 0.5 p=0.55 len=0.05
down 52.500 7 0.08 0.5 p=0.8
up 52.900 7
page 53.000 0
# I writes a whole track from the loops and plays it from the next bar.
key 53.500 I
end 210.500
