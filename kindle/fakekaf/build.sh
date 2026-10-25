#!/usr/bin/env bash
# Build fakekaf as an armel (soft-float) dynamically linked binary for the
# Kindle Paperwhite 2 (FW 5.12.2.1.1).
#
# The device libraries are kept in sysroot/ (gitignored) and are used for
# linking so that the binary targets the device's glibc 2.20 and liblipc.so.1
# exactly.  The toolchain is zig cc, the same one launcher/build.sh uses.
#
#   ./build.sh            -> build/fakekaf
#   OUT=/tmp/fakekaf ./build.sh
#   ZIG=/path/to/zig ./build.sh
#
# Re-populate sysroot/ (only needed on a fresh checkout):
#   see docs/spikes/fakekaf.md, section "sysroot"

set -euo pipefail
cd "$(dirname "$0")"

ZIG="${ZIG:-zig}"
TARGET="${TARGET:-arm-linux-gnueabi.2.20}"
OUT="${OUT:-build/fakekaf}"
SYSROOT="$PWD/sysroot"

if [ ! -f "$SYSROOT/usr/lib/liblipc.so.1.0" ]; then
    echo "build.sh: missing $SYSROOT/usr/lib/liblipc.so.1.0" >&2
    echo "build.sh: copy the device libraries into sysroot/ first" >&2
    exit 1
fi
if [ ! -f "$SYSROOT/lib/libc-2.20.so" ]; then
    echo "build.sh: missing $SYSROOT/lib/libc-2.20.so" >&2
    echo "build.sh: copy the device libraries into sysroot/ first" >&2
    exit 1
fi

mkdir -p "$(dirname "$OUT")"

echo "zig cc ($TARGET, armel/soft-float) -> $OUT"
"$ZIG" cc \
    -target "$TARGET" \
    -mcpu=cortex_a9 \
    -mfloat-abi=soft \
    -std=c11 -O2 -Wall -Wextra -Werror \
    -I"$PWD" \
    -Wl,--dynamic-linker=/lib/ld-linux.so.3 \
    -Wl,--allow-shlib-undefined \
    -L"$SYSROOT/usr/lib" \
    -L"$SYSROOT/lib" \
    fakekaf.c \
    -o "$OUT" \
    -llipc

echo "built $OUT"
