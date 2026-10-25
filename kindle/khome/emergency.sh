#!/bin/sh
# Installed as /mnt/us/emergency.sh.  The jailbreak bridge job
# (/etc/upstart/bridge.conf) runs it on every framework_ready and then skips
# its own repair steps.  It starts khome once per boot, so leaving khome for
# the stock UI sticks until the next reboot.
#
# Fail-safe: touch /mnt/us/khome/disable, or delete this file over SSH.  After
# three boots where khome did not stay up for 90 s, autostart stops until
# /mnt/us/khome/autostart_failures is removed.

DIR=/mnt/us/khome
LOG=$DIR/autostart.log
MARK=/tmp/khome-autostarted
FAILURES=$DIR/autostart_failures

export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export DISPLAY=:0

log() {
    echo "$(date '+%F %T') $*" >>"$LOG"
}

# appmgrd's activeApp names the home before it has loaded, so the state it
# logs is the only reliable signal.
home_state() {
    grep -o 'lipc_id=com.lab126.booklet.home,state=[A-Z_]*' /var/log/messages 2>/dev/null |
        tail -n 1 | sed 's/.*state=//'
}

# The stock home is still loading when framework_ready fires.  Stopping the
# framework before appmgrd sees it start makes appmgrd time out 30 s later and
# show "Application Error" over khome.
launch() {
    i=0
    while [ "$(home_state)" != STARTED ] && [ "$i" -lt 60 ]; do
        sleep 1
        i=$((i + 1))
    done
    log "stock home $(home_state) after $i s"
    sleep 2
    "$DIR/khome.sh" </dev/null >/dev/null 2>&1 &
    sleep 90
    if pidof khome >/dev/null 2>&1; then
        echo 0 >"$FAILURES"
        log "khome is up"
    fi
}

if [ "$1" = launch ]; then
    launch
    exit 0
fi

if [ -e "$MARK" ]; then
    exit 0
fi

# cv_prm falls back to 4200mV on every boot.  Restore the limit set in khome
# once per boot, and before the disable check so a launcher turned off for the
# stock UI still keeps the battery at 80%.
if [ -f "$DIR/charge-limit" ] &&
    [ "$(cat "$DIR/charge-limit")" = on ] &&
    [ -w /sys/devices/system/wario_charger/wario_charger0/cv_prm ]; then
    echo 4000 >/sys/devices/system/wario_charger/wario_charger0/cv_prm
fi

if [ -e "$DIR/disable" ] || [ ! -x "$DIR/khome.sh" ]; then
    exit 0
fi
: >"$MARK"

n=$(cat "$FAILURES" 2>/dev/null)
case "$n" in
    ''|*[!0-9]*) n=0 ;;
esac
if [ "$n" -ge 3 ]; then
    log "autostart skipped after $n failed boots"
    exit 0
fi
echo $((n + 1)) >"$FAILURES"
log "autostart, attempt $((n + 1))"

setsid sh "$0" launch </dev/null >/dev/null 2>&1 &

exit 0
