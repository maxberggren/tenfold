# Build and drop: an eight-bar build stacked layer by layer over a groove,
# every layer landing on the same phrase line, into a drop with drums and bass
# stripped out for the last bar and a sub armed onto the ONE. 128 BPM, A minor.
#
# The groove, made the way the studio makes loops: play, then Space.
# Drums (page 1, the 4x4 pads; the near row is kick, snare, clap, hat): kick on
# every beat, hats between, clap on 2 and 4, then Space where the next ONE
# would land: a one-bar loop that sets 128 BPM.
page 0 1
tap 0.500 0.12 0.12 p=0.7
tap 0.734 0.88 0.12 p=0.5
tap 0.969 0.12 0.12 p=0.7
tap 0.969 0.62 0.12 p=0.6
tap 1.203 0.88 0.12 p=0.5
tap 1.438 0.12 0.12 p=0.7
tap 1.672 0.88 0.12 p=0.5
tap 1.906 0.12 0.12 p=0.7
tap 1.906 0.62 0.12 p=0.6
tap 2.141 0.88 0.12 p=0.5
key 2.375 Space
# Chords (page 3: columns are the key's chords I to VII, the middle row plain
# triads): Am, F, C, G, two beats each over bars 1 and 2, then Space just
# after the bar line: those two bars loop.
page 3.0 3
tap 4.250 0.0714 0.40 len=0.85 p=0.6
tap 5.188 0.7857 0.40 len=0.85 p=0.6
tap 6.125 0.3571 0.40 len=0.85 p=0.6
tap 7.062 0.9286 0.40 len=0.85 p=0.6
key 8.094 Space
# Bass (page 2, a scale-column starter): the roots under them, bars 4 and 5.
page 8.281 2
tap 9.875 0.033 0.5 len=0.2
tap 10.812 0.367 0.5 len=0.2
tap 11.750 0.167 0.5 len=0.2
tap 12.688 0.433 0.5 len=0.2
key 13.719 Space
# FX (page 7). Drop style STRIP: drums and bass leave for the build's last bar.
page 13.906 7
key 13.953 Shift+Right
key 14.000 Shift+Right
# An 8-bar noise sweep from bar 8: it aims at the phrase line at bar 16
# (32.375 s), sliding up as it goes.
down 17.398 1 0.10 0.76 p=0.45
glide 17.844 32.141 1 0.10 0.92 p=0.75 twist=35
# Layers join the same drop, each entering its row's length before it: a
# 4-bar roll (in at bar 12), a 2-bar saw build (bar 14), a 1-bar pitch
# riser and a 1-bar reverse swell (bar 15).
down 19.250 2 0.70 0.60 p=0.5
glide 24.875 32.141 2 0.70 0.72 p=0.8
down 19.719 3 0.50 0.44 p=0.5
glide 28.625 32.141 3 0.52 0.56 p=0.8
down 20.188 4 0.30 0.28 p=0.5
glide 30.500 32.141 4 0.30 0.38 p=0.85
down 20.656 5 0.90 0.28 p=0.55
# A sub drop tapped during the build is armed onto the ONE.
tap 26.750 0.188 0.12
up 32.188 1
up 32.188 2
up 32.188 3
up 32.188 4
up 32.188 5
# After the drop: laser zaps on the off-beats, a downlifter into the next bar.
tap 32.609 0.395 0.12
tap 33.078 0.440 0.12
tap 33.547 0.480 0.12
tap 34.016 0.420 0.12
tap 35.656 0.312 0.15 len=0.3
end 39.875
