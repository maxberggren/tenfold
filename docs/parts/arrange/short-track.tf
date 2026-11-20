# tenfold, page 0 Arrange: a short track (Shift+I)
#
# Shift+I writes a radio-edit arrangement of the same loops: 38 bars.
#
# The loops are recorded on the part pages the way every page does it: Space
# records from the next bar, Space again closes the loop. Only that first
# block depends on the part pages' layouts (the starters here: drums are six
# zones, the others scale columns). Render it:
#   build/tenfold render docs/parts/arrange/short-track.tf out.wav
#   build/tenfold screenshot shot.png docs/parts/arrange/short-track.tf --page 0 --at 80

# --- Loops, made on the parts' pages: Space records from the next bar, Space again closes ---
tempo 0 120
# 1 Drums (six zones: kick, snare, clap, hat, open, perc): two bars, kick on the beats,
# hats off the beat, clap on two and four.
page 0.1 1
key 1.000 Space
tap 2.000 0.08 0.5 p=0.8
tap 2.250 0.58 0.5
tap 2.500 0.08 0.5 p=0.8
tap 2.500 0.42 0.5 p=0.7
tap 2.750 0.58 0.5
tap 3.000 0.08 0.5 p=0.8
tap 3.250 0.58 0.5
tap 3.500 0.08 0.5 p=0.8
tap 3.500 0.42 0.5 p=0.7
tap 3.750 0.58 0.5
tap 4.000 0.08 0.5 p=0.8
tap 4.250 0.58 0.5
tap 4.500 0.08 0.5 p=0.8
tap 4.500 0.42 0.5 p=0.7
tap 4.750 0.58 0.5
tap 5.000 0.08 0.5 p=0.8
tap 5.250 0.58 0.5
tap 5.500 0.08 0.5 p=0.8
tap 5.500 0.42 0.5 p=0.7
tap 5.750 0.58 0.5
key 4.500 Space
# 2 Bass (scale columns): four bars of i-VI-III-VII, the root on the one, then the off-beats.
page 6.250 2
key 6.500 Space
tap 8.000 0.0333 0.5 p=0.7 len=0.4
tap 8.750 0.0333 0.5 p=0.7 len=0.18
tap 9.250 0.0333 0.5 p=0.7 len=0.18
tap 9.750 0.0333 0.5 p=0.7 len=0.18
tap 10.000 0.3667 0.5 p=0.7 len=0.4
tap 10.750 0.3667 0.5 p=0.7 len=0.18
tap 11.250 0.3667 0.5 p=0.7 len=0.18
tap 11.750 0.3667 0.5 p=0.7 len=0.18
tap 12.000 0.1667 0.5 p=0.7 len=0.4
tap 12.750 0.1667 0.5 p=0.7 len=0.18
tap 13.250 0.1667 0.5 p=0.7 len=0.18
tap 13.750 0.1667 0.5 p=0.7 len=0.18
tap 14.000 0.4333 0.5 p=0.7 len=0.4
tap 14.750 0.4333 0.5 p=0.7 len=0.18
tap 15.250 0.4333 0.5 p=0.7 len=0.18
tap 15.750 0.4333 0.5 p=0.7 len=0.18
key 14.500 Space
# 3 Chords: four bars, each chord held through its bar with a stab on the and of two.
page 16.250 3
key 16.500 Space
tap 18.000 0.0333 0.6 p=0.6 len=0.6
tap 18.750 0.0333 0.7 p=0.6 len=0.2
tap 19.500 0.0333 0.65 p=0.55 len=0.35
tap 20.000 0.3667 0.6 p=0.6 len=0.6
tap 20.750 0.3667 0.7 p=0.6 len=0.2
tap 21.500 0.3667 0.65 p=0.55 len=0.35
tap 22.000 0.1667 0.6 p=0.6 len=0.6
tap 22.750 0.1667 0.7 p=0.6 len=0.2
tap 23.500 0.1667 0.65 p=0.55 len=0.35
tap 24.000 0.4333 0.6 p=0.6 len=0.6
tap 24.750 0.4333 0.7 p=0.6 len=0.2
tap 25.500 0.4333 0.65 p=0.55 len=0.35
key 24.500 Space
# 6 Atmos: four bars of wide chords, held.
page 26.250 6
key 26.500 Space
down 28.000 60 0.0333 0.3 p=0.5
up 29.900 60
down 30.000 61 0.3667 0.3 p=0.5
up 31.900 61
down 32.000 62 0.1667 0.3 p=0.5
up 33.900 62
down 34.000 63 0.4333 0.3 p=0.5
up 35.900 63
key 34.500 Space
# 4 Lead: a two-bar hook.
page 36.250 4
key 36.500 Space
tap 38.000 0.5000 0.7 p=0.6 len=0.4
tap 38.250 0.4333 0.7 p=0.6 len=0.2
tap 38.750 0.3000 0.7 p=0.6 len=0.2
tap 39.000 0.4333 0.7 p=0.6 len=0.4
tap 39.500 0.1667 0.7 p=0.6 len=0.4
tap 40.000 0.5000 0.7 p=0.6 len=0.4
tap 40.250 0.4333 0.7 p=0.6 len=0.2
tap 40.750 0.3000 0.7 p=0.6 len=0.2
tap 41.000 0.4333 0.7 p=0.6 len=0.4
tap 41.500 0.6333 0.7 p=0.6 len=0.4
key 40.500 Space
# 5 Arp: two bars of running eighths.
page 42.250 5
key 42.500 Space
tap 44.000 0.0333 0.5 p=0.5 len=0.12
tap 44.250 0.1667 0.5 p=0.5 len=0.12
tap 44.500 0.3000 0.5 p=0.5 len=0.12
tap 44.750 0.5000 0.5 p=0.5 len=0.12
tap 45.000 0.3000 0.5 p=0.5 len=0.12
tap 45.250 0.1667 0.5 p=0.5 len=0.12
tap 45.500 0.3000 0.5 p=0.5 len=0.12
tap 45.750 0.5000 0.5 p=0.5 len=0.12
tap 46.000 0.1667 0.5 p=0.5 len=0.12
tap 46.250 0.3000 0.5 p=0.5 len=0.12
tap 46.500 0.4333 0.5 p=0.5 len=0.12
tap 46.750 0.6333 0.5 p=0.5 len=0.12
tap 47.000 0.4333 0.5 p=0.5 len=0.12
tap 47.250 0.3000 0.5 p=0.5 len=0.12
tap 47.500 0.4333 0.5 p=0.5 len=0.12
tap 47.750 0.6333 0.5 p=0.5 len=0.12
key 46.500 Space
# 1 Drums again: four on the floor with open hats off the beat and a snare pickup,
# for the second drop.
page 48.250 1
key 48.500 Space
tap 50.000 0.08 0.5 p=0.85
tap 50.250 0.75 0.5 p=0.6
tap 50.500 0.08 0.5 p=0.85
tap 50.375 0.58 0.5 p=0.4 len=0.05
tap 50.750 0.75 0.5 p=0.6
tap 51.000 0.08 0.5 p=0.85
tap 50.875 0.58 0.5 p=0.4 len=0.05
tap 51.250 0.75 0.5 p=0.6
tap 51.500 0.08 0.5 p=0.85
tap 51.375 0.58 0.5 p=0.4 len=0.05
tap 51.750 0.75 0.5 p=0.6
tap 51.875 0.25 0.5 p=0.55 len=0.05
tap 52.000 0.08 0.5 p=0.85
tap 51.875 0.58 0.5 p=0.4 len=0.05
tap 52.250 0.75 0.5 p=0.6
tap 52.500 0.08 0.5 p=0.85
tap 52.375 0.58 0.5 p=0.4 len=0.05
tap 52.750 0.75 0.5 p=0.6
tap 53.000 0.08 0.5 p=0.85
tap 52.875 0.58 0.5 p=0.4 len=0.05
tap 53.250 0.75 0.5 p=0.6
tap 53.500 0.08 0.5 p=0.85
tap 53.375 0.58 0.5 p=0.4 len=0.05
tap 53.750 0.75 0.5 p=0.6
tap 53.875 0.25 0.5 p=0.55 len=0.05
key 52.500 Space
page 54.500 0
# I writes a whole track from the loops and plays it from the next bar.
key 55.000 Shift+I
end 140.000
