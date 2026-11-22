# tenfold, page 0 Arrange: a live set, recorded and replayed
#
# The same loops performed live with fingers and keys while O records the
# song: mutes on the bar, a filter ride, echo throws by pressure, the tension
# fader let go high into a drop, a breakdown with a low-pass ride, a build,
# a variation, a key lift and an echo-out. Then P plays the take back from
# background() while page 3 shows.
#
# The loops are made on the part pages the way every page makes them: play,
# then Space loops what was just played (the first, with nothing playing,
# closed on its next ONE, sets the tempo). They are single hits and notes, so
# the first block leans as little as it can on the pages' layouts: the
# drums' near row of pads, the lead's A row of keys, single notes on the
# scale columns of pages 2, 3, 5 and 6 (still starters). Render it:
#   build/tenfold render docs/parts/arrange/live-set.tf out.wav
#   build/tenfold screenshot shot.png docs/parts/arrange/live-set.tf --page 0 --at 64.6

# --- Loops, made on the parts' pages: play, then Space loops what was just played ---
# 1 Drums (page 1, the near row of pads: kick, snare, clap, hat), single hits: two bars,
# kick on the beats, hats off the beat, clap on two and four; Space on the next ONE sets the tempo.
page 0.1 1
tap 0.500 0.12 0.12 p=0.75 len=0.06
tap 0.750 0.88 0.12 p=0.45 len=0.06
tap 1.000 0.12 0.12 p=0.75 len=0.06
tap 1.010 0.62 0.12 p=0.65 len=0.06
tap 1.250 0.88 0.12 p=0.45 len=0.06
tap 1.500 0.12 0.12 p=0.75 len=0.06
tap 1.750 0.88 0.12 p=0.45 len=0.06
tap 2.000 0.12 0.12 p=0.75 len=0.06
tap 2.010 0.62 0.12 p=0.65 len=0.06
tap 2.250 0.88 0.12 p=0.45 len=0.06
tap 2.500 0.12 0.12 p=0.75 len=0.06
tap 2.750 0.88 0.12 p=0.45 len=0.06
tap 3.000 0.12 0.12 p=0.75 len=0.06
tap 3.010 0.62 0.12 p=0.65 len=0.06
tap 3.250 0.88 0.12 p=0.45 len=0.06
tap 3.500 0.12 0.12 p=0.75 len=0.06
tap 3.750 0.88 0.12 p=0.45 len=0.06
tap 4.000 0.12 0.12 p=0.75 len=0.06
tap 4.010 0.62 0.12 p=0.65 len=0.06
tap 4.250 0.88 0.12 p=0.45 len=0.06
key 4.500 Space
# 2 Bass (scale columns): four bars of i-VI-III-VII, the root held on the one, then the off-beats.
page 5.500 2
tap 6.500 0.0333 0.5 p=0.7 len=0.4
tap 7.250 0.0333 0.5 p=0.7 len=0.18
tap 7.750 0.0333 0.5 p=0.7 len=0.18
tap 8.250 0.0333 0.5 p=0.7 len=0.18
tap 8.500 0.3667 0.5 p=0.7 len=0.4
tap 9.250 0.3667 0.5 p=0.7 len=0.18
tap 9.750 0.3667 0.5 p=0.7 len=0.18
tap 10.250 0.3667 0.5 p=0.7 len=0.18
tap 10.500 0.1667 0.5 p=0.7 len=0.4
tap 11.250 0.1667 0.5 p=0.7 len=0.18
tap 11.750 0.1667 0.5 p=0.7 len=0.18
tap 12.250 0.1667 0.5 p=0.7 len=0.18
tap 12.500 0.4333 0.5 p=0.7 len=0.4
tap 13.250 0.4333 0.5 p=0.7 len=0.18
tap 13.750 0.4333 0.5 p=0.7 len=0.18
tap 14.250 0.4333 0.5 p=0.7 len=0.18
key 14.650 Space
# 3 Chords: four bars, a chord on the one, a stab on the and of two, a push on four.
page 15.500 3
tap 16.500 0.0333 0.6 p=0.6 len=0.6
tap 17.250 0.0333 0.7 p=0.6 len=0.2
tap 18.000 0.0333 0.65 p=0.55 len=0.35
tap 18.500 0.3667 0.6 p=0.6 len=0.6
tap 19.250 0.3667 0.7 p=0.6 len=0.2
tap 20.000 0.3667 0.65 p=0.55 len=0.35
tap 20.500 0.1667 0.6 p=0.6 len=0.6
tap 21.250 0.1667 0.7 p=0.6 len=0.2
tap 22.000 0.1667 0.65 p=0.55 len=0.35
tap 22.500 0.4333 0.6 p=0.6 len=0.6
tap 23.250 0.4333 0.7 p=0.6 len=0.2
tap 24.000 0.4333 0.65 p=0.55 len=0.35
key 24.650 Space
# 6 Atmos: four bars of wide chords, held.
page 25.500 6
down 26.500 60 0.0333 0.3 p=0.5
up 28.400 60
down 28.500 61 0.3667 0.3 p=0.5
up 30.400 61
down 30.500 62 0.1667 0.3 p=0.5
up 32.400 62
down 32.500 63 0.4333 0.3 p=0.5
up 34.400 63
key 34.650 Space
# 4 Lead (page 4): a two-bar hook typed on the A row of keys (the scale, an octave up).
page 35.500 4
key 36.500 K down
key 36.900 K up
key 36.750 J down
key 36.950 J up
key 37.250 G down
key 37.450 G up
key 37.500 J down
key 37.900 J up
key 38.000 D down
key 38.400 D up
key 38.500 K down
key 38.900 K up
key 38.750 J down
key 38.950 J up
key 39.250 G down
key 39.450 G up
key 39.500 J down
key 39.900 J up
key 40.000 L down
key 40.400 L up
key 40.650 Space
# 5 Arp: two bars of running eighths.
page 41.500 5
tap 42.500 0.0333 0.5 p=0.5 len=0.12
tap 42.750 0.1667 0.5 p=0.5 len=0.12
tap 43.000 0.3000 0.5 p=0.5 len=0.12
tap 43.250 0.5000 0.5 p=0.5 len=0.12
tap 43.500 0.3000 0.5 p=0.5 len=0.12
tap 43.750 0.1667 0.5 p=0.5 len=0.12
tap 44.000 0.3000 0.5 p=0.5 len=0.12
tap 44.250 0.5000 0.5 p=0.5 len=0.12
tap 44.500 0.1667 0.5 p=0.5 len=0.12
tap 44.750 0.3000 0.5 p=0.5 len=0.12
tap 45.000 0.4333 0.5 p=0.5 len=0.12
tap 45.250 0.6333 0.5 p=0.5 len=0.12
tap 45.500 0.4333 0.5 p=0.5 len=0.12
tap 45.750 0.3000 0.5 p=0.5 len=0.12
tap 46.000 0.4333 0.5 p=0.5 len=0.12
tap 46.250 0.6333 0.5 p=0.5 len=0.12
key 46.650 Space
# 1 Drums again: four on the floor, open hats off the beat, closed hats before the beat and a
# snare pickup, single hits again: for the second drop. Shift+Space makes it a new loop, not a layer.
page 47.500 1
tap 48.500 0.12 0.12 p=0.8 len=0.06
tap 48.750 0.88 0.38 p=0.5 len=0.06
tap 49.000 0.12 0.12 p=0.8 len=0.06
tap 48.875 0.88 0.12 p=0.35 len=0.05
tap 49.250 0.88 0.38 p=0.5 len=0.06
tap 49.500 0.12 0.12 p=0.8 len=0.06
tap 49.375 0.88 0.12 p=0.35 len=0.05
tap 49.750 0.88 0.38 p=0.5 len=0.06
tap 50.000 0.12 0.12 p=0.8 len=0.06
tap 49.875 0.88 0.12 p=0.35 len=0.05
tap 50.250 0.88 0.38 p=0.5 len=0.06
tap 50.375 0.38 0.12 p=0.5 len=0.05
tap 50.500 0.12 0.12 p=0.8 len=0.06
tap 50.375 0.88 0.12 p=0.35 len=0.05
tap 50.750 0.88 0.38 p=0.5 len=0.06
tap 51.000 0.12 0.12 p=0.8 len=0.06
tap 50.875 0.88 0.12 p=0.35 len=0.05
tap 51.250 0.88 0.38 p=0.5 len=0.06
tap 51.500 0.12 0.12 p=0.8 len=0.06
tap 51.375 0.88 0.12 p=0.35 len=0.05
tap 51.750 0.88 0.38 p=0.5 len=0.06
tap 51.875 0.38 0.12 p=0.5 len=0.05
tap 52.000 0.12 0.12 p=0.8 len=0.06
tap 51.875 0.88 0.12 p=0.35 len=0.05
tap 52.250 0.88 0.38 p=0.5 len=0.06
key 52.650 Shift+Space
# --- 0 Arrange: a live set, recorded as a song (O) while it is played ---
page 53.000 0
# Record from the next bar; strip to drums and atmos on that bar (Shift: mutes land on the line).
key 53.500 O
key 53.750 Shift+W
key 53.800 Shift+E
key 53.850 Shift+R
key 53.900 Shift+T
# Intro: a finger on the drums lane rides them up out of a high-pass and back.
down 54.500 20 0.80 0.822
glide 54.600 55.500 20 0.97 0.822
glide 56.000 58.250 20 0.80 0.872
up 58.400 20
# The bass and the arp come in on the bar.
key 58.000 Shift+W
key 58.050 Shift+T
# The arp pressed hard for its last beat: thrown into the echo.
down 61.900 21 0.80 0.316 p=0.5
move 62.000 21 0.80 0.316 p=0.97
up 62.550 21
# The tension fader: up over six beats (filter, wash, pump, the echo roll), let go high: the drop.
down 62.500 22 0.94 0.40
glide 62.600 65.600 22 0.94 0.92
up 65.750 22
# Drop: everything back on the ONE (the chords and the lead too). The lead throws its last beats.
down 69.900 23 0.80 0.443 p=0.5
move 70.000 23 0.80 0.443 p=0.97
up 70.500 23
# Two fingers on the chords: the second one washes them in reverb.
down 71.400 24 0.78 0.569
down 71.500 25 0.84 0.569
glide 71.550 73.250 25 0.84 0.869
up 73.500 25
up 73.550 24
down 73.900 26 0.80 0.443 p=0.5
move 74.000 26 0.80 0.443 p=0.97
up 74.500 26
# Breakdown on the bar: chords and atmos alone, no pump; a finger opens the chords from a low-pass.
key 74.100 N
down 74.500 27 0.80 0.569
move 74.600 27 0.62 0.569
glide 74.700 78.000 27 0.80 0.569
up 78.100 27
# B: a build to the end of the phrase; the arp first, the drums half way, the bass on the drop.
key 77.500 B
# Second drop; V moves every part to its next clip on the bar, L lifts the key for the last bars.
key 86.000 V
key 90.000 L
# X: the last bar pours into the echo, then everything stops and the tape slows.
key 92.000 X
key 95.000 O
# The song plays back from the next bar while the chords page shows: background() runs it.
key 97.000 P
page 98.000 3
# --- 9 Visual: the take plays back from the Song page while the hands shape the picture ---
page 98.200 9
down 100.0 1 0.50 0.50 p=0.5
glide 100.1 103.6 1 0.80 0.72 p=0.95
up 103.8 1
down 104.5 11 0.250 0.450 p=0.6
glide 104.6 109.6 11 0.670 0.630 p=0.7
up 109.8 11
down 104.5 12 0.300 0.550 p=0.6
glide 104.6 109.6 12 0.720 0.730 p=0.7
up 109.8 12
down 104.5 13 0.350 0.450 p=0.6
glide 104.6 109.6 13 0.770 0.630 p=0.7
up 109.8 13
down 110.5 21 0.575 0.500 p=0.6
down 110.5 22 0.425 0.500 p=0.6
glide 110.600 111.050 21 0.578 0.546 p=0.7
glide 110.600 111.050 22 0.422 0.454 p=0.7
glide 111.050 111.500 21 0.569 0.594 p=0.7
glide 111.050 111.500 22 0.431 0.406 p=0.7
glide 111.500 111.950 21 0.548 0.637 p=0.7
glide 111.500 111.950 22 0.452 0.363 p=0.7
glide 111.950 112.400 21 0.517 0.668 p=0.7
glide 111.950 112.400 22 0.483 0.332 p=0.7
glide 112.400 112.850 21 0.478 0.679 p=0.7
glide 112.400 112.850 22 0.522 0.321 p=0.7
glide 112.850 113.300 21 0.436 0.666 p=0.7
glide 112.850 113.300 22 0.564 0.334 p=0.7
glide 113.300 113.750 21 0.398 0.628 p=0.7
glide 113.300 113.750 22 0.602 0.372 p=0.7
glide 113.750 114.200 21 0.369 0.568 p=0.7
glide 113.750 114.200 22 0.631 0.432 p=0.7
up 114.5 21
up 114.5 22
down 115.0 31 0.625 0.450 p=0.6
down 115.0 32 0.539 0.640 p=0.6
down 115.0 33 0.399 0.568 p=0.6
down 115.0 34 0.399 0.332 p=0.6
down 115.0 35 0.539 0.260 p=0.6
glide 115.100 115.600 31 0.624 0.420 p=0.8
glide 115.100 115.600 32 0.556 0.629 p=0.8
glide 115.100 115.600 33 0.411 0.590 p=0.8
glide 115.100 115.600 34 0.389 0.358 p=0.8
glide 115.100 115.600 35 0.520 0.253 p=0.8
glide 115.600 116.100 31 0.619 0.391 p=0.8
glide 115.600 116.100 32 0.572 0.613 p=0.8
glide 115.600 116.100 33 0.425 0.610 p=0.8
glide 115.600 116.100 34 0.382 0.386 p=0.8
glide 115.600 116.100 35 0.502 0.250 p=0.8
glide 116.100 116.600 31 0.613 0.363 p=0.8
glide 116.100 116.600 32 0.586 0.594 p=0.8
glide 116.100 116.600 33 0.441 0.626 p=0.8
glide 116.100 116.600 34 0.377 0.415 p=0.8
glide 116.100 116.600 35 0.483 0.252 p=0.8
glide 116.600 117.100 31 0.603 0.337 p=0.8
glide 116.600 117.100 32 0.599 0.572 p=0.8
glide 116.600 117.100 33 0.458 0.638 p=0.8
glide 116.600 117.100 34 0.375 0.444 p=0.8
glide 116.600 117.100 35 0.465 0.258 p=0.8
glide 117.100 117.600 31 0.591 0.314 p=0.8
glide 117.100 117.600 32 0.609 0.547 p=0.8
glide 117.100 117.600 33 0.476 0.646 p=0.8
glide 117.100 117.600 34 0.376 0.474 p=0.8
glide 117.100 117.600 35 0.447 0.269 p=0.8
glide 117.600 118.100 31 0.578 0.293 p=0.8
glide 117.600 118.100 32 0.617 0.520 p=0.8
glide 117.600 118.100 33 0.495 0.650 p=0.8
glide 117.600 118.100 34 0.380 0.504 p=0.8
glide 117.600 118.100 35 0.431 0.283 p=0.8
glide 118.100 118.600 31 0.562 0.277 p=0.8
glide 118.100 118.600 32 0.622 0.491 p=0.8
glide 118.100 118.600 33 0.513 0.649 p=0.8
glide 118.100 118.600 34 0.386 0.532 p=0.8
glide 118.100 118.600 35 0.416 0.302 p=0.8
glide 118.600 119.100 31 0.545 0.264 p=0.8
glide 118.600 119.100 32 0.625 0.461 p=0.8
glide 118.600 119.100 33 0.532 0.643 p=0.8
glide 118.600 119.100 34 0.395 0.558 p=0.8
glide 118.600 119.100 35 0.403 0.323 p=0.8
up 119.3 31
up 119.3 32
up 119.3 33
up 119.3 34
up 119.3 35
down 119.8 41 0.548 0.524 p=0.5
down 119.8 42 0.485 0.576 p=0.5
down 119.8 43 0.452 0.476 p=0.5
down 119.8 44 0.515 0.424 p=0.5
glide 119.900 120.300 41 0.560 0.541 p=0.8
glide 119.900 120.300 42 0.474 0.597 p=0.8
glide 119.900 120.300 43 0.440 0.459 p=0.8
glide 119.900 120.300 44 0.526 0.403 p=0.8
glide 120.300 120.700 41 0.571 0.562 p=0.8
glide 120.300 120.700 42 0.461 0.614 p=0.8
glide 120.300 120.700 43 0.429 0.438 p=0.8
glide 120.300 120.700 44 0.539 0.386 p=0.8
glide 120.700 121.100 41 0.580 0.588 p=0.8
glide 120.700 121.100 42 0.445 0.628 p=0.8
glide 120.700 121.100 43 0.420 0.412 p=0.8
glide 120.700 121.100 44 0.555 0.372 p=0.8
glide 121.100 121.500 41 0.586 0.616 p=0.8
glide 121.100 121.500 42 0.428 0.638 p=0.8
glide 121.100 121.500 43 0.414 0.384 p=0.8
glide 121.100 121.500 44 0.572 0.362 p=0.8
glide 121.500 121.900 41 0.589 0.647 p=0.8
glide 121.500 121.900 42 0.408 0.643 p=0.8
glide 121.500 121.900 43 0.411 0.353 p=0.8
glide 121.500 121.900 44 0.592 0.357 p=0.8
glide 121.900 122.300 41 0.589 0.680 p=0.8
glide 121.900 122.300 42 0.387 0.643 p=0.8
glide 121.900 122.300 43 0.411 0.320 p=0.8
glide 121.900 122.300 44 0.613 0.357 p=0.8
glide 122.300 122.700 41 0.586 0.715 p=0.8
glide 122.300 122.700 42 0.366 0.638 p=0.8
glide 122.300 122.700 43 0.414 0.285 p=0.8
glide 122.300 122.700 44 0.634 0.362 p=0.8
glide 122.700 123.100 41 0.579 0.750 p=0.8
glide 122.700 123.100 42 0.344 0.627 p=0.8
glide 122.700 123.100 43 0.421 0.250 p=0.8
glide 122.700 123.100 44 0.656 0.373 p=0.8
glide 123.2 123.9 41 0.523 0.571 p=0.6
glide 123.2 123.9 42 0.455 0.536 p=0.6
glide 123.2 123.9 43 0.477 0.429 p=0.6
glide 123.2 123.9 44 0.545 0.464 p=0.6
up 124.0 41
up 124.0 42
up 124.0 43
up 124.0 44
down 124.3 51 0.120 0.350 p=0.8
down 124.3 52 0.170 0.480 p=0.8
down 124.3 53 0.230 0.550 p=0.8
down 124.3 54 0.290 0.500 p=0.8
down 124.3 55 0.330 0.300 p=0.8
down 124.3 56 0.670 0.300 p=0.8
down 124.3 57 0.710 0.500 p=0.8
down 124.3 58 0.770 0.550 p=0.8
down 124.3 59 0.830 0.480 p=0.8
down 124.3 60 0.880 0.350 p=0.8
glide 124.2 128.5 51 0.170 0.600 p=0.9
glide 124.2 128.5 52 0.220 0.730 p=0.9
glide 124.2 128.5 53 0.280 0.800 p=0.9
glide 124.2 128.5 54 0.340 0.750 p=0.9
glide 124.2 128.5 55 0.380 0.550 p=0.9
glide 124.2 128.5 56 0.550 0.150 p=0.9
glide 124.2 128.5 57 0.590 0.350 p=0.9
glide 124.2 128.5 58 0.650 0.400 p=0.9
glide 124.2 128.5 59 0.710 0.330 p=0.9
glide 124.2 128.5 60 0.760 0.200 p=0.9
up 129.0 51
up 129.0 52
up 129.0 53
up 129.0 54
up 129.0 55
up 129.0 56
up 129.0 57
up 129.0 58
up 129.0 59
up 129.0 60
key 130.5 S
key 133.0 K
key 133.3 K
key 135.5 D
key 137.5 Space
key 139.0 F
key 141.0 G
key 141.2 K
end 143.5
