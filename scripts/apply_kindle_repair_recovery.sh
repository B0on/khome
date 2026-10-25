#!/bin/sh
# Write USB-side recovery files when /Volumes/Kindle is mounted.
set -eu

KINDLE=${1:-/Volumes/Kindle}
REPO=$(CDPATH= cd "$(dirname "$0")/.." && pwd -P)
BIN=$KINDLE/extensions/newspaper/bin

if [ ! -d "$KINDLE/extensions/newspaper/bin" ]; then
    echo "Kindle newspaper extension not mounted at $KINDLE" >&2
    exit 1
fi

mkdir -p "$KINDLE/newspaper"
printf '%s\n' unkiosk > "$KINDLE/newspaper/unkiosk"
rm -f "$KINDLE/newspaper/kiosk"

cp "$REPO/kindle/extensions/newspaper/bin/recover_boot.sh" "$BIN/recover_boot.sh"
cat > "$BIN/autostart.sh" <<'EOF'
#!/bin/sh
exec /bin/sh /mnt/us/extensions/newspaper/bin/recover_boot.sh
EOF

sync
echo "Recovery written. Eject Kindle, then hold power 40 seconds."
echo "unkiosk=$(cat "$KINDLE/newspaper/unkiosk")"
echo "autostart:"
cat "$BIN/autostart.sh"
