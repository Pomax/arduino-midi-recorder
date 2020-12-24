import os

midi_files = [f for f in os.listdir('.') if os.path.isfile(f) and '.MID' in f]

for filename in midi_files:
    file = open(filename, "rb+")
    file_size = os.path.getsize(filename)
    track_length = file_size - 22

    field_value = bytearray([
        (track_length & 0xFF000000) >> 24,
        (track_length & 0x00FF0000) >> 16,
        (track_length & 0x0000FF00) >> 8,
        (track_length & 0x000000FF),
    ])

    file.seek(18)
    file.write(file_value)
    file.close()
    print(f"Updated {filename} track length to {track_length}")
