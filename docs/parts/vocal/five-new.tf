# Five voices from further afield, on the Vocal page: the singing goes in
# through the microphone (here the script's own singer), and each style turns
# it into an instrument the app had no voice for.
#
#   TALK BOX  a funk synth up the tube, your mouth shaping it: a G-funk line,
#             a finger adding a synth harmony a third up for the hook
#   THROAT    Tuvan khoomei: a long A drone, a growl an octave under it, the
#             whistle walking its overtones a beat at a time; then a finger
#             picks the overtone (an octave, a fifth)
#   SITAR     long notes with slides between them (meend): thirteen strings
#             tuned to the key ring on under every note and buzz as they swell
#   GAMELAN   short syllables, each one a bronze bar struck at the note, the
#             pair beating; a finger a fifth up strikes a second bar with each
#   SCREWED   the same kind of line, slowed: a third down on dark tape, the
#             second deck a beat behind over the back half of every bar
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
voice 1.10 0.3 hz=220.0 vowel=a level=0.3
voice 1.45 0.3 hz=261.63 vowel=o level=0.3
voice 1.80 0.25 hz=293.66 vowel=a level=0.3
voice 2.10 0.55 hz=329.63 vowel=o level=0.3
voice 2.70 0.3 hz=293.66 vowel=e level=0.3
voice 3.05 0.9 hz=261.63 vowel=a level=0.3
voice 4.30 0.3 hz=329.63 vowel=a level=0.3
voice 4.65 0.3 hz=392.0 vowel=o level=0.3
voice 5.00 0.6 hz=440.0 vowel=a level=0.3
voice 5.65 0.3 hz=392.0 vowel=e level=0.3
voice 6.00 0.3 hz=329.63 vowel=o level=0.3
voice 6.35 1.2 hz=293.66 vowel=a level=0.3
down 4.20 1 0.500 0.58 p=0.6
up 7.60 1
# THROAT: Right once more.
key 8.90 Right
voice 9.50 3.4 hz=110.0 vowel=o level=0.32
voice 13.10 3.2 hz=110.0 vowel=o level=0.32
down 13.70 2 0.856 0.55 p=0.6
up 14.50 2
down 14.60 3 0.619 0.70 p=0.6
up 15.40 3
down 15.50 4 0.856 0.75 p=0.6
up 16.30 4
# SITAR: Right once more.
key 17.30 Right
voice 17.90 1.3 hz=293.66 to=329.63 vowel=a level=0.3
voice 19.40 0.5 hz=329.63 vowel=a level=0.3
voice 20.00 1.6 hz=261.63 to=293.66 vowel=o level=0.3
voice 21.80 0.45 hz=220.0 vowel=a level=0.3
voice 22.35 0.45 hz=261.63 vowel=e level=0.3
voice 22.90 2.0 hz=220.0 vowel=a level=0.3
# GAMELAN: Right once more.
key 25.70 Right
voice 26.30 0.16 hz=220.0 vowel=a level=0.3
voice 26.63 0.16 hz=261.63 vowel=o level=0.3
voice 26.95 0.16 hz=293.66 vowel=a level=0.3
voice 27.28 0.16 hz=329.63 vowel=o level=0.3
voice 27.60 0.16 hz=293.66 vowel=a level=0.3
voice 27.93 0.16 hz=261.63 vowel=o level=0.3
voice 28.26 0.16 hz=220.0 vowel=a level=0.3
voice 28.58 0.16 hz=196.0 vowel=o level=0.3
voice 28.91 0.16 hz=220.0 vowel=a level=0.3
voice 29.23 0.16 hz=261.63 vowel=o level=0.3
voice 29.56 0.16 hz=329.63 vowel=a level=0.3
voice 29.89 0.16 hz=392.0 vowel=o level=0.3
voice 30.21 0.16 hz=329.63 vowel=a level=0.3
voice 30.54 0.16 hz=293.66 vowel=o level=0.3
voice 30.86 0.16 hz=261.63 vowel=a level=0.3
voice 31.19 0.16 hz=220.0 vowel=o level=0.3
down 28.86 5 0.619 0.55 p=0.55
up 31.52 5
# SCREWED: Right once more.
key 34.10 Right
voice 34.70 0.25 hz=329.63 vowel=a level=0.3
voice 35.03 0.25 hz=329.63 vowel=o level=0.3
voice 35.36 0.3 hz=293.66 vowel=a level=0.3
voice 35.70 0.6 hz=261.63 vowel=o level=0.3
voice 36.40 0.3 hz=220.0 vowel=e level=0.3
voice 36.75 1.0 hz=261.63 vowel=a level=0.3
voice 38.00 0.25 hz=329.63 vowel=a level=0.3
voice 38.33 0.25 hz=392.0 vowel=o level=0.3
voice 38.66 0.3 hz=329.63 vowel=a level=0.3
voice 39.00 0.6 hz=293.66 vowel=o level=0.3
voice 39.70 0.3 hz=261.63 vowel=e level=0.3
voice 40.05 1.4 hz=220.0 vowel=a level=0.3
end 43.0
