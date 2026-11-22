# Five new: the third row of sounds, each in its own music over one latched
# A minor triad at 112 BPM, two bars apiece.
#   A steel pan running up and down in sixteenths, accented in threes
#   (calypso, soca): its stretched octaves and twelfths, its neighbours
#   ringing; then a bar of eighths held long, each note rolled.
#   A sitar wandering at random in sixteenth triplets, accents striking the
#   chikari; in the second bar the gate is pushed to legato, so the accented
#   notes near the last are a meend pulled from it and the rest fresh
#   plucks on the one string, over sympathetic strings tuned to A.
#   A log drum in the bass on the pedal pattern, seven euclidean hits of
#   sixteen (amapiano), an octave under the grid and ringing long, each log
#   gliding from the last and yelping on the accents: round (Kelvin Momo)
#   for a bar, then gritty with the glide twisted slow (Kabza De Small).
#   A Game Boy pulse racing up in thirty-seconds, its accents channel 1's
#   sweep stepping down into the note; then the chord pattern, the triad as
#   one channel's 60 Hz arpeggio (chiptune).
#   A gamelan converging over three octaves in sixteenths, polos and sangsih
#   interlocking in norot kotekan on selisir's five keys (Balinese gong
#   kebyar).
tempo 0 112
page 0.05 5
# Steel pan.
key 0.100 Shift+Z
key 0.150 Shift+Z
key 0.200 E
tap 0.300 0.6705 0.01
down 0.500 1 0.0362 0.3200 p=0.55
key 1.000 Return
up 1.200 1
# The second bar in eighths, the gate held long: rolled.
tap 2.600 0.7850 0.7120
# Back to sixteenths, the gate as it was, for the sitar.
tap 4.600 0.6720 0.8130
# Sitar: random in 1/16T, accents every four; legato for the second bar.
key 4.700 Shift+X
key 4.750 Shift+X
key 4.800 Y
key 4.850 ]
tap 4.900 0.6994 0.01
tap 7.000 0.7850 0.8640
# Log drum: pedal in 1/16 in the bass, seven hits of sixteen; round, then gritty.
key 9.000 Shift+C
key 9.050 Shift+C
key 9.100 I
key 9.150 [
key 9.200 Down
key 9.250 O
key 9.270 O
key 9.290 O
key 9.310 O
key 9.330 O
key 9.350 O
key 9.370 O
key 9.390 O
key 9.410 O
tap 9.450 0.8320 0.4880
down 11.450 3 0.9670 0.4880
move 11.520 3 0.9670 0.4880 twist=-25
up 11.600 3
# Chip: up in 1/32 two octaves higher, accents every four sweeping; then chord stabs in 1/8.
key 13.300 Shift+V
key 13.350 Shift+V
tap 13.370 0.6760 0.9150
key 13.400 Q
key 13.450 Up
key 13.500 Up
key 13.550 P
key 13.570 P
key 13.590 P
key 13.610 P
key 13.630 P
key 13.650 P
key 13.670 P
key 13.690 P
key 13.710 P
key 15.550 U
key 15.600 [
key 15.650 [
key 15.700 [
key 15.750 [
# Gamelan: converge in 1/16 over three octaves, norot kotekan in selisir.
key 17.700 Shift+B
key 17.750 Shift+B
key 17.800 R
key 17.850 Down
key 17.900 Shift+Up
tap 17.950 0.6760 0.8130
end 22.800
