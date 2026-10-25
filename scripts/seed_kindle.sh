#!/bin/sh
# Seed the USB-mounted Kindle with the KUAL extension and a cached text edition.
set -eu

REPO=$(CDPATH= cd "$(dirname "$0")/.." && pwd -P)
KINDLE=${1:-/Volumes/Kindle}
EDITIONS=$REPO/out/editions

# Local values are kept out of the repository. See README.md.
if [ -f "$REPO/private/local.env" ]; then
    . "$REPO/private/local.env"
fi

if [ ! -d "$KINDLE/extensions" ]; then
    echo "Kindle volume not mounted at $KINDLE" >&2
    exit 1
fi
if [ ! -f "$EDITIONS/latest-text.json" ]; then
    echo "Missing packed text edition at $EDITIONS/latest-text.json" >&2
    exit 1
fi

edition_path=$(python3 - <<PY
import json
from pathlib import Path
latest = json.loads(Path("$EDITIONS/latest-text.json").read_text())
print(latest["path"])
PY
)

src_edition=$EDITIONS/$edition_path
if [ ! -d "$src_edition" ]; then
    echo "Edition directory missing: $src_edition" >&2
    exit 1
fi

echo "Installing KUAL extension..."
rm -rf "$KINDLE/extensions/newspaper"
cp -R "$REPO/kindle/extensions/newspaper" "$KINDLE/extensions/newspaper"
chmod +x "$KINDLE/extensions/newspaper/bin/"*.sh
rm -f "$KINDLE/extensions/newspaper/bin/._"* "$KINDLE/extensions/newspaper/._"*

echo "Seeding /mnt/us/newspaper/text cache..."
mkdir -p "$KINDLE/newspaper/text/current"
rm -rf "$KINDLE/newspaper/text/current"/*
cp -R "$src_edition/." "$KINDLE/newspaper/text/current/"
cp "$EDITIONS/latest-text.json" "$KINDLE/newspaper/text/latest-text.json"
if [ -z "${KINDLE_SERVER_URL:-}" ]; then
    echo "Set KINDLE_SERVER_URL (for example in private/local.env) to the LAN URL of the edition server." >&2
    exit 1
fi
printf '%s\n' "$KINDLE_SERVER_URL" > "$KINDLE/newspaper/server.url"

echo "Done. Extension -> extensions/newspaper/, cache -> newspaper/text/current/ ($edition_path)"
echo "Emergency Library: create /mnt/us/khome/disable on USB."
