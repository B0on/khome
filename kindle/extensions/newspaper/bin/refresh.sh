#!/bin/sh
set -eu

REFRESH_SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd -P)
. "$REFRESH_SCRIPT_DIR/common.sh"

/bin/sh "$REFRESH_SCRIPT_DIR/fetch.sh" --display
exit $?
