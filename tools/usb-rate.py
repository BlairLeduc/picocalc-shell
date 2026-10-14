#!/usr/bin/env python3
"""usb-rate.py — time usbdisk each way from a Mac (design.md §10, M6).

  tools/usb-rate.py /Volumes/PICOCALC          # 32 MB
  tools/usb-rate.py /Volumes/PICOCALC 64

Writes a file of random bytes to the card through the mounted volume,
reads it back, and compares it byte for byte with what was written, then
removes it. Both ways bypass macOS's cache (F_NOCACHE), and the write's
time includes F_FULLFSYNC, so each rate is the card's over USB, not RAM's.
Use a card set aside for testing (EL §13.4); the file is usbrate.bin at
the volume's root. Nothing else on the card is touched.
"""
import fcntl, os, sys, time

F_NOCACHE = getattr(fcntl, 'F_NOCACHE', 48)
F_FULLFSYNC = getattr(fcntl, 'F_FULLFSYNC', 51)
CHUNK = 1 << 20

volume = sys.argv[1]
mb = int(sys.argv[2]) if len(sys.argv) > 2 else 32
if not os.path.ismount(volume):
    sys.exit(f'usb-rate.py: {volume} is not a mounted volume')
path = os.path.join(volume, 'usbrate.bin')
data = os.urandom(mb * CHUNK)

fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC)
fcntl.fcntl(fd, F_NOCACHE, 1)
t = time.monotonic()
for i in range(0, len(data), CHUNK):
    os.write(fd, data[i:i + CHUNK])
fcntl.fcntl(fd, F_FULLFSYNC)
write_s = time.monotonic() - t
os.close(fd)

fd = os.open(path, os.O_RDONLY)
fcntl.fcntl(fd, F_NOCACHE, 1)
back = bytearray()
t = time.monotonic()
while True:
    piece = os.read(fd, CHUNK)
    if not piece:
        break
    back += piece
read_s = time.monotonic() - t
os.close(fd)
os.remove(path)

kb = len(data) / 1024
print(f'{mb} MB written in {write_s:.1f} s, {kb / write_s:.0f} KB/s')
print(f'{mb} MB read in {read_s:.1f} s, {kb / read_s:.0f} KB/s')
if back != data:
    first = next((i for i in range(min(len(back), len(data))) if back[i] != data[i]),
                 min(len(back), len(data)))
    sys.exit(f'usb-rate.py: MISMATCH: read {len(back)} bytes, first difference at byte {first}')
print('read back byte for byte as written')
