# Five new sets: the same build, drop and hits in each of the FX page's
# sets added after the first ten, four bars each, over a groove. Left from
# EDM wraps round to the last set; five of them reach amapiano, and Right
# steps on from there. The risers land on bar 1 of each four, Return rushes
# them into a drop two bars on, a sub tapped during the build lands on it,
# then each set's own: amapiano's log drum line, gamelan zaps over the
# gong, chiptune blips, phonk's cowbell held for its melody, shoegaze's
# fuzz into a wash. 128 BPM, A minor.
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
# FX (page 7).
page 13.906 7
# AMAPIANO: a breathy flute, deep-house keys off the beat, swung shakers and woodblock; the log drum on the drop, then a log drum line.
key 14.620 Left
key 14.670 Left
key 14.720 Left
key 14.770 Left
key 14.820 Left
tap 15.234 0.10 0.44 p=0.6 len=0.2
tap 15.352 0.30 0.44 p=0.6 len=0.2
tap 15.469 0.50 0.44 p=0.6 len=0.2
tap 15.586 0.70 0.44 p=0.6 len=0.2
key 17.016 Return
tap 17.812 0.188 0.12
tap 18.515 0.188 0.13
tap 18.984 0.188 0.11
tap 19.218 0.188 0.12
tap 19.570 0.188 0.14
tap 20.390 0.188 0.13
tap 20.742 0.188 0.11
tap 21.093 0.188 0.12
tap 21.420 0.438 0.12
# GAMELAN: a suling, saron bars beating in pairs, a gangsa run up the key; a gong ageng on the drop, then bell zaps.
key 22.520 Right
tap 22.734 0.10 0.44 p=0.6 len=0.2
tap 22.852 0.30 0.44 p=0.6 len=0.2
tap 22.969 0.50 0.44 p=0.6 len=0.2
tap 23.086 0.70 0.44 p=0.6 len=0.2
key 24.516 Return
tap 25.312 0.188 0.12
tap 27.422 0.438 0.14
tap 27.891 0.420 0.12
tap 28.125 0.450 0.15
# CHIPTUNE: the noise channel's sweep turning to a buzz, an arpeggio, pulse stabs, a noise-drum roll; square blips; a reset.
key 30.020 Right
tap 30.234 0.10 0.44 p=0.6 len=0.2
tap 30.352 0.30 0.44 p=0.6 len=0.2
tap 30.469 0.50 0.44 p=0.6 len=0.2
tap 30.586 0.70 0.44 p=0.6 len=0.2
key 32.016 Return
tap 32.812 0.188 0.12
tap 34.922 0.438 0.14
tap 35.157 0.420 0.12
tap 35.391 0.450 0.15
tap 36.328 0.562 0.12
# PHONK: worn saws, triplet hats, a distorted 808 on the drop; the cowbell held for its melody.
key 37.520 Right
tap 37.734 0.10 0.44 p=0.6 len=0.2
tap 37.852 0.30 0.44 p=0.6 len=0.2
tap 37.969 0.50 0.44 p=0.6 len=0.2
tap 38.086 0.70 0.44 p=0.6 len=0.2
key 39.516 Return
tap 40.312 0.188 0.12
down 41.250 7 0.438 0.14
up 44.250 7
# SHOEGAZE: amp feedback, a fuzz guitar on the whammy bar, fuzz chords scooping up, a reverse reverb; a wall on the drop, then a wash.
key 45.020 Right
tap 45.234 0.10 0.44 p=0.6 len=0.2
tap 45.352 0.30 0.44 p=0.6 len=0.2
tap 45.469 0.50 0.44 p=0.6 len=0.2
tap 45.586 0.70 0.44 p=0.6 len=0.2
key 47.016 Return
tap 47.812 0.188 0.12
tap 49.922 0.438 0.14
tap 50.391 0.812 0.14 len=0.3
end 55.020
