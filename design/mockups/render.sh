#!/usr/bin/env bash
# Render the three Kindle Paperwhite 4 home-screen mockups.
#
#   bash design/mockups/render.sh
#
# Each src/home-*.html is screenshotted with headless Chrome at 1072x1448,
# then converted to 8-bit grayscale and quantized to 16 levels so the PNGs
# approximate what a 16-gray E-ink panel can show.
#
# Requirements: Google Chrome (override with CHROME=/path/to/chrome) and
# Pillow (the repo .venv is used automatically when present).

set -euo pipefail
cd "$(dirname "$0")"
ROOT="$PWD"

WIDTH=1072
HEIGHT=1448

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

TMP="$(mktemp -d "${TMPDIR:-/tmp}/kindle-mockup.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

for v in a b c; do
  echo "rendering home-$v ..."
  "$CHROME" \
    --headless \
    --disable-gpu \
    --hide-scrollbars \
    --force-device-scale-factor=1 \
    --allow-file-access-from-files \
    --virtual-time-budget=4000 \
    --window-size="$WIDTH,$HEIGHT" \
    --screenshot="$TMP/home-$v.png" \
    "file://$ROOT/src/home-$v.html" >/dev/null 2>&1
  if [ ! -s "$TMP/home-$v.png" ]; then
    echo "error: Chrome produced no screenshot for home-$v" >&2
    exit 1
  fi
done

"$PYTHON" - "$TMP" "$ROOT" "$WIDTH" "$HEIGHT" <<'PY'
import sys
from pathlib import Path
from PIL import Image

tmp, root = Path(sys.argv[1]), Path(sys.argv[2])
width, height = int(sys.argv[3]), int(sys.argv[4])
LEVELS = 16
step = 255 // (LEVELS - 1)  # 17 -> 0,17,...,255

for name in ("a", "b", "c"):
    src = tmp / f"home-{name}.png"
    dst = root / f"home-{name}.png"

    im = Image.open(src)
    if im.size != (width, height):
        sys.exit(f"error: {src.name} is {im.size[0]}x{im.size[1]}, expected {width}x{height}")

    gray = im.convert("L")
    gray = gray.point(lambda v: ((v + step // 2) // step) * step)
    gray.save(dst, dpi=(300, 300))

    tones = [value for value, count in enumerate(gray.histogram()) if count]
    print(f"wrote {dst.name}  {gray.size[0]}x{gray.size[1]}  mode={gray.mode}  tones={len(tones)}")
    if len(tones) > LEVELS:
        sys.exit(f"error: {dst.name} has {len(tones)} tones, expected <= {LEVELS}")
PY

echo "done."
