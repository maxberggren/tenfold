# Sunrise: a breakdown built from nothing, on the Atmos page alone.
# 124 BPM, A minor. The bed plays its own progression i, VI, III, VII (keys Z N C M),
# two bars each, so it needs no other page. Layers arrive one hand at a time,
# climb and open, get latched with a click, swell into a frozen drop, and the
# Pulse scene pumps under it on the beat; when the chords move on, the freeze melts
# into each new one.
page 0.05 6
key 0.100 Z
key 3.811 N
key 7.682 C
key 11.553 M
key 15.424 Z
key 19.295 N
key 23.166 C
key 27.037 M
key 30.908 Z
key 38.650 N
key 42.521 C
key 46.392 M
key 50.263 Z
key 54.134 N
# bar 0: a soft drone fades in from the floor
down 0.2 1 0.07 0.12 p=0.18
# bar 2: wind, drifting toward rain
down 3.871 2 0.83 0.55 p=0.3
glide 5.806 17.419 2 0.89 0.6 p=0.5
# bar 4: strings, climbing through the voicings as they swell
down 7.742 3 0.25 0.25 p=0.35
glide 9.677 23.226 3 0.27 0.72 p=0.85
# bar 6: the choir, oo opening to ah, twisted clockwise so its filter sweeps with the bars
down 11.613 4 0.43 0.38 p=0.3
glide 13.548 25.161 4 0.5 0.5 p=0.7 twist=-35
# bar 8: glass, twinkling on the sixteenths
down 15.484 5 0.73 0.45 p=0.45
glide 17.419 23.226 5 0.76 0.55 p=0.75
# bar 10: a second pad, saw, wide (a flat finger)
down 19.355 6 0.31 0.5 p=0.4 size=1.6
# bar 12: click: everything held is latched; the hands lift, the bed stays
click 23.526
up 24.306 1
up 24.386 2
up 24.466 3
up 24.546 4
up 24.626 5
up 24.706 6
# bar 13: the grain cloud, octaves of the bed itself
key 25.161 G
# bar 14: hold S, the swell rises for two bars
key 27.097 S down
key 30.918 S up
# bar 16: back on i, freeze the halo at its peak and let the latched layers go: it hangs
key 31.218 F
key 31.268 Delete
# ...and the Pulse scene gates under it in sixteenths, pumping on every beat
key 31.368 I
key 31.468 P
# bar 20: the chords move on (VI, III, VII, i); at each change the freeze melts for a bar
# and catches the new chord from the pulse
# bar 26: thaw
key 50.823 F
# bar 22: a hand picks up the pulse's pad orb and twists it clockwise: from gate to sweep
down 42.581 7 0.30 0.5 p=0.5
glide 42.881 46.452 7 0.31 0.6 p=0.7 twist=-45
up 50.323 7
# bar 28: let it all go
key 54.194 I
key 54.294 G
end 61.935
