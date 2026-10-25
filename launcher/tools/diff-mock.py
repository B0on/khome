#!/usr/bin/env python3
"""Compare khome-render design output with design/system/screens.

Standard library only: decodes 8-bit grayscale PNGs (the only format both the
mock and khome-render emit), reports the fraction of differing pixels, and
writes abs-difference PNGs.

    python3 launcher/tools/diff-mock.py
    python3 launcher/tools/diff-mock.py --render /tmp/khome-out/design \
        --mock design/system/screens --out /tmp/khome-out/diff
"""

import argparse
import os
import struct
import sys
import zlib

SCREENS = [
    "home",
    "home-many",
    "home-empty",
    "settings",
    "settings-wifi-off",
    "launching",
    "sleep",
]


def paeth(a, b, c):
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos = 8
    width = height = bitdepth = colortype = interlace = None
    idat = bytearray()
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            width, height, bitdepth, colortype, comp, filt, interlace = \
                struct.unpack(">IIBBBBB", chunk)
            if comp != 0 or filt != 0:
                raise ValueError(f"{path}: unsupported compression/filter")
        elif ctype == b"IDAT":
            idat.extend(chunk)
        elif ctype == b"IEND":
            break
    if width is None:
        raise ValueError(f"{path}: missing IHDR")
    if bitdepth != 8 or colortype != 0:
        raise ValueError(
            f"{path}: expected 8-bit grayscale, got depth={bitdepth} "
            f"type={colortype}")
    if interlace != 0:
        raise ValueError(f"{path}: interlaced PNG is not supported")
    raw = zlib.decompress(bytes(idat))
    stride = width
    expected = (stride + 1) * height
    if len(raw) != expected:
        raise ValueError(f"{path}: bad IDAT size {len(raw)} != {expected}")
    out = bytearray(stride * height)
    prev = bytearray(stride)
    src = 0
    for y in range(height):
        ftype = raw[src]
        src += 1
        line = bytearray(raw[src:src + stride])
        src += stride
        if ftype == 0:
            pass
        elif ftype == 1:
            for x in range(1, stride):
                line[x] = (line[x] + line[x - 1]) & 0xFF
        elif ftype == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 0xFF
        elif ftype == 3:
            for x in range(stride):
                left = line[x - 1] if x > 0 else 0
                line[x] = (line[x] + ((left + prev[x]) >> 1)) & 0xFF
        elif ftype == 4:
            for x in range(stride):
                left = line[x - 1] if x > 0 else 0
                upleft = prev[x - 1] if x > 0 else 0
                line[x] = (line[x] + paeth(left, prev[x], upleft)) & 0xFF
        else:
            raise ValueError(f"{path}: bad filter {ftype}")
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, out


def write_png(path, width, height, pixels):
    def chunk(ctype, payload):
        return (struct.pack(">I", len(payload)) + ctype + payload +
                struct.pack(">I", zlib.crc32(ctype + payload) & 0xFFFFFFFF))

    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw.extend(pixels[y * width:(y + 1) * width])
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def compare(mock_path, render_path, diff_path):
    mw, mh, mock = read_png(mock_path)
    rw, rh, render = read_png(render_path)
    if (mw, mh) != (rw, rh):
        raise ValueError(
            f"size mismatch: mock {mw}x{mh}, render {rw}x{rh}")
    total = mw * mh
    diff = bytearray(total)
    different = 0
    significant = 0
    total_abs = 0
    min_x, min_y, max_x, max_y = mw, mh, -1, -1
    for i in range(total):
        d = abs(mock[i] - render[i])
        diff[i] = d
        total_abs += d
        if d:
            different += 1
            x = i % mw
            y = i // mw
            if x < min_x:
                min_x = x
            if x > max_x:
                max_x = x
            if y < min_y:
                min_y = y
            if y > max_y:
                max_y = y
        if d > 8:
            significant += 1
    if diff_path is not None:
        write_png(diff_path, mw, mh, diff)
    bbox = (min_x, min_y, max_x + 1, max_y + 1) if max_x >= 0 else None
    return {
        "width": mw,
        "height": mh,
        "different": different,
        "significant": significant,
        "total": total,
        "mean_abs": total_abs / total,
        "bbox": bbox,
    }


def main():
    repo = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    parser = argparse.ArgumentParser()
    parser.add_argument("--mock",
                        default=os.path.join(repo, "design/system/screens"))
    parser.add_argument("--render", default="/tmp/khome-out/design")
    parser.add_argument("--out", default="/tmp/khome-out/diff")
    args = parser.parse_args()

    if not os.path.isdir(args.render):
        print(f"error: render directory not found: {args.render}",
              file=sys.stderr)
        return 1
    if not os.path.isdir(args.mock):
        print(f"error: mock directory not found: {args.mock}", file=sys.stderr)
        return 1
    os.makedirs(args.out, exist_ok=True)

    print(f"{'screen':20s} {'pixels differ':>14s} {'>8':>10s} "
          f"{'mean |d|':>9s}  bbox")
    for name in SCREENS:
        mock_path = os.path.join(args.mock, name + ".png")
        render_path = os.path.join(args.render, name + ".png")
        if not os.path.exists(mock_path):
            print(f"{name:20s} (mock missing)")
            continue
        if not os.path.exists(render_path):
            print(f"{name:20s} (render missing)")
            continue
        diff_path = os.path.join(args.out, name + "-diff.png")
        try:
            r = compare(mock_path, render_path, diff_path)
        except ValueError as exc:
            print(f"{name:20s} error: {exc}", file=sys.stderr)
            continue
        pct = 100.0 * r["different"] / r["total"]
        sig_pct = 100.0 * r["significant"] / r["total"]
        print(f"{name:20s} {r['different']:7d} ({pct:5.2f}%) "
              f"{r['significant']:7d} ({sig_pct:5.2f}%) "
              f"{r['mean_abs']:8.3f}  {r['bbox']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
