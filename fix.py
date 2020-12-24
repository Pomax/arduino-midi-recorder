import os

midi_files = [f for f in os.listdir('.') if os.path.isfile(f) and '.MID' in f]

for filename in midi_files:
    # Open the file in binary read/write mode:
    file = open(filename, "rb+")

    # As single-track MIDI files, we know that the track length is
    # equal to the file size, minus the header size up to and
    # including the length value, which is 22 bytes:
    file_size = os.path.getsize(filename)
    track_length = file_size - 22

    # With that done, we can read in the file as a byte array:
    data = bytearray(file.read())

    # And update the track length bytes to the correct value:
    data[18] = (track_length & 0xFF000000) >> 24
    data[19] = (track_length & 0x00FF0000) >> 16
    data[20] = (track_length & 0x0000FF00) >> 8
    data[21] = (track_length & 0x000000FF)

    # Then we write the update file back to disk:
    file.seek(0)
    file.write(data)
    print(f"Updated {filename} track length to {track_length} bytes")
