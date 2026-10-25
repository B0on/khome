#!/bin/sh
# Twice-daily text edition job for the server host. Clock is UTC; edition IDs use Asia/Tokyo.
set -eu

REPO=${KINDLE_NEWSPAPER_ROOT:-$(CDPATH= cd "$(dirname "$0")/.." && pwd -P)}
export TZ=Asia/Tokyo
export PYTHONPATH=$REPO/scripts
LOGDIR=$REPO/logs
mkdir -p "$LOGDIR"
LOCK=$LOGDIR/generate.lock

exec 9>"$LOCK"
if ! flock -n 9; then
    printf '%s already running; skip\n' "$(date -Iseconds)" >> "$LOGDIR/generate.log"
    exit 0
fi

cd "$REPO"
{
    printf '%s start\n' "$(date -Iseconds)"
    text_status=0
    "$REPO/.venv/bin/python" "$REPO/scripts/text_edition.py" || text_status=$?
    printf '%s text status=%s\n' "$(date -Iseconds)" "$text_status"
    printf '%s end status=%s\n' "$(date -Iseconds)" "$text_status"
    exit "$text_status"
} >> "$LOGDIR/generate.log" 2>&1
