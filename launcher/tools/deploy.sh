#!/usr/bin/env bash
# Builds khome for the Paperwhite 2 and installs it into /mnt/us/khome.
#
#   launcher/tools/deploy.sh [host]
#
# Fonts and icons are only copied when their sha256 differs from the device's.

set -euo pipefail
cd "$(dirname "$0")/.."

ROOT="$(cd .. && pwd)"
# Local values are kept out of the repository. See README.md.
if [ -f "$ROOT/private/local.env" ]; then
    . "$ROOT/private/local.env"
fi
HOST="${1:-${KHOME_HOST:-kindle}}"
DEST=/mnt/us/khome
SSH=(ssh -o BatchMode=yes "$HOST")
SCP=(scp -q -o BatchMode=yes)

sha_local() {
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        sha256sum "$1" | awk '{print $1}'
    fi
}

sha_remote() {
    "${SSH[@]}" "sha256sum '$1' 2>/dev/null | awk '{print \$1}'" || true
}

push_if_changed() {
    local src="$1" dst="$2"
    local want have
    want="$(sha_local "$src")"
    have="$(sha_remote "$dst")"
    if [ "$want" = "$have" ]; then
        return 0
    fi
    "${SCP[@]}" "$src" "$HOST:$dst"
    echo "sent $(basename "$src")"
}

# A running binary cannot be overwritten through scp (ETXTBSY).  Send the new
# copy beside it, then rename over the old inode on the device.
push_replace() {
    local src="$1" dst="$2"
    "${SCP[@]}" "$src" "$HOST:$dst.new"
    "${SSH[@]}" "mv -f '$dst.new' '$dst'"
    echo "installed $(basename "$dst")"
}

./build.sh arm
( cd "$ROOT/kindle/fakekaf" && ./build.sh )
( cd "$ROOT/kindle/xexpose" && ./build.sh )
( cd "$ROOT/kindle/xband" && ./build.sh )

"${SSH[@]}" "mkdir -p $DEST/bin $DEST/fonts $DEST/icons/design $DEST/icons/device"

push_replace build/arm/khome "$DEST/bin/khome"
push_replace "$ROOT/kindle/fakekaf/build/fakekaf" "$DEST/bin/fakekaf"
push_replace "$ROOT/kindle/xexpose/build/xexpose" "$DEST/bin/xexpose"
push_replace "$ROOT/kindle/xband/build/xband" "$DEST/bin/xband"
push_replace ../kindle/khome/khome.sh "$DEST/khome.sh"
push_replace ../kindle/khome/emergency.sh "/mnt/us/emergency.sh"
push_if_changed "$ROOT/kindle/khome/cacert.pem" "$DEST/cacert.pem"
"${SSH[@]}" "chmod 755 $DEST/bin/khome $DEST/bin/fakekaf $DEST/bin/xexpose $DEST/bin/xband $DEST/khome.sh"

for font in "$ROOT"/design/system/fonts/*.ttf; do
    push_if_changed "$font" "$DEST/fonts/$(basename "$font")"
done

for icon in assets/icons/design/*.png; do
    push_if_changed "$icon" "$DEST/icons/design/$(basename "$icon")"
done
for icon in assets/icons/device/*.png; do
    push_if_changed "$icon" "$DEST/icons/device/$(basename "$icon")"
done

"${SSH[@]}" "ls -l $DEST/bin/khome $DEST/bin/fakekaf $DEST/bin/xexpose $DEST/bin/xband $DEST/khome.sh"
echo "deploy: $HOST:$DEST"
