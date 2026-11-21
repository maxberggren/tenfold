# Ten sounds: the two that reach past EDM, played the way they want to be.
# Mini (→ eight times), a vintage mono synth: a hard-pressed funk line that
# quacks and always glides, a long slide up an octave, a held note, its
# vibrato coming late, a harmony a third up, then a light touch for the
# G-funk whistle. Flute (→ once more): a slow, breathy melody, tongued notes
# and slurred ones, the vibrato growing out of a held note, a wiggle of your
# own, a twist bend, and a throw into the echo for the dub.
# Render: build/tenfold render docs/parts/lead/ten-sounds.tf ten.wav
page 0 4
tempo 0.01 96
key 0.05 Right
key 0.08 Right
key 0.11 Right
key 0.14 Right
key 0.17 Right
key 0.20 Right
key 0.23 Right
key 0.26 Right
# Mini, pressed hard so every attack quacks: detached notes glide into each
# other (portamento), legato too.
down 1.0 1 0.6100 0.550 p=0.85
up 1.28 1
down 1.32 2 0.6620 0.550 p=0.85
up 1.58 2
down 1.63 3 0.7140 0.600 p=0.85
down 1.94 4 0.8180 0.600 p=0.85
up 1.98 3
up 2.4 4
down 2.5 5 0.7140 0.550 p=0.85
up 2.8 5
down 2.85 6 0.6100 0.550 p=0.85
up 3.4 6
# A slide up an octave and a held note: the vibrato comes by itself.
down 3.6 7 0.4020 0.500 p=0.6
glide 3.7 4.3 7 0.6100 0.700 p=0.7
glide 4.3 6.2 7 0.6100 0.800 p=0.85
up 6.2 7
# A harmony a third up under the left hand, then a phrase over it.
down 6.5 20 0.08 0.58 p=0.6
down 6.6 8 0.7140 0.600 p=0.6
up 7.0 8
down 7.05 9 0.7660 0.600 p=0.6
up 7.4 9
down 7.45 10 0.8180 0.650 p=0.6
glide 7.5 8.6 10 0.8180 0.750 p=0.8
up 8.6 10
up 8.7 20
# A G-funk whistle: a light finger low on the pad, high on the neck,
# sliding up to the top and settling back, its vibrato coming by itself.
down 8.9 30 0.8180 0.150 p=0.35
glide 9.3 9.6 30 0.9740 0.150 p=0.35
glide 10.2 10.4 30 0.9220 0.150 p=0.35
glide 10.8 11.0 30 0.8180 0.150 p=0.35
up 11.9 30
key 12.4 Right
# Flute: a slow melody, the first notes tongued (a puff of air on each).
down 12.8 11 0.6100 0.550 p=0.5
up 13.6 11
down 13.7 12 0.7140 0.550 p=0.5
up 14.3 12
# Slurred: a second finger lands while the first is held.
down 14.4 13 0.8180 0.600 p=0.55
down 14.9 14 0.8700 0.600 p=0.55
up 14.94 13
down 15.4 15 0.8180 0.600 p=0.55
up 15.44 14
# Held: the vibrato grows out of it, swelling as the finger leans in.
glide 15.5 17.4 15 0.8180 0.650 p=0.85
up 17.4 15
# A wiggle of your own, then a twist bend up a step and back.
down 17.6 16 0.7140 0.550 p=0.55 twist=0
glide 17.8 17.9 16 0.7210 0.550
glide 17.9 18.0 16 0.7070 0.550
glide 18.0 18.1 16 0.7210 0.550
glide 18.1 18.2 16 0.7070 0.550
glide 18.2 18.3 16 0.7210 0.550
glide 18.3 18.4 16 0.7140 0.550
glide 18.6 18.9 16 0.7140 0.550 twist=-60
glide 19.1 19.4 16 0.7140 0.550 twist=0
up 19.6 16
# The last note thrown into the echo, the repeats left to ring.
down 19.8 17 0.6100 0.600 p=0.6
down 20.0 21 0.18 0.25 p=0.8
up 20.6 17
up 20.8 21
end 24.2
