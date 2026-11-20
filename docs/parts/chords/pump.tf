# Chords demo: filtered, pumping chords at 122 BPM in A minor.
# The pump sound ducks itself on every beat, no drums needed. The first chord
# is twisted shut, and each of the next three twists the filter a little
# further open (the twist is a knob: it stays where it was left); Space on the
# next ONE loops the four bars, sweep and all. Over the loop, stabs tapped on
# the loop's own chords; then a big flat-fingered chord on another degree, the
# loop stepping back under it, swept shut and wide open with one twist.
tempo 0 122
page 0.01 3
key 0.020 Left
# bars 1-4: one finger per bar; the first twists the filter shut, the rest open it
down 0.050 1 0.0714 0.720 p=0.6 twist=0
glide 0.250 1.767 1 0.0714 0.720 twist=-45 p=0.8
down 1.977 2 0.5000 0.720 p=0.6 twist=0
glide 2.177 3.734 2 0.5000 0.720 twist=18 p=0.8
up 2.017 1
down 3.944 3 0.9286 0.400 p=0.6 twist=0
glide 4.144 5.702 3 0.9286 0.400 twist=22 p=0.8
up 3.984 2
down 5.912 4 0.3571 0.720 p=0.6 twist=0
glide 6.112 7.669 4 0.3571 0.720 twist=25 p=0.8
up 5.952 3
up 7.839 4
# Space on the next ONE: the four bars loop and set the tempo
key 7.919 Space
# bars 5-8: stabs tapped over the loop on its own chords
tap 8.115 0.0714 0.560 len=0.12 p=0.75
tap 8.484 0.0714 0.560 len=0.12 p=0.75
tap 9.098 0.0714 0.560 len=0.12 p=0.75
tap 9.344 0.0714 0.560 len=0.12 p=0.75
tap 9.590 0.0714 0.560 len=0.12 p=0.75
tap 10.082 0.5000 0.560 len=0.12 p=0.75
tap 10.451 0.5000 0.560 len=0.12 p=0.75
tap 11.066 0.5000 0.560 len=0.12 p=0.75
tap 11.311 0.5000 0.560 len=0.12 p=0.75
tap 11.557 0.5000 0.560 len=0.12 p=0.75
tap 12.049 0.9286 0.560 len=0.12 p=0.75
tap 12.418 0.9286 0.560 len=0.12 p=0.75
tap 13.033 0.9286 0.560 len=0.12 p=0.75
tap 13.279 0.9286 0.560 len=0.12 p=0.75
tap 13.525 0.9286 0.560 len=0.12 p=0.75
tap 14.016 0.3571 0.560 len=0.12 p=0.75
tap 14.385 0.3571 0.560 len=0.12 p=0.75
tap 15.000 0.3571 0.560 len=0.12 p=0.75
tap 15.246 0.3571 0.560 len=0.12 p=0.75
tap 15.492 0.3571 0.560 len=0.12 p=0.75
# bars 9-12: a big chord on VI: the loop steps back while it is held; swept shut, then wide open
down 15.748 5 0.7857 0.560 p=0.6 size=1.8 twist=0
glide 15.984 19.180 5 0.7857 0.560 twist=-55 p=0.7 size=1.8
glide 19.672 23.115 5 0.7857 0.560 twist=75 p=1.0 size=1.8
up 23.607 5
end 27.541
