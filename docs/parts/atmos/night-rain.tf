# Night rain: a lo-fi bed, reshaped by hand, with a loop of glass swells and a key change.
# 124 BPM. The Nostalgia scene lays vinyl, a hollow pad and a drone; a hand picks up the
# pad's orb and moves it; rain joins; four taps and a hold in the glass column loop as
# swells; the bed walks VI, VII, i on its own; then the key moves a fifth down and
# everything, loop included, follows.
page 0.05 6
# the scene, on one key
key 0.2 T
# bar 2: pick up the pad's orb (it lies at x 0.35, y 0.3) and carry it up, twisted
# clockwise: the voicing climbs and the filter starts to sweep with the bars
down 3.871 1 0.35 0.3 p=0.5
glide 4.071 9.677 1 0.33 0.55 p=0.65 twist=-30
up 9.877 1
# bar 4: rain, pressed firmer as it thickens
down 7.742 2 0.86 0.45 p=0.4
glide 9.677 15.484 2 0.86 0.5 p=0.75
click 15.484
up 15.884 2
# bar 9: L arms a tap loop (the pad says so); four quick firm taps in the glass column
# on the beat, then the fifth held: the swells loop in slot 1, and the loop disarms
key 16.919 L
tap 17.419 0.72 0.62 p=0.85
tap 17.903 0.72 0.62 p=0.85
tap 18.387 0.72 0.62 p=0.85
tap 18.871 0.72 0.62 p=0.85
down 19.354 3 0.72 0.62 p=0.85
up 19.854 3
# bar 11: the bed's own chords: VI, VII, then i, two bars each
key 21.230 N
key 25.101 M
key 28.972 Z
# bar 13: a choir, oo, light and wide, rising late
down 25.161 4 0.42 0.35 p=0.25 size=1.6
glide 27.097 34.839 4 0.45 0.55 p=0.55 size=1.6
up 36.774 4
# bar 19: the key of D (Ctrl+Left: a fifth down); the bed, the scene and the loop follow
key 36.714 Ctrl+Left
# bar 25: let it all go
key 48.387 Backspace
key 48.387 Delete
end 56.129
