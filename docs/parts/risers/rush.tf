# Rush: an eight-bar build of harmonised pitch risers, with layers added from
# the keyboard, cut short by a click three bars in: the drop comes on the next
# bar line and everything rising rushes into it, through a half-beat gap.
# Then an echo throw and a reverb wash from the keys. 128 BPM, A minor.
#
# The groove, made the way the studio makes loops: play, then Space.
# Drums (page 1, the 4x4 pads; the near row is kick, snare, clap, hat): kick on
# every beat, hats between, clap on 2 and 4, then Space where the next ONE
# would land: a one-bar loop that sets 128 BPM.
page 0 1
tap 0.500 0.12 0.12 p=0.7
tap 0.734 0.88 0.12 p=0.5
tap 0.969 0.12 0.12 p=0.7
tap 0.969 0.62 0.12 p=0.6
tap 1.203 0.88 0.12 p=0.5
tap 1.438 0.12 0.12 p=0.7
tap 1.672 0.88 0.12 p=0.5
tap 1.906 0.12 0.12 p=0.7
tap 1.906 0.62 0.12 p=0.6
tap 2.141 0.88 0.12 p=0.5
key 2.375 Space
# Chords (page 3: columns are the key's chords I to VII, the middle row plain
# triads): Am, F, C, G, two beats each over bars 1 and 2, then Space just
# after the bar line: those two bars loop.
page 3.0 3
tap 4.250 0.0714 0.40 len=0.85 p=0.6
tap 5.188 0.7857 0.40 len=0.85 p=0.6
tap 6.125 0.3571 0.40 len=0.85 p=0.6
tap 7.062 0.9286 0.40 len=0.85 p=0.6
key 8.094 Space
# Bass (page 2, a scale-column starter): the roots under them, bars 4 and 5.
page 8.281 2
tap 9.875 0.033 0.5 len=0.2
tap 10.812 0.367 0.5 len=0.2
tap 11.750 0.167 0.5 len=0.2
tap 12.688 0.433 0.5 len=0.2
key 13.719 Space
# FX (page 7). Drop style GAP: everything but the risers holds its breath for
# the last half beat.
page 13.906 7
key 13.953 Shift+Right
# Two pitch risers in the 8-bar row aiming at the phrase line at bar 16: one
# lands on the tonic, the other slides two scale steps up, to the third.
down 17.398 1 0.30 0.76 p=0.5
down 17.422 2 0.30 0.76 p=0.5
glide 17.562 18.078 2 0.395 0.76
# From the keyboard: D (saw) and F (roll), 4-bar risers joining the same
# drop, due to come in at bar 12.
key 19.250 D
key 20.188 F
# Three bars in, the click: the drop comes on the next bar line instead.
# The pitch risers rush the rest of their climb into one bar; the saw and
# the roll, still waiting, come in at once and build in that bar too.
glide 22.062 24.406 1 0.30 0.88 p=0.8
glide 22.062 24.406 2 0.395 0.88 p=0.8
click 23.141
up 24.641 1
up 24.641 2
# After the drop, from the keyboard: an echo throw on the last beat of the
# next bar (N), then a bar of reverb wash (M).
key 28.156 N down
key 28.625 N up
key 28.625 M down
key 30.500 M up
end 34.250
