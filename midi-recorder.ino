/*********************************************************

   This is the code for a prototype inline MIDI recorder
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
int lastPlayState = 0;
bool play = false;

#define PLACE_MARKER_PIN 4
int lastMarkState = 0;
int nextMarker = 1;

#define AUDIO 8
#define CHIP_SELECT 9
#define HAS_MORE_BYTES 0x80

#define NOTE_OFF_EVENT 0x80
#define NOTE_ON_EVENT 0x90
#define CONTROL_CHANGE_EVENT 0xB0
#define PITCH_BEND_EVENT 0xE0

#define RECORDING_TIMEOUT 120000000 // 2 minute timeout
unsigned long lastLoopCounter = 0;
unsigned long loopCounter = 0;

unsigned long startTime = 0;
unsigned long lastTime = 0;

#define FILE_FLUSH_INTERVAL 400;
String filename;
File file;

MIDI_CREATE_DEFAULT_INSTANCE();

/**
   Set up our inline MIDI recorder
*/
void setup() {
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.setHandlePitchBend(handlePitchBend);
  MIDI.setHandleControlChange(handleControlChange);

  // set up the tone playing button
  pinMode(AUDIO_DEBUG_PIN, INPUT);

  // set up the MIDI marker button
  pinMode(PLACE_MARKER_PIN, INPUT);

  // set up SD card functionality and allocate a file
  pinMode(CHIP_SELECT, OUTPUT);
  if (SD.begin(CHIP_SELECT)) {
    findNextFilename();
    if (file) createMidiFile();
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
   Set up a new MIDI file with some boiler plate byte code
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
   The program loop consists of flushing our file to disk,
   checking our buttons to see if they just got pressed,
   and then handling MIDI input, if there is any.
*/
void loop() {
  updateFile();
  setPlayState();
  checkForMarker();
  MIDI.read();
}


// ======================================================================================


/**
   We flush the file's in-memory content to disk
   every 400ms, allowing. That way if we take the
   SD card out, it's basically impossible for any
   data to have been lost.
*/
void updateFile() {
  loopCounter = millis();
  if (loopCounter - lastLoopCounter > FILE_FLUSH_INTERVAL) {
    checkReset();
    lastLoopCounter = loopCounter;
    file.flush();
  }
}

/**
   This "function" would normally crash any kernel that tries
   to run it by violating memory access. Instead, the Arduino's
   watchdog will auto-reboot, giving us a software "reset".
*/
void(* resetArduino) (void) = 0;

/**
  if we've not received any data for 2 minutes, and we were
  previously recording, we reset the arduino so that when
  we start playing again, we'll be doing so in a new file,
  rather than having multiple sessions with huge silence 
  between them in the same file.
*/
void checkReset() {
  if (startTime == 0) return;
  if (!file) return;
  if (micros() - lastTime > RECORDING_TIMEOUT) {
    file.close();
    resetArduino();
  }
}

/**
   A little audio-debugging: pressing the button tied to the
   audio debug pin will cause the program to play notes for
   every MIDI note-on event that comes flying by.
*/
void setPlayState() {
  int playState = digitalRead(AUDIO_DEBUG_PIN);
  if (playState != lastPlayState) {
    lastPlayState = playState;
    if (playState == 1) play = !play;
  }
}

/**
   This checks whether the MIDI marker button got pressed,
   and if so, writes a MIDI marker message into the track.
*/
void checkForMarker() {
  int markState = digitalRead(PLACE_MARKER_PIN);
  if (markState  != lastMarkState) {
    lastMarkState = markState;
    if (markState == 1) {
      writeMidiMarker();
    }
  }
}

/**
  Write a MIDI marker to file, by writing a delta, then
  the op code for "midi marker", the number of letters
  the marker label has, and then the label (using ASCII).

  For simplicity, the marker labels will just be a
  sequence number starting at "1".
*/
void writeMidiMarker() {
  if (!file) return;

  // delta + event code
  writeVarLen(file, getDelta());
  file.write(0xFF);
  file.write(0x06);

  // how many letters are we writing?
  byte len = 1;
  if (nextMarker > 9) len++;
  if (nextMarker > 99) len++;
  if (nextMarker > 999) len++;
  writeVarLen(file, len);

  // our label:
  byte marker[len];
  String(nextMarker++).getBytes(marker, len);
  file.write(marker, len);
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
  bend += 0x2000; // MIDI bend uses the range 0x0000-0x3FFF, with 0x2000 as center.
  byte lsb = bend & 0x7F;
  byte msb = bend >> 7;
  writeToFile(PITCH_BEND_EVENT, lsb, msb, getDelta());
}

/**
   This calculates the number of ticks since the last MIDI event
*/
int getDelta() {
  if (startTime == 0) {
    // if this is the first event, even if the Arduino's been
    // powered on for hours, this should be delta zero.
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
   Write "common" MIDI events to file, where common MIDI events
   all use the following data format:

     <delta> <event code> <byte> <byte>

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
   Encode a unsigned 32 bit integer as variable-length byte sequence
   of, at most, 4 7-bit-with-has-more bytes. This function is supplied
   as part of the MIDI file format specification.
*/
void writeVarLen(File file, unsigned long value) {
  // capture the first 7 bit block
  unsigned long buffer = value & 0x7f;

  // shift in 7 bit blocks with "has-more" bit from the
  // right for as long as `value` has more bits to encode.
  while ((value >>= 7) > 0) {
    buffer <<= 8;
    buffer |= HAS_MORE_BYTES;
    buffer |= value & 0x7f;
  }

  // Then unshift bytes one at a time for as long as the has-more bit is high.
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
