# Creating a MIDI pass-through recorder

If you've ever used audio softare on the computer, you probably know that MIDI exists: a signalling protocol that allows controllers to control vitual instruments like synths. It's also the protocol used by real audio hardware to talk to each, and you can think of it as the language in which devices talk about what they're doing, rather than what audio they're generating.

As such, there are two ways to record instruments (real or virtual): record the sound they're making, or record the MIDI event that cause that sound to be made, and that's where things get interesting. 

There are many, _many_ ways to record audio, from microphones to line monitors to audio interfaces, but not all that many ways to record MIDI events. Essentially: unless you're running software that monitors MIDI events, there isn't really any way to record MIDI. So I set out to change that: in the same way that you can just hook up an audio field recorder (like a Tuscan DR-05) to sit between an audio-out on something that generates audio and an audio-in on something that should be listening to that audio, writing that to an SD card as `.wav` or `.mp3` or the like,  I built a MIDI "field recorder" that you plug in between your MIDI-out and some MIDI-in, indiscriminately recording every MIDI event that gets sent over the wire to an SD card as a `.mid` file.

You'd think this would be something that already exists as a product you can just buy. Amazingly, it is not. So if you want one too, you'll have to build one, and if you want to build one, this post might be useful to you!

## The circuitry

To build this, we're going to basically build a standard Arduino based MIDI pass-through, with an SD card circuit hooked up so we can save the data that comes flying by. To build everything, we'll need:

1. An Arduino UNO R3 or equivalent board (anywhere between $10 and $25)
2. An Arduino SD card module ($7 for a pack of five)
3. Two female 5-pin DIN connectors ($5 for a pack of ten)
4. A 6N138 optocoupler ($10 for a pack of ten)
5. 3x 220 ohm resistors
6. 1x 4.7k ohm resistor
7. A diode
8. A (4 pin) clicky pushy button

### The MIDI part of our recorder

We set up MIDI-In on the Arduino `RX<-0` pin, with MIDI-Thru via the `TX->1` pin. The only tricky bit about this is that MIDI signals are isolated from the rest of the circuitry via an optocoupler (which gets around ground loop problems by literally transmitting signals by running them through a LED, which emits the signal as light, which then gets picked up by another LED and turned back into signal).

<img alt="MIDI circuit diagram" src="./MIDI.png" width="75%">

### The SD part of our recorder

The SD card circuitry is literally just a matter of "connect the pins to the pins", with the only oddity being that the pins don't _quite_ line up well enough to literally just stick the SD card module directly into the Arduino.

<img alt="SD module diagram" src="./sd card.png" width="50%">

### Adding a beep, for debugging

Also, we're going to add a little piezo speaker and a button that we can press to turn on (or off) playing a note corresponding to a MIDI note getting played, mostly as the audio equivalent of visual debugging. There's barely any work here: we hook up the "speaker" between pin 8 and ground, and the button to pin 2. Beep, beep!

<img alt="beep beep button diagram" src="./button.png" width="50%">



## The Software

In order to program the Arduino to do what we need it to do, we'll need `midi.h` and `SD.h` imported. To make everything happen our program consists of four parts:

1. basic signal handling (MIDI library)
2. basic file writing (SD library)
3. Audio debugging (beep beep)
4. restart on "no activity"



### MIDI handling

...

### File management

...

### Making some beeps

...

### Autorestart

...

