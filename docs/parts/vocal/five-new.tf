# Five voices from further afield, on the Vocal page: the singing goes in
# through the microphone (here the script's own singer), and each style turns
# it into an instrument the app had no voice for. 92 bpm.
#
#   TALK BOX  a funk synth up the tube, your mouth shaping it. SUNG: a G-funk
#             line low down, the synth sliding and scooping, a sub under it;
#             two fingers add harmonies that move with it as one hand. Then
#             B for KEYS: the lanes are the keyboard, played legato (the
#             synth glides from key to key, each key pressed harder or
#             lighter, so the synth's filter opens and closes), the voice
#             only says the words
#   THROAT    Tuvan throat singing. SYGYT: a long A drone, pressed, the
#             whistle (its own overtone, pinned) walking a four-bar figure on
#             the overtones in the key, home on the root every bar; then a
#             finger picks the overtone, lane by lane. B: KARGYRAA, the
#             growl, the subharmonic an octave under the voice leading and a
#             vowel walking its overtones. B again: KHOOMEI, softer
#   SITAR     the voice plays a sitar: syllables pluck the string (da and
#             ra in turn, the chikari striking da ra dir da), sung
#             slides bend it (meend), a held note keeps only a little of the
#             vibrato (gamak), the jawari buzzes, tarafs ring; a finger
#             plucks a fifth over the long note
#   GAMELAN   syllables strike bronze bars in pelog, the voice drawn into
#             pelog under them; the kenong and kempul strike the note sung,
#             the gong ageng throbs and sinks; a finger plays the kotekan,
#             nyog cag, then norot over it, landing on the note sung
#   SCREWED   the voice on slowed tape a fourth down, dragging further
#             behind through a long phrase; a finger cuts to the second deck
#             a beat behind and back (the chop), the lowest lane spins it
#             back; then B: a fifth down, the next phrase dropping onto the
#             beat
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
down 8.95 10 0.500 0.60 p=0.3
up 9.66 10
down 9.60 11 0.611 0.60 p=0.45
up 10.01 11
down 9.95 12 0.666 0.60 p=0.6
up 10.36 12
down 10.30 13 0.721 0.60 p=0.8
up 11.01 13
down 10.95 14 0.666 0.60 p=1.0
up 11.36 14
down 11.30 15 0.611 0.60 p=0.8
up 12.01 15
down 11.95 16 0.500 0.60 p=0.5
up 12.66 16
down 12.60 17 0.445 0.60 p=0.35
up 13.01 17
down 12.95 18 0.500 0.60 p=0.9
up 13.70 18
# THROAT: Right once more. SYGYT first: the whistle walks its figure alone,
# then three lanes pick overtones; B for KARGYRAA, the growl an octave down
# with its vowel melody; B again for KHOOMEI.
key 14.10 Right
voice 14.70 5.6 hz=110.0 vowel=o level=0.32
voice 20.50 3.4 hz=110.0 vowel=o level=0.32
down 20.90 1 0.62 0.6 p=0.6
up 21.70 1
down 21.70 2 0.74 0.6 p=0.6
up 22.50 2
down 22.50 3 0.86 0.6 p=0.6
up 23.30 3
key 24.10 B
voice 24.40 5.0 hz=98.0 vowel=a level=0.32
down 27.20 4 0.38 0.6 p=0.6
up 28.20 4
down 28.20 5 0.62 0.6 p=0.6
up 29.20 5
key 29.80 B
voice 30.10 4.4 hz=110.0 vowel=u level=0.32
# SITAR: Right once more.
key 34.90 Right
voice 35.50 0.6 hz=220.0 vowel=a level=0.3
voice 36.20 1.3 hz=261.63 to=293.66 vowel=a level=0.3
voice 37.60 0.4 hz=329.63 vowel=o level=0.3
voice 38.10 0.35 hz=293.66 vowel=a level=0.3
voice 38.55 1.4 hz=261.63 to=246.94 vowel=a level=0.3
voice 40.10 0.35 hz=220.0 vowel=e level=0.3
voice 40.55 0.35 hz=261.63 vowel=a level=0.3
voice 41.00 2.2 hz=329.63 vowel=a level=0.3
down 40.95 1 0.62 0.60 p=0.6
up 42.90 1
# GAMELAN: Right once more.
key 45.10 Right
voice 45.70 0.16 hz=220.0 vowel=a level=0.3
voice 46.03 0.16 hz=329.63 vowel=o level=0.3
voice 46.35 0.16 hz=261.63 vowel=a level=0.3
voice 46.68 0.16 hz=293.66 vowel=o level=0.3
voice 47.00 0.16 hz=293.66 vowel=a level=0.3
voice 47.33 0.16 hz=261.63 vowel=o level=0.3
voice 47.66 0.16 hz=329.63 vowel=a level=0.3
voice 48.31 0.16 hz=293.66 vowel=a level=0.3
voice 48.63 0.16 hz=196.0 vowel=o level=0.3
voice 48.96 0.16 hz=261.63 vowel=a level=0.3
voice 49.29 0.16 hz=220.0 vowel=o level=0.3
voice 49.61 0.16 hz=220.0 vowel=a level=0.3
voice 49.94 0.16 hz=261.63 vowel=o level=0.3
voice 50.26 0.16 hz=196.0 vowel=a level=0.3
voice 50.92 0.16 hz=220.0 vowel=a level=0.3
voice 51.24 0.16 hz=329.63 vowel=o level=0.3
voice 51.57 0.16 hz=261.63 vowel=a level=0.3
voice 51.89 0.16 hz=293.66 vowel=o level=0.3
voice 52.22 0.16 hz=293.66 vowel=a level=0.3
voice 52.55 0.16 hz=261.63 vowel=o level=0.3
voice 52.87 0.16 hz=329.63 vowel=a level=0.3
voice 53.52 0.16 hz=293.66 vowel=a level=0.3
voice 53.85 0.16 hz=196.0 vowel=o level=0.3
voice 54.18 0.16 hz=261.63 vowel=a level=0.3
voice 54.50 0.16 hz=220.0 vowel=o level=0.3
voice 54.83 0.16 hz=220.0 vowel=a level=0.3
voice 55.15 0.16 hz=261.63 vowel=o level=0.3
voice 55.48 0.16 hz=196.0 vowel=a level=0.3
down 48.31 1 0.38 0.62 p=0.6
down 51.00 2 0.14 0.62 p=0.6
up 53.52 2
up 53.52 1
# SCREWED: Right once more.
key 57.20 Right
voice 57.80 0.25 hz=329.63 vowel=a level=0.3
voice 58.13 0.25 hz=329.63 vowel=o level=0.3
voice 58.46 0.3 hz=293.66 vowel=a level=0.3
voice 58.80 0.6 hz=261.63 vowel=o level=0.3
voice 59.45 0.3 hz=220.0 vowel=e level=0.3
voice 59.80 0.5 hz=261.63 vowel=a level=0.3
voice 60.35 0.25 hz=329.63 vowel=a level=0.3
voice 60.65 0.25 hz=392.0 vowel=o level=0.3
voice 60.95 0.3 hz=329.63 vowel=a level=0.3
voice 61.30 0.5 hz=293.66 vowel=o level=0.3
voice 61.85 0.3 hz=261.63 vowel=e level=0.3
voice 62.20 0.5 hz=293.66 vowel=a level=0.3
voice 62.75 0.3 hz=329.63 vowel=o level=0.3
voice 63.10 0.3 hz=293.66 vowel=a level=0.3
voice 63.45 1.4 hz=220.0 vowel=a level=0.3
down 61.95 1 0.50 0.60 p=0.6
up 62.45 1
down 62.95 3 0.80 0.60 p=0.6
up 63.30 3
down 63.80 1 0.50 0.60 p=0.6
up 64.30 1
down 67.90 2 0.144 0.60 p=0.6
up 68.70 2
key 69.20 B
voice 69.40 0.3 hz=329.63 vowel=a level=0.3
voice 69.75 0.3 hz=293.66 vowel=o level=0.3
voice 70.10 1.2 hz=261.63 vowel=a level=0.3
end 76.1
