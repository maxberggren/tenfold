# Chords demo: a trance anthem at 138 BPM in A minor.
# Four bars of supersaw pad played legato with one hand (Am add9, Fmaj7,
# C add9, Gsus4 sliding up into G, the last with a flat finger), looped with
# Return at the bar line. Over the loop: the gated trance sound, a voicing
# higher, its filter twisted open; a build on the rail with two and three
# fingers; then the pluck in 3-3-2 over the pad loop, and a held wub to end.
tempo 0 138
page 0.01 3
# bars 1-4: the pad, legato: each finger lands just before the last lifts
down 0.050 1 0.0714 0.560 p=0.55 size=1.0
glide 0.150 1.550 1 0.0714 0.560 p=0.85
down 1.759 2 0.7857 0.720 p=0.55 size=1.0
up 1.819 1
down 3.498 3 0.3571 0.560 p=0.55 size=1.0
up 3.558 2
down 5.237 4 0.9286 0.240 p=0.55 size=1.9
up 5.297 3
glide 6.087 6.261 4 0.9286 0.400 p=0.9 size=1.9
key 6.304 Return
up 6.907 4
# bars 5-8: the trance gate over the loop, a voicing up, filter twisted open on the third chord
key 7.087 Right
key 7.109 Up
down 6.967 5 0.0714 0.400 p=0.7
up 8.666 5
down 8.706 6 0.7857 0.400 p=0.7
up 10.405 6
down 10.445 7 0.3571 0.400 p=0.7
glide 10.545 12.045 7 0.3571 0.400 twist=60 p=0.9
up 12.144 7
down 12.184 8 0.9286 0.400 p=0.7
# the build: two fingers on the 16ths rail (double time), then a third (four times)
down 12.609 9 0.8125 0.85
down 12.617 10 0.8125 0.86
down 13.478 11 0.8125 0.88
up 13.883 9
up 13.883 10
up 13.883 11
up 13.883 8
# bars 9-12: the pluck, its 3-3-2 rhythm on held chords, over the pad loop
key 14.000 Right
key 14.022 Right
key 14.043 Right
down 13.923 12 0.0714 0.560 p=0.6
up 15.622 12
down 15.662 13 0.7857 0.560 p=0.6
up 17.361 13
down 17.401 14 0.3571 0.560 p=0.6
up 19.100 14
down 19.140 15 0.9286 0.560 p=0.6
up 20.840 15
# bars 13-14: the pad again, a big F major seventh held with the wub gate
key 20.913 Left
key 20.922 Left
key 20.930 Left
key 20.939 Left
key 20.957 Down
down 21.170 16 0.7857 0.720 p=0.8 size=2.0
down 21.087 17 0.9375 0.95
up 24.348 17
up 24.448 16
key 24.435 Backspace
end 27.826
