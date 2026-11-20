# Loop and scream: play a pluck riff through one bar and press Space on the next
# ONE: that bar loops and keeps its tempo. Then switch to Scream and play over it:
# a trill with two fingers, slides, a bend, harmony sliding from a third to a fifth,
# the octave click, and a throw at the end.
# Render: build/tenfold render docs/parts/lead/loop-and-scream.tf loop.wav
page 0 4
tempo 0.01 124
key 1.6935 Right
# The riff, played once through a bar...
down 1.9355 1 0.6100 0.550 p=0.55
up 2.0206 1
down 2.1774 2 0.8180 0.550 p=0.55
up 2.2626 2
down 2.4194 3 0.7140 0.550 p=0.55
up 2.5045 3
down 2.6613 4 0.9740 0.550 p=0.55
up 2.7465 4
down 2.9032 5 0.6100 0.550 p=0.55
up 2.9884 5
down 3.1452 6 0.8180 0.550 p=0.55
up 3.2303 6
down 3.3871 7 0.8700 0.550 p=0.55
up 3.4723 7
down 3.629 8 0.8180 0.550 p=0.55
up 3.7142 8
# ...and Space on the next ONE: the bar loops.
key 3.871 Space
key 4.3548 Right
key 4.4032 Right
key 4.4516 Right
key 4.5 Right
# Scream over the loop.
down 5.8065 9 0.6100 0.600 p=0.55
glide 5.8548 6.7258 9 0.7140 0.600 p=0.85
glide 6.0968 6.2903 9 0.7140 0.600
up 6.7142 9
down 6.7742 10 0.8180 0.600 p=0.55
down 6.8952 11 0.8700 0.600 p=0.55
up 6.9352 10
down 7.0161 12 0.8180 0.600 p=0.55
up 7.0561 11
down 7.1371 13 0.8700 0.600 p=0.55
up 7.1771 12
down 7.2581 14 0.8180 0.600 p=0.55
up 7.2981 13
down 7.379 15 0.8700 0.600 p=0.55
up 7.419 14
down 7.5 16 0.8180 0.600 p=0.55
up 7.54 15
up 7.6819 16
down 7.7419 17 0.9740 0.700 p=0.55
down 7.7419 225 0.060 0.580 p=0.6
glide 7.9839 8.071 17 0.9810 0.700
glide 8.071 8.1581 17 0.9670 0.700
glide 8.1581 8.2452 17 0.9810 0.700
glide 8.2452 8.3323 17 0.9670 0.700
glide 8.3323 8.4194 17 0.9810 0.700
glide 8.4194 8.5065 17 0.9670 0.700
glide 8.5065 8.5935 17 0.9810 0.700
glide 8.5935 8.6806 17 0.9670 0.700
up 8.6497 17
glide 8.6806 8.729 17 0.9740 0.700
down 8.7097 18 0.8700 0.600 p=0.55
click 8.7097 len=0.919
down 9.1935 19 0.8180 0.600 p=0.55
up 9.2335 18
up 9.6174 19
down 9.6774 20 0.7140 0.600 p=0.55
glide 9.6774 9.9677 225 0.06 0.700
glide 9.7258 10.3548 20 0.7140 0.600 p=0.90
down 10.4032 21 0.8180 0.600 p=0.55
up 10.4432 20
down 10.6452 22 0.8700 0.800 p=0.55
up 10.6852 21
down 10.7903 324 0.055 0.15
glide 10.8103 11.0323 324 0.055 0.300
up 11.5161 324
up 11.5529 22
down 11.6129 23 0.9740 0.800 p=0.55
glide 12.5806 13.1613 23 0.6100 0.500
down 13.0645 226 0.165 0.380 p=0.8
up 13.4884 23
up 13.5 225
up 13.5484 226
key 14.7581 Backspace
end 16.9355
