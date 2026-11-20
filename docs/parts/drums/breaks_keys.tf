# Breaks: a beat tapped in by hand, closed on its first pad, sets 172 BPM; the snare is
# grabbed with a click and dragged lower, longer and darker while the loop plays;
# ride and tambourine land together as a groove and are kept into the loop; then the
# keyboard takes over: a held hat key rolling faster, accents, and a tom run to finish.
page 0 1
key 0.1 Right
key 0.1 Right
key 0.1 Right
# Kick, kick, snare, kick, snare, ghost: then back on the kick, on time, and stay.
tap 0.600 0.12 0.12 p=0.8 len=0.07
tap 0.774 0.12 0.12 p=0.5 len=0.07
tap 0.948 0.38 0.12 p=0.75 len=0.07
tap 1.470 0.12 0.12 p=0.7 len=0.07
tap 1.644 0.38 0.12 p=0.75 len=0.07
tap 1.818 0.38 0.12 p=0.15 len=0.07
down 1.992 1 0.12 0.12 p=0.8
up 2.514 1
# Click on the snare and drag: down a few semitones, longer, twisted darker.
down 4.776 2 0.38 0.12 p=0.6
click 4.811 len=1.2
glide 4.985 5.646 2 0.28 0.2 twist=-25
up 5.820 2
# Ride and tambourine together: a groove; the ride finger slides up to busy; kept.
down 6.864 3 0.38 0.88 p=0.5
down 6.881 4 0.88 0.62 p=0.55
glide 7.212 7.316 3 0.38 0.96
click 7.908 len=0.1
up 8.082 3
up 8.082 4
# Keys: V held rolls the hat, Up makes it faster; Shift+X and Shift+C are accents.
key 10.344 V down
key 11.040 Up
key 11.562 V up
key 11.649 Down
key 11.997 Shift+X
key 12.258 Shift+C
key 12.693 Shift+X
key 12.954 Shift+C
key 13.128 Q
key 13.215 W
key 13.302 E
key 13.389 Q
key 13.476 U
end 15.912
