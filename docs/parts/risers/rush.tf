# Rush: an eight-bar build of harmonised pitch risers, with layers added from
# the keyboard, cut short by a click two bars in: the drop comes on the next
# bar line and everything rising rushes into it, through a half-beat gap.
# Then an echo throw and a reverb wash from the keys. 128 BPM, A minor.
#
# The beat (page 1): kick on every beat, hats between, clap on 2 and 4; the
# next ONE held closes the loop and sets the tempo. Bars then fall at
# 2.375 + 1.875 k seconds.
page 0 1
tap 0.500 0.08 0.5 p=0.7
tap 0.734 0.58 0.5
tap 0.969 0.08 0.5 p=0.7
tap 0.969 0.42 0.5
tap 1.203 0.58 0.5
tap 1.438 0.08 0.5 p=0.7
tap 1.672 0.58 0.5
tap 1.906 0.08 0.5 p=0.7
tap 1.906 0.42 0.5
tap 2.141 0.58 0.5
down 2.375 9 0.08 0.5 p=0.7
up 2.8 9

# Chords (page 3): Am, F, C, G as stabs on every half bar, looped.
page 3.0 3
tap 4.250 0.033 0.5 len=0.2
tap 5.188 0.367 0.5 len=0.2
tap 6.125 0.167 0.5 len=0.2
tap 7.063 0.433 0.5 len=0.2
down 8.000 30 0.033 0.5
up 8.3 30

# Bass (page 2): the roots under them.
page 8.4 2
tap 9.875 0.033 0.5 len=0.2
tap 10.813 0.367 0.5 len=0.2
tap 11.750 0.167 0.5 len=0.2
tap 12.688 0.433 0.5 len=0.2
down 13.625 31 0.033 0.5
up 13.9 31

# FX (page 7). Drop style GAP: everything holds its breath for the last half beat.
page 14.000 7
key 14.1 Right
# Two pitch risers in the 8-bar row, aiming at the bar at 30.5: one lands on
# the tonic, the other slides two scale steps up, to the third.
down 15.547 1 0.30 0.76 p=0.5
down 15.594 2 0.30 0.76 p=0.5
glide 15.688 16.203 2 0.395 0.76
# From the keyboard: D (saw) and F (roll) join the same drop, entering four
# bars before it.
key 17.375 D
key 19.250 F
# Two bars in, the click: the drop comes on the next bar line instead.
# The pitch risers rush the rest of their climb into one bar; the saw and
# the roll, still waiting, come in at once and build in that bar too.
glide 20.188 22.531 1 0.30 0.88 p=0.8
glide 20.188 22.531 2 0.395 0.88 p=0.8
click 21.266
up 22.766 1
up 22.766 2
# After the drop, from the keyboard: an echo throw on the last beat of the
# next bar (N), then a bar of reverb wash (M).
key 26.281 N down
key 26.750 N up
key 26.750 M down
key 28.625 M up
end 32.375
