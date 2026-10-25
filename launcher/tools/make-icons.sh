#!/usr/bin/env bash
# Render the six design-system SVG icons at the design size (1072-wide artwork)
# and at the Paperwhite 2 size (round(design * 758 / 1072)), using the same
# headless-Chrome + 16-tone quantization method as design/system/render.sh.
#
#   bash launcher/tools/make-icons.sh
#
# Requirements: Google Chrome (CHROME=/path/to/chrome) and Pillow (the repo
# .venv is used automatically when present; PYTHON= overrides).

set -euo pipefail
cd "$(dirname "$0")"
LAUNCHER="$(cd .. && pwd)"
ROOT="$(cd ../.. && pwd)"
SRC="$ROOT/design/system/icons"
OUT="$LAUNCHER/assets/icons"

CHROME="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"
if [ ! -x "$CHROME" ]; then
  echo "error: Chrome not found at: $CHROME" >&2
  echo "       set CHROME=/path/to/chrome" >&2
  exit 1
fi

PYTHON="${PYTHON:-$ROOT/.venv/bin/python}"
if [ ! -x "$PYTHON" ]; then
  PYTHON="$(command -v python3)"
fi
if ! "$PYTHON" -c 'import PIL' >/dev/null 2>&1; then
  echo "error: Pillow is not installed for $PYTHON" >&2
  exit 1
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/khome-icons.XXXXXX")"
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

mkdir -p "$OUT/design" "$OUT/device"

# Design sheet: identical geometry to design/system/render.sh.
cat > "$TMP/design.html" <<EOF
<!DOCTYPE html><html><head><meta charset="utf-8"><style>
html,body{margin:0;width:464px;height:220px;background:#fff}
img{position:absolute;display:block}
</style></head><body>
<img src="file://$SRC/newspaper.svg" width="96" height="96" style="left:16px;top:16px">
<img src="file://$SRC/manga.svg" width="96" height="96" style="left:128px;top:16px">
<img src="file://$SRC/browser.svg" width="96" height="96" style="left:240px;top:16px">
<img src="file://$SRC/settings.svg" width="96" height="96" style="left:352px;top:16px">
<img src="file://$SRC/wifi.svg" width="40" height="30" style="left:16px;top:144px">
<img src="file://$SRC/battery.svg" width="56" height="28" style="left:80px;top:144px">
</body></html>
EOF

# Device sheet: 96->68, 40x30->28x21, 56x28->40x20.
cat > "$TMP/device.html" <<EOF
<!DOCTYPE html><html><head><meta charset="utf-8"><style>
html,body{margin:0;width:320px;height:120px;background:#fff}
img{position:absolute;display:block}
</style></head><body>
<img src="file://$SRC/newspaper.svg" width="68" height="68" style="left:8px;top:8px">
<img src="file://$SRC/manga.svg" width="68" height="68" style="left:84px;top:8px">
<img src="file://$SRC/browser.svg" width="68" height="68" style="left:160px;top:8px">
<img src="file://$SRC/settings.svg" width="68" height="68" style="left:236px;top:8px">
<img src="file://$SRC/wifi.svg" width="28" height="21" style="left:8px;top:88px">
<img src="file://$SRC/battery.svg" width="40" height="20" style="left:48px;top:88px">
</body></html>
EOF

echo "rendering design-size icons ..."
shot "$TMP/design.html" "$TMP/design.png" 464 220
echo "rendering device-size icons ..."
shot "$TMP/device.html" "$TMP/device.png" 320 120

"$PYTHON" - "$TMP" "$OUT" <<'PY'
import sys
from pathlib import Path
from PIL import Image

tmp, out = Path(sys.argv[1]), Path(sys.argv[2])
LEVELS = 16
step = 255 // (LEVELS - 1)

SHEETS = {
    "design": (
        tmp / "design.png",
        {
            "newspaper": (16, 16, 96, 96),
            "manga": (128, 16, 96, 96),
            "browser": (240, 16, 96, 96),
            "settings": (352, 16, 96, 96),
            "wifi": (16, 144, 40, 30),
            "battery": (80, 144, 56, 28),
        },
    ),
    "device": (
        tmp / "device.png",
        {
            "newspaper": (8, 8, 68, 68),
            "manga": (84, 8, 68, 68),
            "browser": (160, 8, 68, 68),
            "settings": (236, 8, 68, 68),
            "wifi": (8, 88, 28, 21),
            "battery": (48, 88, 40, 20),
        },
    ),
}

fail = []
for size_name, (sheet_path, icons) in SHEETS.items():
    sheet = Image.open(sheet_path)
    for name, (x, y, w, h) in icons.items():
        crop = sheet.crop((x, y, x + w, y + h)).convert("L")
        gray = crop.point(lambda v: ((v + step // 2) // step) * step)
        dst = out / size_name / f"{name}.png"
        gray.save(dst, dpi=(300, 300))
        tones = [v for v, n in enumerate(gray.histogram()) if n]
        print(f"wrote {size_name}/{dst.name}  {gray.size[0]}x{gray.size[1]}  tones={len(tones)}")
        if gray.size != (w, h):
            fail.append(f"{dst} is {gray.size}, expected {(w, h)}")
        if len(tones) > LEVELS:
            fail.append(f"{dst} has {len(tones)} tones, expected <= {LEVELS}")
if fail:
    for msg in fail:
        print("error:", msg, file=sys.stderr)
    sys.exit(1)
PY

echo "done."
