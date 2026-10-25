#!/usr/bin/env bash
# Render every Kindle design-system screen and icon.
#
#   bash design/system/render.sh
#
# Screens: 1072x1448 CSS px, screenshotted with headless Chrome at device
# scale 1, converted to 8-bit grayscale and quantized to the 16 tones the
# Paperwhite 4 panel can actually show (0, 17, ..., 255).
# Icons: the four app glyphs at 96x96 and the two status glyphs at their
# display size, on white, quantized the same way. Icons are written before
# the screens because the screen HTML blits the finished PNGs 1:1.
#
# Requirements: Google Chrome (CHROME=/path/to/chrome) and Pillow
# (the repo .venv is used automatically when present; PYTHON= overrides).

set -euo pipefail
cd "$(dirname "$0")"
ROOT="$PWD"

WIDTH=1072
HEIGHT=1448
SCREENS="home home-many home-empty settings settings-wifi-off launching sleep"
LEVELS=16

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

TMP="$(mktemp -d "${TMPDIR:-/tmp}/kindle-system.XXXXXX")"
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

mkdir -p "$ROOT/screens" "$ROOT/icons"

# One sheet keeps every icon on the same white background and lets Pillow
# crop exact display-size bitmaps.
cat > "$TMP/icons.html" <<EOF
<!DOCTYPE html><html><head><meta charset="utf-8"><style>
html,body{margin:0;width:464px;height:220px;background:#fff}
img{position:absolute;display:block}
</style></head><body>
<img src="file://$ROOT/icons/newspaper.svg" width="96" height="96" style="left:16px;top:16px">
<img src="file://$ROOT/icons/manga.svg" width="96" height="96" style="left:128px;top:16px">
<img src="file://$ROOT/icons/browser.svg" width="96" height="96" style="left:240px;top:16px">
<img src="file://$ROOT/icons/settings.svg" width="96" height="96" style="left:352px;top:16px">
<img src="file://$ROOT/icons/wifi.svg" width="40" height="30" style="left:16px;top:144px">
<img src="file://$ROOT/icons/battery.svg" width="56" height="28" style="left:80px;top:144px">
</body></html>
EOF

echo "rendering icons ..."
shot "$TMP/icons.html" "$TMP/icons.png" 464 220

"$PYTHON" - "$TMP" "$ROOT" <<'PY'
import sys
from pathlib import Path
from PIL import Image

tmp, root = Path(sys.argv[1]), Path(sys.argv[2])
LEVELS = 16
step = 255 // (LEVELS - 1)  # 0, 17, ..., 255

ICONS = {
    "newspaper": (16, 16, 96, 96),
    "manga": (128, 16, 96, 96),
    "browser": (240, 16, 96, 96),
    "settings": (352, 16, 96, 96),
    "wifi": (16, 144, 40, 30),
    "battery": (80, 144, 56, 28),
}

sheet = Image.open(tmp / "icons.png")
fail = []
for name, (x, y, w, h) in ICONS.items():
    crop = sheet.crop((x, y, x + w, y + h)).convert("L")
    gray = crop.point(lambda v: ((v + step // 2) // step) * step)
    dst = root / "icons" / f"{name}.png"
    gray.save(dst, dpi=(300, 300))
    tones = [v for v, n in enumerate(gray.histogram()) if n]
    print(f"wrote icons/{dst.name}  {gray.size[0]}x{gray.size[1]}  mode={gray.mode}  tones={len(tones)}")
    if gray.size != (w, h):
        fail.append(f"icons/{dst.name} is {gray.size}, expected {(w, h)}")
    if gray.mode != "L":
        fail.append(f"icons/{dst.name} mode is {gray.mode}, expected L")
    if len(tones) > LEVELS:
        fail.append(f"icons/{dst.name} has {len(tones)} tones, expected <= {LEVELS}")
if fail:
    for msg in fail:
        print("error:", msg, file=sys.stderr)
    sys.exit(1)
PY

for name in $SCREENS; do
  echo "rendering $name ..."
  shot "$ROOT/src/$name.html" "$TMP/$name.png" "$WIDTH" "$HEIGHT"
done

"$PYTHON" - "$TMP" "$ROOT" "$WIDTH" "$HEIGHT" "$SCREENS" <<'PY'
import sys
from pathlib import Path
from PIL import Image

tmp, root = Path(sys.argv[1]), Path(sys.argv[2])
width, height = int(sys.argv[3]), int(sys.argv[4])
screens = sys.argv[5].split()
LEVELS = 16
step = 255 // (LEVELS - 1)

fail = []
for name in screens:
    src = tmp / f"{name}.png"
    dst = root / "screens" / f"{name}.png"
    im = Image.open(src)
    if im.size != (width, height):
        fail.append(f"{src.name} is {im.size[0]}x{im.size[1]}, expected {width}x{height}")
        continue
    gray = im.convert("L").point(lambda v: ((v + step // 2) // step) * step)
    gray.save(dst, dpi=(300, 300))
    tones = [v for v, n in enumerate(gray.histogram()) if n]
    print(f"wrote screens/{dst.name}  {gray.size[0]}x{gray.size[1]}  mode={gray.mode}  tones={len(tones)}")
    if gray.size != (width, height):
        fail.append(f"screens/{dst.name} is {gray.size}, expected {(width, height)}")
    if gray.mode != "L":
        fail.append(f"screens/{dst.name} mode is {gray.mode}, expected L")
    if len(tones) > LEVELS:
        fail.append(f"screens/{dst.name} has {len(tones)} tones, expected <= {LEVELS}")

if fail:
    for msg in fail:
        print("error:", msg, file=sys.stderr)
    sys.exit(1)
PY

echo "done."
