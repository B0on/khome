#!/usr/bin/env bash
# Builds the X expose helper for the Paperwhite 2 (armel, soft-float).
#
#   ./build.sh            -> build/xexpose
#   OUT=/tmp/xexpose ./build.sh
#
# Xlib is opened with dlopen at runtime, so only the device libc from
# ../fakekaf/sysroot is needed at link time.

set -euo pipefail
cd "$(dirname "$0")"

ZIG="${ZIG:-zig}"
TARGET="${TARGET:-arm-linux-gnueabi.2.20}"
OUT="${OUT:-build/xexpose}"
SYSROOT="$PWD/../fakekaf/sysroot"

if [ ! -f "$SYSROOT/lib/libc-2.20.so" ]; then
    echo "build.sh: missing $SYSROOT/lib/libc-2.20.so" >&2
    exit 1
fi

mkdir -p "$(dirname "$OUT")"

echo "zig cc ($TARGET, armel/soft-float) -> $OUT"
"$ZIG" cc \
    -target "$TARGET" \
    -mcpu=cortex_a9 \
    -mfloat-abi=soft \
    -std=c11 -O2 -Wall -Wextra -Werror \
    -Wl,--dynamic-linker=/lib/ld-linux.so.3 \
    -L"$SYSROOT/usr/lib" \
    -L"$SYSROOT/lib" \
    xexpose.c \
    -o "$OUT" \
    -ldl

echo "built $OUT"
