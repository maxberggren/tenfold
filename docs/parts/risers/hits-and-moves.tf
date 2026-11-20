# Hits and moves: the bottom row over a groove. Laser zaps tapped on the
# off-beats and looped, an echo throw, a reverb wash into shimmer, a DJ
# sweep down and up, then a power-down into a clicked drop, sub drops and a
# downlifter. 128 BPM, A minor.
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

# FX (page 7): the hit row and the mix moves.
page 14.000 7
# Laser zaps on the off-beats, a different chord tone across the cell;
# the next off-beat held closes the loop (FX slot 1).
tap 15.734 0.395 0.12
tap 16.203 0.440 0.12
tap 16.672 0.480 0.12
tap 17.141 0.420 0.12
down 17.609 20 0.395 0.12
up 17.938 20
# An echo throw on the bar's last beat: every part into the tape echo.
down 20.656 21 0.688 0.14
glide 20.703 21.078 21 0.688 0.19
up 21.125 21
# Two bars of reverb wash, sliding up into shimmer.
down 21.125 22 0.812 0.10
glide 21.359 24.406 22 0.812 0.19 p=0.7
up 24.875 22
# The DJ sweep: a bar sliding left closes a low-pass on the whole mix, a
# bar sliding up opens a high-pass.
down 24.875 23 0.938 0.10
glide 24.969 26.656 23 0.738 0.10
glide 26.750 28.531 23 0.938 0.27
up 28.625 23
# A power-down from beat 2, a click on beat 3: the mix sinks into the
# bar line and comes back on it with the drop.
tap 29.094 0.562 0.12
click 29.562
# After the drop: sub drops on the ONE of the next two bars, then a
# downlifter held for a bar.
tap 32.375 0.188 0.15
tap 34.250 0.217 0.08
down 36.125 24 0.312 0.15
up 38.000 24
end 41.750
