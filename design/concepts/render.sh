#!/usr/bin/env bash
# Render the three UX concepts and the 3 x 3 comparison sheet.
#
#   sh design/concepts/render.sh
#
# Each src/*.html is screenshotted at 1072 x 1448 with headless Chrome at
# device scale 1, converted to 8-bit grayscale and quantized to the 16 tones
# the Paperwhite 2 panel can show (0, 17, ..., 255). Every screen is written
# twice: screens/<name>.png at 1072 x 1448 and screens/<name>-758.png at
# 758 x 1024 (0.707). compare.png is built from the 758-wide set.
#
# Requirements: Google Chrome (CHROME=/path/to/chrome) and Pillow
# (the repo .venv is used automatically when present; PYTHON= overrides).

set -euo pipefail
cd "$(dirname "$0")"
ROOT="$PWD"

WIDTH=1072
HEIGHT=1448
SMALL_WIDTH=758
SMALL_HEIGHT=1024
SCREENS="a-home a-quick a-sleep b-home b-quick b-sleep c-home c-sheet c-sleep"

CHROME="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"
if [ ! -x "$CHROME" ]; then
  echo "error: Chrome not found at: $CHROME" >&2
  echo "       set CHROME=/path/to/chrome" >&2
  exit 1
fi

PYTHON="${PYTHON:-$ROOT/../../.venv/bin/python}"
if [ ! -x "$PYTHON" ]; then
  PYTHON="$(command -v python3)"
fi
if ! "$PYTHON" -c 'import PIL' >/dev/null 2>&1; then
  echo "error: Pillow is not installed for $PYTHON" >&2
  echo "       e.g. $PYTHON -m pip install Pillow" >&2
  exit 1
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/kindle-concepts.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

shot() { # shot <html> <out.png> <w> <h>
  "$CHROME" \
    --headless \
    --disable-gpu \
    --hide-scrollbars \
    --force-device-scale-factor=1 \
    --allow-file-access-from-files \
    --default-background-color=FFFFFFFF \
    --virtual-time-budget=4000 \
    --window-size="$3,$4" \
    --screenshot="$2" \
    "file://$1" >/dev/null 2>&1
  if [ ! -s "$2" ]; then
    echo "error: Chrome produced no screenshot for $1" >&2
    exit 1
  fi
}

# Static guards: readable text at 758, large enough taps, no banned effects,
# and no Kindle or Amazon wording anywhere in the mockups.
"$PYTHON" - "$ROOT" "$SMALL_WIDTH" "$WIDTH" <<'PY'
import re
import sys
from pathlib import Path

root, small_width, width = Path(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
sources = sorted((root / "src").glob("*.html")) + [root / "src" / "base.css"]
fail = []
scale = small_width / width

for path in sources:
    text = path.read_text(encoding="utf-8")
    for size in re.findall(r"font-size:\s*(\d+)px", text):
        if int(size) * scale < 16:
            fail.append(f"{path.name}: font-size {size}px falls under 16px at 758")
    for attrs in re.findall(r'class="[^"]*\btap\b[^"]*"[^>]*?style="([^"]*)"', text, re.S):
        w = re.search(r"width:\s*(\d+)px", attrs)
        h = re.search(r"height:\s*(\d+)px", attrs)
        if not w or not h:
            fail.append(f"{path.name}: tap without explicit width/height: {attrs}")
            continue
        if int(w.group(1)) * scale < 44 or int(h.group(1)) * scale < 44:
            fail.append(f"{path.name}: tap {w.group(1)}x{h.group(1)} falls under 44px at 758")
    for banned in ("rgba", "opacity", "gradient", "box-shadow", "filter:", "blur("):
        if banned in text:
            fail.append(f"{path.name}: banned rendering effect: {banned}")
    if re.search(r"kindle|amazon", text, re.I):
        fail.append(f"{path.name}: banned brand wording")

if fail:
    for msg in fail:
        print("error:", msg, file=sys.stderr)
    sys.exit(1)
print("preflight ok: text >= 16px and taps >= 44px at 758, no banned effects or brands")
PY

mkdir -p "$ROOT/screens"

for name in $SCREENS; do
  echo "rendering $name ..."
  shot "$ROOT/src/$name.html" "$TMP/$name.png" "$WIDTH" "$HEIGHT"
done

"$PYTHON" - "$TMP" "$ROOT" "$WIDTH" "$HEIGHT" "$SMALL_WIDTH" "$SMALL_HEIGHT" "$SCREENS" <<'PY'
import sys
from pathlib import Path
from PIL import Image

tmp, root = Path(sys.argv[1]), Path(sys.argv[2])
width, height = int(sys.argv[3]), int(sys.argv[4])
small_width, small_height = int(sys.argv[5]), int(sys.argv[6])
screens = sys.argv[7].split()
LEVELS = 16
step = 255 // (LEVELS - 1)

fail = []

def tone(im):
    return [v for v, n in enumerate(im.histogram()) if n]

def quantize(im):
    return im.convert("L").point(lambda v: ((v + step // 2) // step) * step)

for name in screens:
    src = tmp / f"{name}.png"
    im = Image.open(src)
    if im.size != (width, height):
        fail.append(f"{src.name} is {im.size[0]}x{im.size[1]}, expected {width}x{height}")
        continue

    full = quantize(im)
    dst = root / "screens" / f"{name}.png"
    full.save(dst, dpi=(300, 300))
    tones = tone(full)
    print(f"wrote screens/{dst.name}  {full.size[0]}x{full.size[1]}  mode={full.mode}  tones={len(tones)}")
    if full.mode != "L":
        fail.append(f"screens/{dst.name} mode is {full.mode}, expected L")
    if len(tones) > LEVELS:
        fail.append(f"screens/{dst.name} has {len(tones)} tones, expected <= {LEVELS}")

    small = quantize(full.resize((small_width, small_height), Image.LANCZOS))
    dst = root / "screens" / f"{name}-758.png"
    small.save(dst, dpi=(212, 212))
    tones = tone(small)
    print(f"wrote screens/{dst.name}  {small.size[0]}x{small.size[1]}  mode={small.mode}  tones={len(tones)}")
    if small.size != (small_width, small_height):
        fail.append(f"screens/{dst.name} is {small.size}, expected {(small_width, small_height)}")
    if len(tones) > LEVELS:
        fail.append(f"screens/{dst.name} has {len(tones)} tones, expected <= {LEVELS}")

if fail:
    for msg in fail:
        print("error:", msg, file=sys.stderr)
    sys.exit(1)
PY

"$PYTHON" - "$ROOT" <<'PY'
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

root = Path(sys.argv[1])
fonts = root.parent / "system" / "fonts"
serif = ImageFont.truetype(str(fonts / "NotoSerifJP-Bold.ttf"), 40)
sans = ImageFont.truetype(str(fonts / "NotoSansJP-Medium.ttf"), 24)
sans_small = ImageFont.truetype(str(fonts / "NotoSansJP-Medium.ttf"), 20)

ROWS = [
    ("A", "TODAY", "a newspaper front page", ["a-home", "a-quick", "a-sleep"]),
    ("B", "CARDS", "a glanceable desk", ["b-home", "b-quick", "b-sleep"]),
    ("C", "AMBIENT", "a quiet clock", ["c-home", "c-sheet", "c-sleep"]),
]

MARGIN = 48
LABEL = 260
GAP = 24
CELL_W = 379
CELL_H = 512
HEAD = 88

sheet_w = MARGIN + LABEL + 24 + CELL_W * 3 + GAP * 2 + MARGIN
sheet_h = MARGIN + HEAD + CELL_H * 3 + GAP * 2 + MARGIN
im = Image.new("L", (sheet_w, sheet_h), 255)
d = ImageDraw.Draw(im)

for col, title in enumerate(["HOME", "CONTROLS", "SLEEP"]):
    x = MARGIN + LABEL + 24 + col * (CELL_W + GAP)
    d.text((x, MARGIN + 20), title, font=sans, fill=85)

for row, (letter, name, note, files) in enumerate(ROWS):
    y = MARGIN + HEAD + row * (CELL_H + GAP)
    d.text((MARGIN, y + 8), letter, font=serif, fill=0)
    d.text((MARGIN + 44, y + 18), name, font=serif, fill=0)
    d.text((MARGIN, y + 68), note, font=sans_small, fill=85)
    for col, stem in enumerate(files):
        x = MARGIN + LABEL + 24 + col * (CELL_W + GAP)
        src = root / "screens" / f"{stem}-758.png"
        thumb = Image.open(src).convert("L").resize((CELL_W, CELL_H), Image.LANCZOS)
        im.paste(thumb, (x, y))
        d.rectangle([x, y, x + CELL_W - 1, y + CELL_H - 1], outline=0, width=3)

step = 17
out = im.point(lambda v: ((v + step // 2) // step) * step)
out.save(root / "compare.png", dpi=(150, 150))
tones = [v for v, n in enumerate(out.histogram()) if n]
print(f"wrote compare.png  {out.size[0]}x{out.size[1]}  mode={out.mode}  tones={len(tones)}")
if len(tones) > 16:
    print("error: compare.png has more than 16 tones", file=sys.stderr)
    sys.exit(1)
PY

echo "done."
