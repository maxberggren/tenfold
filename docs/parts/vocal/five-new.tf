# Five voices from further afield, on the Vocal page: the singing goes in
# through the microphone (here the script's own singer), and each style turns
# it into an instrument the app had no voice for. 92 bpm.
#
#   TALK BOX  a funk synth up the tube, your mouth shaping it: a G-funk line
#             low down, the synth sliding between notes and scooping into
#             each phrase, a sub octave under it; then two fingers add synth
#             harmonies, a third and a fifth up, spread left and right
#   THROAT    Tuvan khoomei: a long A drone, the whistle (its own overtones)
#             walking the ones in the key a beat at a time, a growl under;
#             then a finger picks the overtone, lane by lane
#   SITAR     the voice plays a sitar: syllables pluck the string, sung
#             slides bend it (meend), the jawari buzzes, the chikari strike
#             the beat; a finger plucks a fifth over the long note
#   GAMELAN   syllables strike bronze bars in pelog; the gongs mark the
#             cycle (the gong ageng every fourth bar); a finger plays the
#             kotekan, two parts interlocking on the sixteenths
#   SCREWED   the voice on slowed tape, a fourth down, dragging behind and
#             spun back at the bar; a finger chops the last word, doubled
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
# THROAT: Right once more.
key 8.90 Right
voice 9.50 4.2 hz=110.0 vowel=o level=0.32
voice 14.00 3.6 hz=110.0 vowel=o level=0.32
down 14.50 1 0.62 0.6 p=0.6
up 15.40 1
down 15.40 2 0.74 0.6 p=0.6
up 16.30 2
down 16.30 3 0.86 0.6 p=0.6
up 17.10 3
down 17.10 4 0.62 0.6 p=0.6
up 17.50 4
# SITAR: Right once more.
key 18.30 Right
voice 18.90 0.6 hz=220.0 vowel=a level=0.3
voice 19.60 1.3 hz=261.63 to=293.66 vowel=a level=0.3
voice 21.00 0.4 hz=329.63 vowel=o level=0.3
voice 21.50 0.35 hz=293.66 vowel=a level=0.3
voice 21.95 1.4 hz=261.63 to=246.94 vowel=a level=0.3
voice 23.50 0.35 hz=220.0 vowel=e level=0.3
voice 23.95 0.35 hz=261.63 vowel=a level=0.3
voice 24.40 2.0 hz=329.63 to=293.66 vowel=a level=0.3
down 24.35 1 0.62 0.60 p=0.6
up 26.30 1
# GAMELAN: Right once more.
key 27.70 Right
voice 28.30 0.16 hz=220.0 vowel=a level=0.3
voice 28.63 0.16 hz=329.63 vowel=o level=0.3
voice 28.95 0.16 hz=261.63 vowel=a level=0.3
voice 29.28 0.16 hz=293.66 vowel=o level=0.3
voice 29.60 0.16 hz=293.66 vowel=a level=0.3
voice 29.93 0.16 hz=261.63 vowel=o level=0.3
voice 30.26 0.16 hz=329.63 vowel=a level=0.3
voice 30.91 0.16 hz=293.66 vowel=a level=0.3
voice 31.23 0.16 hz=196.0 vowel=o level=0.3
voice 31.56 0.16 hz=261.63 vowel=a level=0.3
voice 31.89 0.16 hz=220.0 vowel=o level=0.3
voice 32.21 0.16 hz=220.0 vowel=a level=0.3
voice 32.54 0.16 hz=261.63 vowel=o level=0.3
voice 32.87 0.16 hz=196.0 vowel=a level=0.3
voice 33.52 0.16 hz=220.0 vowel=a level=0.3
voice 33.84 0.16 hz=329.63 vowel=o level=0.3
voice 34.17 0.16 hz=261.63 vowel=a level=0.3
voice 34.50 0.16 hz=293.66 vowel=o level=0.3
voice 34.82 0.16 hz=293.66 vowel=a level=0.3
voice 35.15 0.16 hz=261.63 vowel=o level=0.3
voice 35.47 0.16 hz=329.63 vowel=a level=0.3
voice 36.13 0.16 hz=293.66 vowel=a level=0.3
voice 36.45 0.16 hz=196.0 vowel=o level=0.3
voice 36.78 0.16 hz=261.63 vowel=a level=0.3
voice 37.10 0.16 hz=220.0 vowel=o level=0.3
voice 37.43 0.16 hz=220.0 vowel=a level=0.3
voice 37.76 0.16 hz=261.63 vowel=o level=0.3
voice 38.08 0.16 hz=196.0 vowel=a level=0.3
voice 38.73 0.16 hz=220.0 vowel=a level=0.3
voice 39.06 0.16 hz=329.63 vowel=o level=0.3
down 30.91 1 0.50 0.62 p=0.6
up 36.13 1
# SCREWED: Right once more.
key 42.09 Right
voice 42.69 0.25 hz=329.63 vowel=a level=0.3
voice 43.02 0.25 hz=329.63 vowel=o level=0.3
voice 43.35 0.3 hz=293.66 vowel=a level=0.3
voice 43.69 0.6 hz=261.63 vowel=o level=0.3
voice 44.39 0.3 hz=220.0 vowel=e level=0.3
voice 44.74 1.0 hz=261.63 vowel=a level=0.3
voice 46.19 0.25 hz=329.63 vowel=a level=0.3
voice 46.52 0.25 hz=392.0 vowel=o level=0.3
voice 46.85 0.3 hz=329.63 vowel=a level=0.3
voice 47.19 0.6 hz=293.66 vowel=o level=0.3
voice 47.89 0.3 hz=261.63 vowel=e level=0.3
voice 48.24 0.9 hz=220.0 vowel=a level=0.3
down 49.39 1 0.50 0.60 p=0.6
up 50.69 1
end 52.4
