# Five voices from further afield, on the Vocal page: the singing goes in
# through the microphone (here the script's own singer), and each style turns
# it into an instrument the app had no voice for. 92 bpm.
#
#   TALK BOX  a funk synth up the tube, your mouth shaping it. SUNG: a G-funk
#             line low down, the synth sliding and scooping, a sub under it;
#             two fingers add harmonies that move with it as one hand. Then
#             B for KEYS: the lanes are the keyboard, played legato (the
#             synth glides from key to key), the voice only says the words
#   THROAT    Tuvan khoomei: a long A drone growling with kargyraa's period
#             doubling, the pinned whistle singing its two-bar figure on the
#             overtones in the key, home on the root every bar; then a
#             finger picks the overtone, lane by lane
#   SITAR     the voice plays a sitar: syllables pluck the string, sung
#             slides bend it (meend), a held note keeps only a little of the
#             vibrato (gamak), the jawari buzzes, tarafs ring; a finger
#             plucks a fifth over the long note
#   GAMELAN   syllables strike bronze bars in pelog, the voice drawn into
#             pelog under them; the gongs mark the cycle; a finger plays the
#             kotekan, two parts interlocking on the sixteenths
#   SCREWED   the voice on slowed tape a fourth down, dragging further
#             behind through a long phrase until it stops and drops onto
#             the voice; a finger chops the last word, the lowest lane spins
#             it back; then B: a fifth down
#
# Render: build/tenfold render docs/parts/vocal/five-new.tf five-new.wav
page 0 8
tempo 0.01 92

# TALK BOX: Right six times from NATURAL.
key 0.50 Right
key 0.53 Right
key 0.56 Right
key 0.59 Right
key 0.62 Right
key 0.65 Right
voice 1.10 0.25 hz=110.0 vowel=u level=0.3
voice 1.40 0.35 hz=110.0 vowel=a level=0.3
voice 1.80 0.25 hz=130.81 vowel=o level=0.3
voice 2.10 0.5 hz=164.81 vowel=a level=0.3
voice 2.65 0.25 hz=146.83 vowel=e level=0.3
voice 2.95 0.3 hz=130.81 vowel=u level=0.3
voice 3.30 0.9 hz=110.0 vowel=a level=0.3
voice 4.40 0.3 hz=196.0 vowel=o level=0.3
voice 4.75 0.3 hz=164.81 vowel=a level=0.3
voice 5.10 0.25 hz=146.83 vowel=u level=0.3
voice 5.40 0.45 hz=164.81 vowel=a level=0.3
voice 5.90 0.6 hz=220.0 vowel=o level=0.3
voice 6.55 0.3 hz=196.0 vowel=e level=0.3
voice 6.90 1.3 hz=164.81 vowel=a level=0.3
down 4.30 1 0.50 0.60 p=0.6
down 4.30 2 0.62 0.60 p=0.5
up 8.30 1
up 8.30 2
key 8.70 B
voice 9.00 0.28 hz=220.0 vowel=a level=0.3
voice 9.33 0.28 hz=220.0 vowel=o level=0.3
voice 9.66 0.28 hz=220.0 vowel=e level=0.3
voice 9.99 0.28 hz=220.0 vowel=a level=0.3
voice 10.32 0.28 hz=220.0 vowel=u level=0.3
voice 10.65 0.28 hz=220.0 vowel=a level=0.3
voice 10.98 0.28 hz=220.0 vowel=o level=0.3
voice 11.31 0.28 hz=220.0 vowel=e level=0.3
voice 11.64 0.28 hz=220.0 vowel=a level=0.3
voice 11.97 0.28 hz=220.0 vowel=o level=0.3
voice 12.30 0.28 hz=220.0 vowel=a level=0.3
voice 12.63 0.28 hz=220.0 vowel=e level=0.3
voice 12.96 0.28 hz=220.0 vowel=o level=0.3
voice 13.29 0.28 hz=220.0 vowel=a level=0.3
down 8.95 10 0.500 0.60 p=0.6
up 9.66 10
down 9.60 11 0.611 0.60 p=0.6
up 10.01 11
down 9.95 12 0.666 0.60 p=0.6
up 10.36 12
down 10.30 13 0.721 0.60 p=0.6
up 11.01 13
down 10.95 14 0.666 0.60 p=0.6
up 11.36 14
down 11.30 15 0.611 0.60 p=0.6
up 12.01 15
down 11.95 16 0.500 0.60 p=0.6
up 12.66 16
down 12.60 17 0.445 0.60 p=0.6
up 13.01 17
down 12.95 18 0.500 0.60 p=0.6
up 13.70 18
# THROAT: Right once more.
key 14.10 Right
voice 14.70 5.6 hz=110.0 vowel=o level=0.32
voice 20.50 3.4 hz=110.0 vowel=o level=0.32
down 20.90 1 0.62 0.6 p=0.6
up 21.70 1
down 21.70 2 0.74 0.6 p=0.6
up 22.50 2
down 22.50 3 0.86 0.6 p=0.6
up 23.30 3
# SITAR: Right once more.
key 24.50 Right
voice 25.10 0.6 hz=220.0 vowel=a level=0.3
voice 25.80 1.3 hz=261.63 to=293.66 vowel=a level=0.3
voice 27.20 0.4 hz=329.63 vowel=o level=0.3
voice 27.70 0.35 hz=293.66 vowel=a level=0.3
voice 28.15 1.4 hz=261.63 to=246.94 vowel=a level=0.3
voice 29.70 0.35 hz=220.0 vowel=e level=0.3
voice 30.15 0.35 hz=261.63 vowel=a level=0.3
voice 30.60 2.2 hz=329.63 vowel=a level=0.3
down 30.55 1 0.62 0.60 p=0.6
up 32.50 1
# GAMELAN: Right once more.
key 34.70 Right
voice 35.30 0.16 hz=220.0 vowel=a level=0.3
voice 35.63 0.16 hz=329.63 vowel=o level=0.3
voice 35.95 0.16 hz=261.63 vowel=a level=0.3
voice 36.28 0.16 hz=293.66 vowel=o level=0.3
voice 36.60 0.16 hz=293.66 vowel=a level=0.3
voice 36.93 0.16 hz=261.63 vowel=o level=0.3
voice 37.26 0.16 hz=329.63 vowel=a level=0.3
voice 37.91 0.16 hz=293.66 vowel=a level=0.3
voice 38.23 0.16 hz=196.0 vowel=o level=0.3
voice 38.56 0.16 hz=261.63 vowel=a level=0.3
voice 38.89 0.16 hz=220.0 vowel=o level=0.3
voice 39.21 0.16 hz=220.0 vowel=a level=0.3
voice 39.54 0.16 hz=261.63 vowel=o level=0.3
voice 39.86 0.16 hz=196.0 vowel=a level=0.3
voice 40.52 0.16 hz=220.0 vowel=a level=0.3
voice 40.84 0.16 hz=329.63 vowel=o level=0.3
voice 41.17 0.16 hz=261.63 vowel=a level=0.3
voice 41.49 0.16 hz=293.66 vowel=o level=0.3
voice 41.82 0.16 hz=293.66 vowel=a level=0.3
voice 42.15 0.16 hz=261.63 vowel=o level=0.3
voice 42.47 0.16 hz=329.63 vowel=a level=0.3
voice 43.12 0.16 hz=293.66 vowel=a level=0.3
voice 43.45 0.16 hz=196.0 vowel=o level=0.3
voice 43.78 0.16 hz=261.63 vowel=a level=0.3
voice 44.10 0.16 hz=220.0 vowel=o level=0.3
voice 44.43 0.16 hz=220.0 vowel=a level=0.3
voice 44.75 0.16 hz=261.63 vowel=o level=0.3
voice 45.08 0.16 hz=196.0 vowel=a level=0.3
down 37.91 1 0.50 0.62 p=0.6
up 43.12 1
# SCREWED: Right once more.
key 46.80 Right
voice 47.40 0.25 hz=329.63 vowel=a level=0.3
voice 47.73 0.25 hz=329.63 vowel=o level=0.3
voice 48.06 0.3 hz=293.66 vowel=a level=0.3
voice 48.40 0.6 hz=261.63 vowel=o level=0.3
voice 49.05 0.3 hz=220.0 vowel=e level=0.3
voice 49.40 0.5 hz=261.63 vowel=a level=0.3
voice 49.95 0.25 hz=329.63 vowel=a level=0.3
voice 50.25 0.25 hz=392.0 vowel=o level=0.3
voice 50.55 0.3 hz=329.63 vowel=a level=0.3
voice 50.90 0.5 hz=293.66 vowel=o level=0.3
voice 51.45 0.3 hz=261.63 vowel=e level=0.3
voice 51.80 0.5 hz=293.66 vowel=a level=0.3
voice 52.35 0.3 hz=329.63 vowel=o level=0.3
voice 52.70 0.3 hz=293.66 vowel=a level=0.3
voice 53.05 1.4 hz=220.0 vowel=a level=0.3
down 56.00 1 0.50 0.60 p=0.6
up 57.30 1
down 57.50 2 0.144 0.60 p=0.6
up 58.30 2
key 58.80 B
voice 59.00 0.3 hz=329.63 vowel=a level=0.3
voice 59.35 0.3 hz=293.66 vowel=o level=0.3
voice 59.70 1.2 hz=261.63 vowel=a level=0.3
end 63.7
