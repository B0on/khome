#!/bin/bash
set -eu

D=$(cd "$(dirname "$0")" && pwd)
K='export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin DISPLAY=:0;'
LOG=/mnt/us/khome/khome.log
FAV=/mnt/us/khome/favorites.tsv
TABS=/mnt/us/khome/tabs.txt
SAVE=/tmp/kpw2

HOME_BROWSER_X=452
HOME_BROWSER_Y=524

SHEET_HOME_X=243
SHEET_FAVORITE_X=515
SHEET_RELOAD_X=650
SHEET_NEW_X=379
SHEET_BUTTON_Y=76

EDIT_CANCEL_X=110
EDIT_CANCEL_Y=45

SETTINGS_TILE_X=600
SETTINGS_TILE_Y=520
SETTINGS_BACK_X=113
SETTINGS_BACK_Y=172

PAGE_NEXT_X=670
PAGE_NEXT_Y=981

dev() { ssh -o BatchMode=yes kindle "$K $*"; }
logtail() { dev "tail -n $1 $LOG"; }
shot() { "$D/kshot.sh" "$1" >/dev/null; }
tap() { "$D/ktap.sh" "$1" "$2"; sleep "${3:-2}"; }
open_sheet() { "$D/ksweep.sh" 379 5 379 500; sleep 2; }
fav_count() { dev "wc -l < $FAV"; }
tabs_count() { dev "wc -l < $TABS"; }
appmgr_fails() { dev "grep -c appmgrAppFailedFatal /var/log/messages || true"; }
alert_shown() { dev "grep -c default_alert_displayed /var/log/messages || true"; }

# Every tap below assumes the home screen; from Settings the browser tile lands
# on the front light buttons.  Check before the trap, which would otherwise
# copy an older run's snapshot over the live files.
if ! dev "grep ' -> ' $LOG | tail -n 1" | grep -q -- '-> HOME$'; then
    echo "khome is not on the home screen; go home first" >&2
    exit 1
fi

# Stage 8 empties favorites.tsv, so an aborted run has to put the files back.
restore_snapshot() {
    dev "cp $SAVE/favorites.before-flows $FAV 2>/dev/null; cp $SAVE/tabs.before-flows $TABS 2>/dev/null; true" || true
}
trap restore_snapshot EXIT

echo "== 0. snapshot favorites.tsv and tabs.txt"
dev "mkdir -p $SAVE && cp $FAV $SAVE/favorites.before-flows && cp $TABS $SAVE/tabs.before-flows && ls -l $SAVE"
printf 'appmgrAppFailedFatal: %s\n' "$(appmgr_fails)"
printf 'default_alert_displayed: %s\n' "$(alert_shown)"
logtail 3
shot flows-00-prep

echo "== 1. home: browser tile, 15s"
tap "$HOME_BROWSER_X" "$HOME_BROWSER_Y" 15
logtail 8
shot flows-01-browser

echo "== 2. sheet: Favorite"
open_sheet
tap "$SHEET_FAVORITE_X" "$SHEET_BUTTON_Y" 3
logtail 8
shot flows-02-favorite
printf 'last favorite title: '
dev "tail -n 1 $FAV | cut -f2"

echo "== 3. sheet: Reload, 5s"
open_sheet
tap "$SHEET_RELOAD_X" "$SHEET_BUTTON_Y" 5
logtail 8
shot flows-03-reload

echo "== 4. sheet: New tab, 10s, then Cancel"
open_sheet
tap "$SHEET_NEW_X" "$SHEET_BUTTON_Y" 10
logtail 8
shot flows-04-edit
tap "$EDIT_CANCEL_X" "$EDIT_CANCEL_Y" 2
logtail 6
printf 'tabs.txt lines: %s\n' "$(tabs_count)"

echo "== 5. tabs.txt line count"
printf 'tabs.txt lines: %s\n' "$(tabs_count)"
logtail 4
shot flows-05-tabs

echo "== 6. sheet: Home, 8s"
open_sheet
tap "$SHEET_HOME_X" "$SHEET_BUTTON_Y" 8
logtail 8
shot flows-06-home

echo "== 7. seven favorites and paging"
dev "for i in 1 2 3 4 5 6; do printf 'https://example.com/p%s\\tp%s\\n' \$i \$i; done >> $FAV"
printf 'favorites.tsv lines: %s (paging needs 7 or more)\n' "$(fav_count)"
tap "$SETTINGS_TILE_X" "$SETTINGS_TILE_Y" 3
logtail 4
tap "$SETTINGS_BACK_X" "$SETTINGS_BACK_Y" 3
logtail 6
shot flows-07-page1
tap "$PAGE_NEXT_X" "$PAGE_NEXT_Y" 3
logtail 4
shot flows-07-page2

echo "== 8. empty favorites"
dev ": > $FAV"
tap "$SETTINGS_TILE_X" "$SETTINGS_TILE_Y" 3
tap "$SETTINGS_BACK_X" "$SETTINGS_BACK_Y" 3
logtail 6
printf 'favorites.tsv lines: %s\n' "$(fav_count)"
shot flows-08-empty

echo "== 9. restore favorites.tsv and tabs.txt"
dev "cp $SAVE/favorites.before-flows $FAV && cp $SAVE/tabs.before-flows $TABS"
tap "$SETTINGS_TILE_X" "$SETTINGS_TILE_Y" 3
tap "$SETTINGS_BACK_X" "$SETTINGS_BACK_Y" 3
logtail 8
printf 'favorites.tsv lines: %s\n' "$(fav_count)"
printf 'tabs.txt lines: %s\n' "$(tabs_count)"
printf 'appmgrAppFailedFatal: %s\n' "$(appmgr_fails)"
printf 'default_alert_displayed: %s\n' "$(alert_shown)"
shot flows-09-restored
