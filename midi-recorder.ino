/*********************************************************

   This is the code for a prototype MIDI field recorder
   that sits between a MIDI-OUT device (like a controller)
   and a MIDI-IN device (like a computer) for saving all
   note, pitch, and CC data that's being played without
   having any device specifically set to "record".

   This is Public Domain code, with all the caveats that
   comes with for you, because public domain is not the
   same as open source.

   See https://opensource.org/node/878 for more details.

   The reason this code is in the public domain is
   because anyone with half a brain and a need to
   create this functionality will reasonably end up
   with code that's so similar as to effectively be
   the same as what has been written here.

   Having said that, there are countries that do not
   recognize the Public Domain. In those countries,
   this code is to be considered as being provided
   under an MIT license. See the end of this file
   for its full license text.
 ********************************************************/

#include <SD.h>
#include <MIDI.h>

#define AUDIO_DEBUG_PIN 2
#define AUDIO 8
#define CHIP_SELECT 9
#define HAS_MORE_BYTES 0x80

#define NOTE_OFF_EVENT 0x81
#define NOTE_ON_EVENT 0x91
#define CONTROL_CHANGE_EVENT 0xB1
#define PITCH_BEND_EVENT 0xE1

#define RECORDING_TIMEOUT 120000000
// 2 minutes, counted in microseconds

int lastPlayState = 0;
bool play;

String filename;
File file;

unsigned long startTime = 0;
unsigned long lastTime = 0;

unsigned long lastLoopCounter = 0;
unsigned long loopCounter = 0;

MIDI_CREATE_DEFAULT_INSTANCE();

/**
   Set up our MIDI field recorder
*/
void setup() {
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.setHandlePitchBend(handlePitchBend);
  MIDI.setHandleControlChange(handleControlChange);

  // set up the tone playing button
  pinMode(AUDIO_DEBUG_PIN, INPUT);
  play = false;

  // set up SD card functionality
  pinMode(CHIP_SELECT, OUTPUT);

  if (SD.begin(CHIP_SELECT)) {
    findNextFilename();
    if (file) {
      createMidiFile();
    }
  }
}

/**
    We could use the EEPROM to store this number,
    but since we're not going to get timestamped
    files anyway, just looping is also fine.
*/
void findNextFilename() {
  for (int i = 1; i < 1000; i++) {
    filename = "file-";
    if (i < 10) filename += "0";
    if (i < 100) filename += "0";
    filename += String(i);
    filename += String(".mid");

    if (!SD.exists(filename)) {
      file = SD.open(filename, FILE_WRITE);
      return;
    }
  }
}

/**
   Set up a new MIDI file with some boilter plate byte code
*/
void createMidiFile() {
  byte header[] = {
    0x4D, 0x54, 0x68, 0x64,   // "MThd" chunk
    0x00, 0x00, 0x00, 0x06,   // chunk length (from this point on)
    0x00, 0x00,               // format 0
    0x00, 0x01,               // one track
    0x01, 0xC2                // data rate = 450 ticks per quarter note
  };
  file.write(header, 14);

  byte track[] = {
    0x4D, 0x54, 0x72, 0x6B,   // "MTrk" chunk
    0x00, 0x00, 0x00, 0x00    // chunk length placeholder (MSB)
  };
  file.write(track, 8);

  byte tempo[] = {
    0x00,                     // time delta (of zero)
    0xFF, 0x51, 0x03,         // tempo op code
    0x06, 0xDD, 0xD0          // real rate = 450,000μs per quarter note (= 132 BPM)
  };
  file.write(tempo, 7);
}

/**
   The program loop consists of checking whether we need to perform
   any file management, and then checking for MIDI input.
*/
void loop() {
  updateFile();
  setPlayState();
  MIDI.read();
}


// ======================================================================================


/**
   We close-and-reopen our file every 400ms, allowing
   us to treat the file as an always-open byte stream,
   while ensuring the file stays up to date on the SD
   card rather than being a 0 byte file because it
   never gets closed, or having to open and close it
   for every single write operation (which would be
   rather terrible, performance wise).
*/
void updateFile() {
  loopCounter = millis();
  if (loopCounter - lastLoopCounter > 400) {
    checkReset();
    lastLoopCounter = loopCounter;
    file.flush();
  }
}

/**
   This will crash the arduino when called as a function,
   so the watchdog will catch that and restart, instead =)
*/
void(* resetArduino) (void) = 0;

/**
  if we've not received any data for 2 minutes,
  delete the current file if it's empty, and reset
  the arduino, so that we're on a new file.
*/
void checkReset() {
  if (!file) return;
  if (micros() - lastTime > RECORDING_TIMEOUT) {
    if (startTime == 0) {
      file.close();
      SD.remove(filename);
    }
    resetArduino();
  }
}

/**
   This mostly exists for debuggin purposes: pressing
   the button tied to the audio debug pin will cause
   the program to play notes for every MIDI note-on
   event that comes flying by.
*/
void setPlayState() {
  int playState = digitalRead(AUDIO_DEBUG_PIN);
  if (playState != lastPlayState) {
    lastPlayState = playState;
    if (playState == 1) play = !play;
  }
}


// ======================================================================================


void handleNoteOff(byte channel, byte pitch, byte velocity) {
  writeToFile(NOTE_OFF_EVENT, pitch, velocity, getDelta());
}

void handleNoteOn(byte channel, byte pitch, byte velocity) {
  writeToFile(NOTE_ON_EVENT, pitch, velocity, getDelta());
  if (play) tone(AUDIO, 440 * pow(2, (pitch - 69.0) / 12.0), 100);
}

void handleControlChange(byte channel, byte cc, byte value) {
  writeToFile(CONTROL_CHANGE_EVENT, cc, value, getDelta());
}

void handlePitchBend(byte channel, int bend) {
  byte lsb = (byte) (bend & 0x7F);
  byte msb = (byte) ((bend >> 7) & 0x7F);
  writeToFile(PITCH_BEND_EVENT, lsb, msb, getDelta());
}

/**
   This calculates the number of ticks since the last MIDI event
*/
int getDelta() {
  if (startTime == 0) {
    startTime = millis();
    lastTime = startTime;
    return 0;
  }
  unsigned long now = millis();
  unsigned int delta = (now - lastTime);
  lastTime = now;
  return delta;
}

/**
   Write MIDI events to file, where all MIDI events are
   recording uses the format:

   <tick delta> <event code> <byte> <byte>

   See the "Standard MIDI-File Format" for more information.
*/
void writeToFile(byte eventType, byte b1, byte b2, int delta) {
  if (!file) return;
  writeVarLen(file, delta);
  file.write(eventType);
  file.write(b1);
  file.write(b2);
}

/**
   Ported from the MIDI spec:
*/
void writeVarLen(File file, unsigned long value) {
  unsigned long buffer = value & 0x7f;

  // esnure MSBF ordering
  while ((value >>= 7) > 0) {
    buffer <<= 8;
    buffer |= HAS_MORE_BYTES;
    buffer |= value & 0x7f;
  }

  // write out values (LIFO)
  while (true) {
    file.write((byte)(buffer & 0xff));
    if (buffer & HAS_MORE_BYTES) {
      buffer >>= 8;
    } else {
      break;
    }
  }
}

/*********************************************************
   If you live in a country that does not recognise the
   Public Domain, the following (MIT) license applies:

     Copyright 2020, Pomax
     Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
     The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
     THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 ********************************************************/
 
