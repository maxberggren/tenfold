# Loop and scream: tap a pluck riff, land on its first note again on the beat and
# hold to loop it (the loop sets the tempo), then switch to Scream and play over it:
# a trill with two fingers, slides, a bend, harmony sliding from a third to a fifth,
# the octave click, and a throw at the end.
# Render: build/tenfold render docs/parts/lead/loop-and-scream.tf loop.wav
page 0 4
tempo 0.01 124
key 0.2581 Right
# The riff, tapped once in time...
down 0.5 1 0.6100 0.550 p=0.55
up 0.5852 1
down 0.7419 2 0.8180 0.550 p=0.55
up 0.8271 2
down 0.9839 3 0.7140 0.550 p=0.55
up 1.0690 3
down 1.2258 4 0.9740 0.550 p=0.55
up 1.3110 4
down 1.4677 5 0.6100 0.550 p=0.55
up 1.5529 5
down 1.7097 6 0.8180 0.550 p=0.55
up 1.7948 6
down 1.9516 7 0.8700 0.550 p=0.55
up 2.0368 7
down 2.1935 8 0.8180 0.550 p=0.55
up 2.2787 8
# ...and its first note again, held: it loops.
down 2.4355 90 0.6100 0.55 p=0.55
up 2.871 90
key 2.9194 Right
key 2.9677 Right
key 3.0161 Right
key 3.0645 Right
# Scream over the loop.
down 4.371 9 0.6100 0.600 p=0.55
glide 4.4194 5.2903 9 0.6100 0.600 p=0.85
glide 4.6613 4.8548 9 0.7140 0.600
up 5.2787 9
down 5.3387 10 0.8180 0.600 p=0.55
down 5.4597 11 0.8700 0.600 p=0.55
up 5.4997 10
down 5.5806 12 0.8180 0.600 p=0.55
up 5.6206 11
down 5.7016 13 0.8700 0.600 p=0.55
up 5.7416 12
down 5.8226 14 0.8180 0.600 p=0.55
up 5.8626000000000005 13
down 5.9435 15 0.8700 0.600 p=0.55
up 5.9835 14
down 6.0645 16 0.8180 0.600 p=0.55
up 6.1045 15
up 6.2465 16
down 6.3065 17 0.9740 0.700 p=0.55
down 6.3065 225 0.140 0.580 p=0.6
glide 6.5484 6.6355 17 0.9810 0.700
glide 6.6355 6.7226 17 0.9670 0.700
glide 6.7226 6.8097 17 0.9810 0.700
glide 6.8097 6.8968 17 0.9670 0.700
glide 6.8968 6.9839 17 0.9810 0.700
glide 6.9839 7.071 17 0.9670 0.700
glide 7.071 7.1581 17 0.9810 0.700
glide 7.1581 7.2452 17 0.9670 0.700
up 7.2142 17
glide 7.2452 7.2935 17 0.9740 0.700
down 7.2742 18 0.8700 0.600 p=0.55
click 7.2742 len=0.919
down 7.7581 19 0.8180 0.600 p=0.55
up 7.7981 18
up 8.1819 19
down 8.2419 20 0.7140 0.600 p=0.55
glide 8.2419 8.5323 225 0.06 0.700
glide 8.2903 8.9194 20 0.7140 0.600 p=0.90
down 8.9677 21 0.8180 0.600 p=0.55
up 9.0077 20
down 9.2097 22 0.8700 0.800 p=0.55
up 9.249699999999999 21
down 9.3548 324 0.055 0.15
glide 9.3748 9.5968 324 0.055 0.300
up 10.0806 324
up 10.1174 22
down 10.1774 23 0.9740 0.800 p=0.55
glide 11.1452 11.7258 23 0.6100 0.500
down 11.629 226 0.165 0.380 p=0.8
up 12.0529 23
up 12.0645 225
up 12.1129 226
key 13.3226 Backspace
end 15.5
