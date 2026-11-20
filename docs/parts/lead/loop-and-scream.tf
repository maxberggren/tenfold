# Loop and scream: tap a pluck riff, land on its first note again on the beat and
# hold to loop it (the loop sets the tempo), then switch to Scream and play over it:
# a trill with two fingers, slides, a bend, harmony sliding from a third to a fifth,
# the octave click, and a throw at the end.
# Render: build/tenfold render docs/parts/lead/loop-and-scream.tf loop.wav
page 0 4
tempo 0.01 124
key 0.7581 Right
# The riff, tapped once in time...
down 1.0 1 0.6100 0.550 p=0.55
up 1.0852 1
down 1.2419 2 0.8180 0.550 p=0.55
up 1.3271 2
down 1.4839 3 0.7140 0.550 p=0.55
up 1.5690 3
down 1.7258 4 0.9740 0.550 p=0.55
up 1.8110 4
down 1.9677 5 0.6100 0.550 p=0.55
up 2.0529 5
down 2.2097 6 0.8180 0.550 p=0.55
up 2.2948 6
down 2.4516 7 0.8700 0.550 p=0.55
up 2.5368 7
down 2.6935 8 0.8180 0.550 p=0.55
up 2.7787 8
# ...and its first note again, held: it loops.
down 2.9355 90 0.6100 0.55 p=0.55
up 3.371 90
key 3.4194 Right
key 3.4677 Right
key 3.5161 Right
key 3.5645 Right
# Scream over the loop.
down 4.871 9 0.6100 0.600 p=0.55
glide 4.9194 5.7903 9 0.7140 0.600 p=0.85
glide 5.1613 5.3548 9 0.7140 0.600
up 5.7787 9
down 5.8387 10 0.8180 0.600 p=0.55
down 5.9597 11 0.8700 0.600 p=0.55
up 5.9997 10
down 6.0806 12 0.8180 0.600 p=0.55
up 6.1206 11
down 6.2016 13 0.8700 0.600 p=0.55
up 6.2416 12
down 6.3226 14 0.8180 0.600 p=0.55
up 6.3626000000000005 13
down 6.4435 15 0.8700 0.600 p=0.55
up 6.4835 14
down 6.5645 16 0.8180 0.600 p=0.55
up 6.6045 15
up 6.7465 16
down 6.8065 17 0.9740 0.700 p=0.55
down 6.8065 225 0.060 0.580 p=0.6
glide 7.0484 7.1355 17 0.9810 0.700
glide 7.1355 7.2226 17 0.9670 0.700
glide 7.2226 7.3097 17 0.9810 0.700
glide 7.3097 7.3968 17 0.9670 0.700
glide 7.3968 7.4839 17 0.9810 0.700
glide 7.4839 7.571 17 0.9670 0.700
glide 7.571 7.6581 17 0.9810 0.700
glide 7.6581 7.7452 17 0.9670 0.700
up 7.7142 17
glide 7.7452 7.7935 17 0.9740 0.700
down 7.7742 18 0.8700 0.600 p=0.55
click 7.7742 len=0.919
down 8.2581 19 0.8180 0.600 p=0.55
up 8.2981 18
up 8.6819 19
down 8.7419 20 0.7140 0.600 p=0.55
glide 8.7419 9.0323 225 0.06 0.700
glide 8.7903 9.4194 20 0.7140 0.600 p=0.90
down 9.4677 21 0.8180 0.600 p=0.55
up 9.5077 20
down 9.7097 22 0.8700 0.800 p=0.55
up 9.749699999999999 21
down 9.8548 324 0.055 0.15
glide 9.8748 10.0968 324 0.055 0.300
up 10.5806 324
up 10.6174 22
down 10.6774 23 0.9740 0.800 p=0.55
glide 11.6452 12.2258 23 0.6100 0.500
down 12.129 226 0.165 0.380 p=0.8
up 12.5529 23
up 12.5645 225
up 12.6129 226
key 13.8226 Backspace
end 16.0
