#!/bin/sh
# Sends the KOReader user patches to the device.
#
#   kindle/koreader/deploy.sh [host]
#
# The rakuyomi library patch is distributed separately (private/), so a clean
# checkout has no local patch to send.

set -eu

HERE=$(CDPATH= cd "$(dirname "$0")" && pwd -P)
ROOT=$(CDPATH= cd "$HERE/../.." && pwd -P)
# Local values are kept out of the repository. See README.md.
if [ -f "$ROOT/private/local.env" ]; then
    . "$ROOT/private/local.env"
fi
HOST=${1:-${KHOME_HOST:-kindle}}
DEST=/mnt/us/koreader/patches
SSH="ssh -o BatchMode=yes $HOST"

$SSH "mkdir -p $DEST"

# The rakuyomi patch is kept outside the public tree. Send it when a local copy
# exists, otherwise send whatever is in patches/ and skip cleanly when empty.
if [ -f "$ROOT/private/2-rakuyomi-library.lua" ]; then
    scp -q -o BatchMode=yes "$ROOT/private/2-rakuyomi-library.lua" "$HOST:$DEST/"
elif ls "$HERE"/patches/*.lua >/dev/null 2>&1; then
    scp -q -o BatchMode=yes "$HERE"/patches/*.lua "$HOST:$DEST/"
else
    echo "no local KOReader patches to send (the rakuyomi patch is distributed separately)"
fi
$SSH "ls -l $DEST"
