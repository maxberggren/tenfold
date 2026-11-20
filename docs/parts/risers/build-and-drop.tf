# Build and drop: a four-bar build stacked layer by layer, every layer
# landing on the same ONE, into a drop with the drums and bass stripped out
# for the last bar. 128 BPM, A minor.
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

# FX (page 7). Drop style STRIP: drums and bass leave for the build's last bar.
page 14.0 7
key 14.1 Right
key 14.2 Right
# A 4-bar noise sweep from the bar at 15.5: the drop is the bar at 23.0.
down 15.55 1 0.10 0.60 p=0.45
glide 16.0 22.6 1 0.10 0.78 p=0.75 twist=35
# A 2-bar snare roll and a 2-bar saw build join it: both come in at 19.25.
down 16.0 2 0.70 0.44 p=0.5
glide 19.3 22.6 2 0.70 0.58 p=0.8
down 16.3 3 0.50 0.45 p=0.5
glide 19.3 22.6 3 0.52 0.60 p=0.8
# A 1-bar pitch riser and a 1-bar reverse swell: in at 21.125.
down 16.6 4 0.30 0.28 p=0.5
glide 21.2 22.6 4 0.30 0.40 p=0.85
down 16.9 5 0.90 0.28 p=0.55
up 22.7 1
up 22.7 2
up 22.7 3
up 22.7 4
up 22.7 5
# After the drop: laser zaps on the off-beats, a downlifter into the next bar.
tap 23.234 0.44 0.12
tap 23.703 0.44 0.16
tap 24.172 0.44 0.10
tap 24.641 0.44 0.18
tap 24.875 0.31 0.15 len=0.3
end 29
