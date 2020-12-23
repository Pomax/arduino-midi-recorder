/*********************************************************

   This is the code for a prototype MIDI field recorder
   that sits between a MIDI-OUT device (like a controller)
   and a MIDI-IN device (like a computer) for saving all
   note data that's being played without having any device
   specifically set to "record".

 ********************************************************/
#include <SD.h>
#include <MIDI.h>

#define AUDIO_DEBUG_PIN 2
#define AUDIO 8
#define CHIP_SELECT 9
#define HAS_MORE_BYTES 0x80

int last_play_state = 0;
bool play;
String filename;
File file;

unsigned long startTime = 0;
unsigned long lastTime = 0;

unsigned long lastLoopCounter = 0;
unsigned long loopCounter = 0;

unsigned long RECORDING_TIMEOUT = 120000000; // 2 minutes, counted in microseconds

MIDI_CREATE_DEFAULT_INSTANCE();

/**
   This will reboot the arduino when called
*/
void(* resetArduino) (void) = 0;

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
    // We could use the EEPROM to store this number,
    // but since we're not going to get timestamped
    // files anyway, just looping is also fine.
    for (int i = 1; i < 9999; i++) {
      filename = "file-";
      if (i < 10) filename += "0";
      if (i < 100) filename += "0";
      filename += String(i);
      filename += String(".mid");
      if (!SD.exists(filename)) break;
    }

    if (filename) {
      openFile();

      byte header[] = {
        0x4D, 0x54, 0x68, 0x64,   // "MThd" chunk
        0x00, 0x00, 0x00, 0x06,   // chunk length (from this point on)
        0x00, 0x00,               // format 0
        0x00, 0x01,               // one track
        0x0F, 0xA0                // data rate = 4000 ticks per quarter note
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
        0x05, 0xF3, 0x70          // real rate = 390,000μs per quarter note
      };
      file.write(tempo, 7);
    }
  }
}

/**
   The "game loop" consists of checking whether we need to
   perform any file management, and then checking for MIDI input.
*/
void loop() {
  cycleFile();
  setPlayState();
  MIDI.read();
}


// ======================================================================================

/**
   Standard file open, in O_READ|O_CREATE|O_WRITE|O_APPEND mode
*/
void openFile() {
  if (file) file.close();
  file = SD.open(filename, FILE_WRITE);
}

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
   We close-and-reopen our file every 400ms, allowing
   us to treat the file as an always-open byte stream,
   while ensuring the file stays up to date on the SD
   card rather than being a 0 byte file because it
   never gets closed, or having to open and close it
   for every single write operation (which would be
   rather terrible, performance wise).
*/
void cycleFile() {
  loopCounter = millis();
  if (loopCounter - lastLoopCounter > 400) {
    checkReset();
    lastLoopCounter = loopCounter;
    openFile();
  }
}

/**
   This mostly exists for debuggin purposes: pressing
   the button tied to the audio debug pin will cause
   the program to play notes for every MIDI note-on
   event that comes flying by.
*/
void setPlayState() {
  int sig = digitalRead(AUDIO_DEBUG_PIN);
  if (sig != last_play_state) {
    last_play_state = sig;
    if (sig == 1) play = !play;
  }
}


// ======================================================================================


void handleNoteOn(byte channel, byte pitch, byte velocity) {
  writeToFile(0x90, pitch, velocity, getDelta());
  // Only useful for audio based debugging:
  if (play) tone(AUDIO, 440 * pow(2, (pitch - 69.0) / 12.0), 100);
}

void handleNoteOff(byte channel, byte pitch, byte velocity) {
  writeToFile(0x80, pitch, velocity, getDelta());
}

void handlePitchBend(byte channel, int bend) {
  byte lsb = (byte) (bend & 0x7F);
  byte msb = (byte) ((bend >> 7) & 0x7F);
  writeToFile(0xE0, lsb, msb, getDelta());
}

void handleControlChange(byte channel, byte cc, byte value) {
  writeToFile(0xB0, cc, value, getDelta());
}

/**
   This calculates the number of ticks since the last MIDI event
*/
int getDelta() {
  if (startTime == 0) {
    startTime = micros();
    lastTime = startTime;
    return 0;
  }
  unsigned long now = micros();
  unsigned int v = (now - lastTime) / 100;
  lastTime = now;
  return v;
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
