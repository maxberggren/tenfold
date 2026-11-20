# Chords demo: house at 124 BPM in A minor.
# A ii-V-style progression of sevenths typed on the keyboard (Shift+F, J, D, A:
# Dm7, G7, Cmaj7, Am7) on the house organ, whose offbeat stabs come from
# holding the keys; Return loops it. Then the house piano tapped live in the
# seventh row over the loop, a rail fill that gates everything, the loop
# included, for two beats, and hold mode (a click) for the last chords.
tempo 0 124
page 0.01 3
key 0.020 Right
key 0.030 Right
# bars 1-4: organ sevenths from the keyboard
key 0.050 Shift+F down
key 1.895 Shift+F up
key 1.945 Shift+J down
key 3.831 Shift+J up
key 3.881 Shift+D down
key 5.766 Shift+D up
key 5.816 Shift+A down
key 7.702 Shift+A up
key 6.871 Return
# bars 5-8: the piano over the loop, tapped: stabs on 1, the and of 2, and 4
key 7.790 Right
tap 7.742 0.5000 0.720 len=0.14 p=0.75
tap 8.468 0.5000 0.720 len=0.14 p=0.55
tap 9.194 0.5000 0.720 len=0.14 p=0.55
tap 9.435 0.5000 0.720 len=0.14 p=0.55
tap 9.677 0.9286 0.720 len=0.14 p=0.75
tap 10.403 0.9286 0.720 len=0.14 p=0.55
tap 11.129 0.9286 0.720 len=0.14 p=0.55
tap 11.371 0.9286 0.720 len=0.14 p=0.55
tap 11.613 0.3571 0.720 len=0.14 p=0.75
tap 12.339 0.3571 0.720 len=0.14 p=0.55
tap 13.065 0.3571 0.720 len=0.14 p=0.55
tap 13.306 0.3571 0.720 len=0.14 p=0.55
tap 13.548 0.0714 0.720 len=0.14 p=0.75
tap 14.274 0.0714 0.720 len=0.14 p=0.55
tap 15.000 0.0714 0.720 len=0.14 p=0.55
tap 15.242 0.0714 0.720 len=0.14 p=0.55
# bar 8, beats 3-4: hold the 16ths cell on the rail: everything stutters, the loop too
down 14.516 1 0.8125 0.82
up 15.464 1
# bars 9-12: hold mode: click once, then each chord rings until the next
down 15.494 2 0.5000 0.560 p=0.7
click 15.584
up 15.726 2
tap 17.419 0.9286 0.560 len=0.1 p=0.8
tap 19.355 0.3571 0.560 len=0.1 p=0.8
tap 21.290 0.7857 0.560 len=0.1 p=0.8
click 23.226
key 23.274 Backspace
end 27.097
