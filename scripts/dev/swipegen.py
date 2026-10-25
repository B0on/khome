import struct, sys

def ev(t, c, v):
    return struct.pack('<IIHHi', 0, 0, t, c, v)

x1, y1, x2, y2 = (int(v) for v in sys.argv[1:5])
steps = int(sys.argv[5]) if len(sys.argv) > 5 else 10
steps = max(2, steps)
out = [ev(3, 0x2f, 0), ev(3, 0x39, 77), ev(3, 0x35, x1), ev(3, 0x36, y1),
       ev(1, 0x14a, 1), ev(0, 0, 0)]
for i in range(1, steps + 1):
    x = x1 + (x2 - x1) * i // steps + (i % 2)
    y = y1 + (y2 - y1) * i // steps
    out += [ev(3, 0x35, x), ev(3, 0x36, y), ev(0, 0, 0)]
out += [ev(3, 0x39, -1), ev(1, 0x14a, 0), ev(0, 0, 0)]
open('swipe.bin', 'wb').write(b''.join(out))
