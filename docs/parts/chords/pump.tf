# Chords demo: filtered, pumping chords at 122 BPM in A minor.
# The pump sound ducks itself on every beat, no drums needed. The first chord
# is twisted shut, and each of the next three twists the filter a little
# further open (the twist is a knob: it stays where it was left). Then a click
# arms the recorder, two bars of tapped stabs are recorded from the bar line,
# and a second click closes them into a loop; over it a
# big flat-fingered chord is swept shut and open again with one twist.
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
# bar 4, beat 3: click to arm the recorder; bars 5-6 a tapped stab rhythm; a click just after bar 7 starts loops it
click 7.377
tap 7.869 0.0714 0.720 len=0.15 p=0.7
tap 8.238 0.0714 0.720 len=0.15 p=0.7
tap 8.607 0.0714 0.720 len=0.15 p=0.7
tap 9.098 0.5000 0.720 len=0.15 p=0.7
tap 9.467 0.5000 0.720 len=0.15 p=0.7
tap 9.836 0.7857 0.720 len=0.15 p=0.7
tap 10.205 0.7857 0.720 len=0.15 p=0.7
tap 10.574 0.9286 0.720 len=0.15 p=0.7
tap 11.066 0.9286 0.720 len=0.15 p=0.7
tap 11.311 0.9286 0.720 len=0.15 p=0.7
click 11.902
# bars 7-10: over the loop, a big chord swept shut, then wide open
down 13.780 5 0.7857 0.560 p=0.6 size=1.8 twist=0
glide 14.016 17.213 5 0.7857 0.560 twist=-55 p=0.7 size=1.8
glide 17.705 21.148 5 0.7857 0.560 twist=75 p=1.0 size=1.8
up 21.639 5
end 24.590
