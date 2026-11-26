# Five new sets: the same build and drop in each of the FX page's sets
# added after the first ten, four bars each, over a groove, then what each
# brings of its own. Left from EDM wraps round to the last set; five of
# them reach amapiano, and Right steps on from there. The risers land on
# bar 1 of each four and Return rushes them into a drop on the next bar
# line. Then: amapiano's log drum alone on the ONE (the drums and bass wait
# a beat), a log drum line tapped across its cell, with a slide, and an
# "ayy" held into chops; gamelan's gong and a bonang held; chiptune's
# triangle bass line and blips; phonk's cowbell figures, its Memphis vocal
# off a slowing tape and a sliding 808; shoegaze's wall, its feedback
# swelling back, a dive-bomb and harmonics. 128 BPM, A minor.
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
# AMAPIANO: the log drum rolled into the drop, a whistle calling and answering, deep-house keys, swung shakers and woodblock, a cabasa; the log drum alone on the drop, then a log drum line with a slide, and an ayy held.
key 14.620 Left
key 14.670 Left
key 14.720 Left
key 14.770 Left
key 14.820 Left
tap 15.234 0.10 0.44 p=0.6 len=0.2
tap 15.352 0.30 0.44 p=0.6 len=0.2
tap 15.469 0.50 0.44 p=0.6 len=0.2
tap 15.586 0.70 0.44 p=0.6 len=0.2
tap 15.704 0.90 0.44 p=0.6 len=0.2
key 17.016 Shift+Return
tap 17.727 0.1875 0.12 len=0.06
tap 18.078 0.1875 0.12 len=0.06
tap 18.547 0.2272 0.12 len=0.06
tap 18.781 0.1478 0.12 len=0.06
tap 19.016 0.1875 0.12 len=0.06
tap 19.250 0.1875 0.12 len=0.06
tap 19.602 0.2470 0.12 len=0.06
tap 20.656 0.1677 0.12 len=0.06
tap 20.891 0.1875 0.12 len=0.06
down 19.953 41 0.1875 0.12 p=0.6
glide 20.070 20.246 41 0.2470 0.12
up 20.422 41
down 20.656 48 0.4375 0.12 p=0.6
up 21.594 48
# GAMELAN, in slendro: ceng-ceng rattling in the bars' tones, a suling's ornaments, saron bars beating in pairs, a gangsa's norot then telu; a gong ageng on the drop, then a bonang held.
key 22.520 Right
tap 22.734 0.10 0.44 p=0.6 len=0.2
tap 22.852 0.30 0.44 p=0.6 len=0.2
tap 22.969 0.50 0.44 p=0.6 len=0.2
tap 23.086 0.70 0.44 p=0.6 len=0.2
tap 23.204 0.90 0.44 p=0.6 len=0.2
key 24.516 Shift+Return
down 26.750 42 0.4375 0.14 p=0.6
up 28.625 42
# CHIPTUNE: the noise channel's sweep, an arpeggio stepping by frames, a tracker's pattern (volume column, duty sweep, vibrato, a slide up) with its echo, the noise channel's stepped swell; the triangle's kick on the drop and a triangle bass line, blips, a stepped fall.
key 30.020 Right
tap 30.234 0.10 0.44 p=0.6 len=0.2
tap 30.352 0.30 0.44 p=0.6 len=0.2
tap 30.469 0.50 0.44 p=0.6 len=0.2
tap 30.586 0.70 0.44 p=0.6 len=0.2
tap 30.704 0.90 0.44 p=0.6 len=0.2
key 32.016 Shift+Return
tap 32.375 0.1875 0.10 len=0.06
tap 32.609 0.1875 0.10 len=0.06
tap 32.844 0.2272 0.10 len=0.06
tap 33.078 0.1875 0.10 len=0.06
tap 33.312 0.2470 0.10 len=0.06
tap 33.547 0.2272 0.10 len=0.06
tap 33.781 0.1875 0.10 len=0.06
tap 34.016 0.1478 0.10 len=0.06
tap 34.250 0.1875 0.10 len=0.06
tap 34.484 0.1875 0.10 len=0.06
tap 34.719 0.2272 0.10 len=0.06
tap 34.953 0.2470 0.10 len=0.06
down 35.188 43 0.4375 0.12 p=0.6
up 35.656 43
tap 35.773 0.3125 0.12 len=0.1
# PHONK: a worn tape's hiss with the horror chord printed through it and rewound with a squeal into the drop, a horror chord on a tape spinning up, the cowbell's 3-3-2 rolled, triplet hats, a horror chord on the drop; the cowbell's busy figure, then its falling one twisted; the Memphis vocal off a slowing tape, tapped and held; an 808 line sliding.
key 37.520 Right
tap 37.734 0.10 0.44 p=0.6 len=0.2
tap 37.852 0.30 0.44 p=0.6 len=0.2
tap 37.969 0.50 0.44 p=0.6 len=0.2
tap 38.086 0.70 0.44 p=0.6 len=0.2
tap 38.204 0.90 0.44 p=0.6 len=0.2
key 39.516 Shift+Return
down 39.975 44 0.4375 0.19 p=0.6
up 41.750 44
down 41.800 45 0.4375 0.07 p=0.6
move 42.219 45 0.4375 0.07 twist=30
up 43.625 45
tap 40.812 0.3125 0.12 len=0.1
down 42.687 46 0.3125 0.12 p=0.6
up 43.156 46
tap 39.875 0.1875 0.14 len=0.06
tap 40.578 0.2470 0.14 len=0.06
tap 41.047 0.1478 0.14 len=0.06
tap 41.750 0.1875 0.14 len=0.06
tap 42.453 0.2669 0.14 len=0.06
# SHOEGAZE: amp feedback singing, a glide guitar, fuzz chords strummed into a wall and tremolo-picked, a reversed reverb; a palm-muted chug on the drop and the wall blooming out of it, its feedback swelling back within the beat, a dive-bomb, harmonics held.
key 45.020 Right
tap 45.234 0.10 0.44 p=0.6 len=0.2
tap 45.352 0.30 0.44 p=0.6 len=0.2
tap 45.469 0.50 0.44 p=0.6 len=0.2
tap 45.586 0.70 0.44 p=0.6 len=0.2
tap 45.704 0.90 0.44 p=0.6 len=0.2
key 47.016 Shift+Return
tap 49.250 0.3125 0.15 len=0.4
down 49.953 47 0.4375 0.14 p=0.6
up 50.891 47
end 55.020
