# House from two hands, at the studio's 124 BPM: a four-finger groove kept as a loop,
# a percussion groove overdubbed into it, ghost notes, a tom fill, then a two-bar
# snare build (faster, higher, louder, opening) over a kick roll, and the drop.
page 0 1
# Four fingers land together and stay: kick, clap, hat, open hat groove.
down 0.484 1 0.12 0.12 p=0.65
down 0.503 2 0.62 0.12 p=0.6
down 0.523 3 0.88 0.12 p=0.55
down 0.542 4 0.88 0.38 p=0.55
# A click keeps it: it plays on to the bar line and becomes loop 1.
click 1.645 len=0.12
up 2.032 1
up 2.032 2
up 2.032 3
up 2.032 4
# Rim, shaker and conga land together: a percussion groove. The conga slides
# sideways two sixteenths, the shaker up to busy; a click overdubs one pass.
down 4.355 5 0.38 0.38 p=0.5
down 4.374 6 0.62 0.38 p=0.45
down 4.394 7 0.88 0.62 p=0.55
glide 4.839 5.129 7 1.0 0.62
glide 4.839 5.129 6 0.62 0.46 p=0.55
click 5.903 len=0.12
up 6.194 5
up 6.194 6
up 6.194 7
# Ghost snares between the beats, then a run across the toms into the next bar.
tap 10.524 0.38 0.12 p=0.15 len=0.06
tap 11.008 0.38 0.12 p=0.15 len=0.06
tap 11.371 0.38 0.12 p=0.15 len=0.06
tap 12.460 0.38 0.12 p=0.15 len=0.06
tap 12.823 0.38 0.12 p=0.15 len=0.06
down 13.065 8 0.06 0.62 p=0.7
glide 13.084 13.355 8 0.97 0.62 p=0.8
up 13.403 8
# The build: the loop stops at the bar, the snare rolls from 1/8 to 1/32 over two
# bars, bending up and opening as the finger presses harder; the kick rolls under it.
key 13.645 Backspace
down 13.548 9 0.38 0.12 p=0.2
glide 13.839 17.323 9 0.62 0.42 p=1.0 twist=35
down 15.484 10 0.12 0.12 p=0.5
glide 16.355 16.548 10 0.12 0.2 p=0.9
# The drop: everything lets go, the crash and the kick land on the one, loop 1 is back.
key 15.968 F1
up 17.371 9
up 17.371 10
tap 17.419 0.12 0.88 p=0.9 len=0.1
tap 17.419 0.12 0.12 p=0.9 len=0.1
end 22.258
