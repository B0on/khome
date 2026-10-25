#!/bin/sh
# Install 07:00 / 18:00 JST edition cron on a UTC server host. Idempotent.
set -eu

REPO=${KINDLE_NEWSPAPER_ROOT:-$(CDPATH= cd "$(dirname "$0")/.." && pwd -P)}
JOB=$REPO/scripts/run_edition_cron.sh
MARKER='# KINDLE_TIMES_EDITIONS'

chmod +x "$JOB" "$REPO/scripts/text_edition.py"

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
crontab -l 2>/dev/null | grep -v 'KINDLE_TIMES_EDITIONS' | grep -v 'run_edition_cron.sh' > "$tmp" || true

{
    cat "$tmp"
    printf '%s\n' "$MARKER"
    printf '%s\n' "0 22 * * * $JOB"
    printf '%s\n' "0 9 * * * $JOB"
} | crontab -

printf '%s\n' "Installed edition cron (07:00 JST = 22:00 UTC, 18:00 JST = 09:00 UTC)"
crontab -l | grep -A2 'KINDLE_TIMES_EDITIONS' || crontab -l | grep run_edition_cron
