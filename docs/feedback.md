# Feedback on the Vocal page

The Vocal page plays the microphone straight back (the monitor, mixed after
the limiter so nothing is added to its latency). In headphones that is the
point; on speakers the microphone hears the voice again, and anything the
trip gains on grows into a howl. tenfold guards against it twice.

## Before: what the sound plays through

The audio backend says whether the output is speakers or headphones, best
effort, and the studio publishes it (`StudioView::output`):

- **Linux (PipeWire).** tenfold follows the graph (`src/linux/pipewire.cpp`,
  classified by `src/outputkind.h`): the sink its stream is linked to (the
  default sink before that), and for a card's sink the device's *active
  route* for that sink: `port.type` `speaker` or `headphones`/`headset`.
  ALSA's jack detection switches that route when a plug goes in or out, and
  tenfold hears the change. A filter chain or loopback (Asahi's speaker
  DSP, an equaliser) is followed to where it plays; Bluetooth goes by the
  device's form factor; failing all that, a sink named "speaker" or
  "headphone". `tenfold output` prints what it finds.
- **macOS (CoreAudio).** The default output's transport type and, for the
  built-in output, its data source: internal speakers (`ispk`) or
  headphones (`hdpn`); on Apple silicon the jack is a device of its own
  named "External Headphones". Bluetooth goes by its name (AirPods,
  "Headphones", "Speaker"). Both are watched for changes.

Anything else (HDMI, a line out, a USB interface) is *unknown* and nothing
is muted for it: the detector below still is there.

On speakers the page keeps the monitor muted and says so ("SPEAKERS:
MONITOR MUTED"), until headphones are plugged in or the player presses
Shift+M (or clicks the warning) to hear it on the speakers anyway. Singing
still goes onto the tape, so Space still makes loops, and loops play out of
the speakers as usual: a loop is played back a bar later, not fed straight
back, so it can't run away (a layer sung over it picks up some of it, which
is all).

## During: hearing a howl start

The engine listens for the howl itself (`src/howl.h`, in the vocal engine's
monitor path, real-time safe, about 0.5 % of a core). Every 512 samples one
1024-point FFT gives the microphone's spectrum and the monitor's. A suspect
is a line (a cluster within 6 % of one frequency) that for six frames
(64 ms) either grows as a howl does, by the same factor every trip, a
straight line in dB that doesn't level off, or holds nearly all the band's
energy without sliding; and that the monitor's own output carries too.

Singing can look like that (a pure "oo", a straight note), so a suspect is
tested by taking the loop apart: the monitor dips 30 dB for a moment. A
howl, with nothing left to feed it, falls 10 dB; given the monitor back, it
returns. Both, and the monitor is muted, faded out in 6 ms, and stays muted:
"FEEDBACK: MONITOR MUTED" on the page, the monitor chip red, and the notice
"Feedback: tenfold muted your monitor — use headphones". Plugging in headphones
brings the monitor back, or Shift+M. A voice doesn't fall when the monitor
dips, so a wrong suspect costs one short dip, and that note is left alone
while it lasts.

In the tests a simulated room (the script's `room` event: the mix back into
the microphone through a resonance, a trip later) howls at 1.1, 2.5 and
4 kHz; the monitor is dipped within 15–40 ms of the howl reaching −30 dBFS
and muted within 70–120 ms. Loud singing (phrases, held and straight notes,
slides, RIOT and KARGYRAA, and a scream and a growl played into the
microphone) is never muted.
