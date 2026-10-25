#!/bin/sh
# Runs khome on the "no cvm + fakekaf" path: X, awesome, blanket, pillow, kb,
# appmgrd and powerd stay up, only the Java framework (cvm) is kept from
# starting.  fakekaf answers powerd's com.lab126.kaf frameworkStarted query and
# unloads the boot splash.  On exit the stock framework is started again.

DIR=/mnt/us/khome
LOG=$DIR/khome.log
FLAG=/mnt/us/DONT_START_FRAMEWORK
FAKEKAF=$DIR/bin/fakekaf
FAKEKAF_PID=$DIR/fakekaf.pid

export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export DISPLAY=:0
export TZ=JST-9

log() {
    echo "khome.sh: $*" >>"$LOG"
}

# Starts fakekaf when it is missing and waits for com.lab126.kaf to answer.
# appmgrd needs the home-booklet stand-in to hand GO to a stopped app; without
# it appmgrd loops on the failed home GO and never sends GO to the browser.
start_fakekaf() {
    if pidof fakekaf >/dev/null 2>&1; then
        return 0
    fi
    if pidof cvm >/dev/null 2>&1; then
        log "cvm is running, not starting fakekaf"
        return 1
    fi
    "$FAKEKAF" >>"$DIR/fakekaf.log" 2>&1 9>&- &
    fakepid=$!
    echo "$fakepid" >"$FAKEKAF_PID"
    i=0
    while [ "$i" -lt 20 ]; do
        if [ "$(lipc-get-prop com.lab126.kaf frameworkStarted 2>/dev/null)" = "1" ]; then
            break
        fi
        sleep 0.25
        i=$((i + 1))
    done
    if [ "$i" -ge 20 ]; then
        return 1
    fi
    return 0
}

# fakekaf can die without khome restarting, and a khome session can run for
# hours after that.  Keep it alive while the no-cvm path owns the device.
fakekaf_watch() {
    while :; do
        sleep 5
        if pidof cvm >/dev/null 2>&1; then
            log "cvm is running, fakekaf watchdog stopping"
            return 1
        fi
        if ! pidof fakekaf >/dev/null 2>&1; then
            if start_fakekaf; then
                log "fakekaf restarted, pid $(cat "$FAKEKAF_PID")"
            else
                log "fakekaf did not register after a restart"
            fi
        fi
    done
}

if [ -e "$DIR/disable" ]; then
    echo "khome.sh: $DIR/disable exists, not starting" >>"$LOG"
    exit 0
fi

if pidof khome >/dev/null 2>&1; then
    echo "khome.sh: khome already running" >>"$LOG"
    exit 0
fi

exec 9>"$DIR/khome.pid"
if ! flock -n 9; then
    echo "khome.sh: another launcher holds $DIR/khome.pid" >>"$LOG"
    exit 0
fi
: >"$LOG"

flag_read_count() {
    n=$(grep -c 'framework:dont_start' /var/log/messages 2>/dev/null) || n=0
    echo "$n"
}

framework_stopped() {
    initctl status framework 2>/dev/null | grep -q 'stop/waiting'
}

# appmgrd gives the home booklet load and go, and treats a reply that never
# arrives as an app failure.  31 s later it displays the stock "Application
# Error" alert.  It exposes no state property, so the state it logs is the
# signal.  lipc-probe lists only activeApp and the write-only result
# properties.
home_state() {
    grep -o 'lipc_id=com.lab126.booklet.home,state=[A-Z_]*' /var/log/messages 2>/dev/null |
        tail -n 1 | sed 's/.*state=//'
}

# Stopping lab126_gui while cvm is still answering load or go kills the home
# booklet mid-request and leaves appmgrd waiting.  Wait for a settled state
# first, but never longer than appmgrd's own 31 s timeout.
wait_for_home_idle() {
    i=0
    seen=0
    stable=0
    while [ "$i" -lt 60 ]; do
        state=$(home_state)
        case "$state" in
            STARTED|PAUSED|UNLOADED)
                # A second sample a half second later keeps a transition that
                # syslog has not flushed yet from looking settled.
                stable=$((stable + 1))
                if [ "$stable" -ge 2 ]; then
                    return 0
                fi
                ;;
            '')
                # No home state since syslog last started means appmgrd is
                # not waiting on the booklet, because a pending load or go
                # always logs its state first.  Two seconds covers a flush.
                stable=0
                if [ "$seen" = "0" ] && [ "$i" -ge 4 ]; then
                    return 0
                fi
                ;;
            *)
                stable=0
                seen=1
                ;;
        esac
        sleep 0.5
        i=$((i + 1))
    done
    log "home state is $(home_state) after 30 s, stopping lab126_gui anyway"
    return 1
}

RESTORED=0

# koreader.sh execs /var/tmp/koreader.sh and runs reader.lua through its own
# luajit, so all three names can outlive a killed khome.sh.
KOREADER_PROCS="koreader.sh reader.lua luajit"

koreader_alive() {
    for name in $KOREADER_PROCS; do
        pidof "$name" >/dev/null 2>&1 && return 0
    done
    return 1
}

koreader_signal() {
    for name in $KOREADER_PROCS; do
        killall "-$1" "$name" >/dev/null 2>&1
    done
}

# powerd sleeps after 600 s idle and that timeout cannot be raised, so the
# timer is reset while awake.  preventScreenSaver is not used because it also
# disables the power button.  An asleep device is left alone.
keepawake() {
    while :; do
        if [ "$(lipc-get-prop com.lab126.powerd state 2>/dev/null)" = "active" ]; then
            lipc-set-prop com.lab126.powerd touchScreenSaverTimeout 1 >/dev/null 2>&1
        fi
        sleep 240
    done
}

KEEPAWAKE_PID=
FAKEKAF_WATCH_PID=

restore() {
    trap '' INT TERM HUP
    [ "$RESTORED" = "1" ] && return 0
    RESTORED=1
    if [ -n "$KEEPAWAKE_PID" ]; then
        kill -TERM "$KEEPAWAKE_PID" >/dev/null 2>&1
    fi
    if [ -n "$FAKEKAF_WATCH_PID" ]; then
        kill -TERM "$FAKEKAF_WATCH_PID" >/dev/null 2>&1
        FAKEKAF_WATCH_PID=
    fi
    koreader_left=0
    if koreader_alive; then
        koreader_left=1
        log "KOReader is still running, terminating it"
        koreader_signal TERM
        i=0
        while [ "$i" -lt 50 ] && koreader_alive; do
            sleep 0.1
            i=$((i + 1))
        done
        if koreader_alive; then
            koreader_signal KILL
        fi
    fi
    # khome's own cleanup may have killed KOReader already; it leaves this.
    if [ -e /var/tmp/khome.koreader-killed ]; then
        koreader_left=1
        rm -f /var/tmp/khome.koreader-killed
    fi
    app=$(lipc-get-prop com.lab126.appmgrd activeApp 2>/dev/null)
    if [ "$app" = "com.lab126.browser" ] || pidof mesquite >/dev/null 2>&1; then
        # khome stops the browser when it exits; this covers a crash or a
        # khome that was SIGKILLed mid-cleanup.  The stop goes through
        # appmgrd like the close path, because setting the browser's kill
        # property is an abnormal exit and appmgrd answers it with its
        # "Application Error" alert.  fakekaf is still up here to answer as
        # the home booklet that stop hands GO to.
        log "stopping the browser through appmgrd"
        lipc-set-prop com.lab126.appmgrd stop com.lab126.browser >/dev/null 2>&1
        i=0
        while [ "$i" -lt 50 ] && pidof mesquite >/dev/null 2>&1; do
            sleep 0.1
            i=$((i + 1))
        done
        if pidof mesquite >/dev/null 2>&1; then
            log "browser ignored the stop for 5 s, setting kill"
            lipc-set-prop com.lab126.browser kill 1 >/dev/null 2>&1
            sleep 2
        fi
        if pidof mesquite >/dev/null 2>&1; then
            log "browser still running, terminating mesquite"
            killall -TERM mesquite >/dev/null 2>&1
        fi
    fi
    rm -f "$FAKEKAF_PID"
    for pid in $(pidof fakekaf); do
        kill -TERM "$pid" >/dev/null 2>&1
    done
    i=0
    while [ "$i" -lt 30 ] && pidof fakekaf >/dev/null 2>&1; do
        sleep 0.1
        i=$((i + 1))
    done
    for pid in $(pidof fakekaf); do
        kill -KILL "$pid" >/dev/null 2>&1
    done
    # fakekaf answers as com.lab126.booklet.home.  lab126_gui starts cvm, which
    # registers the real home booklet, so no fakekaf may still hold the name.
    # The block above runs before /sbin/start lab126_gui below; this wait only
    # makes the kill land before that start.
    i=0
    while [ "$i" -lt 30 ] && pidof fakekaf >/dev/null 2>&1; do
        sleep 0.1
        i=$((i + 1))
    done
    if pidof fakekaf >/dev/null 2>&1; then
        log "fakekaf did not exit; the stock home booklet may not register"
    fi
    /sbin/stop lab126_gui
    if [ "$koreader_left" = "1" ]; then
        # A killed KOReader leaves winmgr stuck, and lab126_gui alone does not
        # restart the x job; cvm then dumpstacks without a working winmgr.
        log "KOReader was running, restarting x"
        /sbin/stop x
        /sbin/start x
    fi
    rm -f "$FLAG"
    # A killed KOReader leaves volumd stopped; stock needs it to answer
    # driveModeState and to bind /chroot for the browser.
    killall -CONT volumd 2>/dev/null
    /sbin/start lab126_gui
    log "stock framework started"
}

# The flag window in start_gui_without_framework has no khome to stop yet: a
# signal there only needs the stock GUI back, and restore removes the flag
# before it starts lab126_gui.  Installed before the first stop so a signal
# cannot leave lab126_gui stopped either.
on_signal_before_start() {
    trap '' INT TERM HUP
    log "signal before khome started, restoring stock"
    restore
    exit 0
}

trap 'on_signal_before_start' INT TERM HUP

# lab126_gui.conf's pre-start emits start_splash, so every start puts the boot
# splash back.  fakekaf unloads it once it is registered.
start_gui_without_framework() {
    before=$(flag_read_count)
    touch "$FLAG"
    /sbin/start lab126_gui
    # /etc/upstart/framework.conf is the only reader of the flag: its pre-start
    # stops the job when the file exists.  framework starts on
    # contentpack_font_ready, emitted by contentpack_fonts_dependency.conf
    # ("start on started lab126_gui"), which polls contentpackd with
    # lipc-wait-event -s 2 up to five times.  contentpackd is stopped by
    # lab126_gui, so that wait can take ~10 s: right after `start` framework is
    # still stop/waiting from the previous stop, and deleting the flag then
    # would let cvm start when the event finally fires.  The pre-start's own
    # f_log line is the proof that it ran with the flag in place.
    i=0
    while [ "$i" -lt 60 ]; do
        if [ "$(flag_read_count)" -gt "$before" ] && framework_stopped; then
            break
        fi
        sleep 0.5
        i=$((i + 1))
    done
    if [ "$i" -ge 60 ]; then
        # The pre-start may not have read the flag yet; removing it now would let
        # a later contentpack_font_ready start cvm alongside khome.
        log "no framework:dont_start line after 30 s, restoring stock"
        restore
        return 1
    fi
    if pidof cvm >/dev/null 2>&1; then
        log "cvm started despite the flag, leaving stock alone"
        rm -f "$FLAG"
        return 1
    fi
    # Safe now: the pre-start has already read the flag this boot.  Removing it
    # keeps the next boot (or a later framework start) on the stock path.
    rm -f "$FLAG"
    log "framework is stop/waiting, flag removed"
    return 0
}

# KOReader SIGSTOPs volumd and only SIGCONTs it on its clean exit path, so a
# killed KOReader leaves it stopped; driveModeState then times out and
# chroot.conf skips the /chroot bind, which kills the browser at launch.
killall -CONT volumd 2>/dev/null

# Stopping lab126_gui is the only way to be sure cvm stays down.  A lab126_gui
# that was just started has no cvm yet, but cvm follows it about 10 s later.
wait_for_home_idle
log "stopping lab126_gui to keep cvm down"
/sbin/stop lab126_gui
# stop returns before cvm has finished exiting.
i=0
while [ "$i" -lt 30 ] && pidof cvm >/dev/null 2>&1; do
    sleep 0.5
    i=$((i + 1))
done
if pidof cvm >/dev/null 2>&1; then
    log "cvm is still running after 15 s, restoring stock"
    rm -f "$FLAG"
    if ! initctl status lab126_gui 2>/dev/null | grep -q 'start/running'; then
        /sbin/start lab126_gui
    fi
    exit 1
fi
if ! start_gui_without_framework; then
    exit 1
fi

i=0
while [ "$i" -lt 60 ]; do
    if lipc-get-prop com.lab126.blanket load >/dev/null 2>&1 &&
        lipc-get-prop com.lab126.appmgrd activeApp >/dev/null 2>&1; then
        break
    fi
    sleep 0.5
    i=$((i + 1))
done
if [ "$i" -ge 60 ]; then
    log "blanket or appmgrd did not answer lipc; restoring stock"
    /sbin/stop lab126_gui
    rm -f "$FLAG"
    /sbin/start lab126_gui
    exit 1
fi

if pidof fakekaf >/dev/null 2>&1; then
    log "fakekaf already running"
else
    if pidof cvm >/dev/null 2>&1; then
        log "cvm is running, refusing to start fakekaf"
        /sbin/stop lab126_gui
        rm -f "$FLAG"
        /sbin/start lab126_gui
        exit 1
    fi
    if start_fakekaf; then
        log "fakekaf registered, pid $(cat "$FAKEKAF_PID")"
    else
        log "fakekaf did not register com.lab126.kaf; restoring stock"
        restore
        exit 1
    fi
fi

fakekaf_watch 9>&- &
FAKEKAF_WATCH_PID=$!

# chroot.conf binds /chroot only on framework_ready and skips the binds when
# volumd does not answer driveModeState.  Without the binds mesquite cannot
# open /var/local/certman and exits at launch.  The binds live in memory only.
if grep -q ' /chroot/var/local ' /proc/mounts; then
    log "/chroot is bound"
elif [ "$(lipc-get-prop com.lab126.volumd driveModeState 2>/dev/null)" = "0" ]; then
    (. /etc/upstart/functions && chroot_umount && chroot_mount) >/dev/null 2>&1
    if grep -q ' /chroot/var/local ' /proc/mounts; then
        log "bound /chroot for the browser"
    else
        log "binding /chroot failed"
    fi
else
    log "volumd driveModeState is not 0, leaving /chroot unbound"
fi

# The firmware's CA bundle (2021) lacks newer roots such as SSL.com TLS RSA
# Root CA 2022, so mesquite rejects those sites with "Unacceptable TLS
# certificate".  Bind the stock roots plus Mozilla's bundle over the file
# mesquite reads inside /chroot; the stock roots stay for Amazon's services.
CA_DST=/chroot/etc/ssl/certs/ca-certificates-prod.crt
if [ -f "$DIR/cacert.pem" ] && ! grep -q " $CA_DST " /proc/mounts; then
    cat /etc/ssl/certs/ca-certificates-prod.crt "$DIR/cacert.pem" >"$DIR/ca-bundle.crt" &&
        mount -o bind "$DIR/ca-bundle.crt" "$CA_DST" &&
        log "bound $DIR/ca-bundle.crt over $CA_DST"
fi

on_signal() {
    trap '' INT TERM HUP
    # A signal can land before the first child is recorded or between restarts.
    if [ -n "$KHOME_PID" ]; then
        kill -TERM "$KHOME_PID" >/dev/null 2>&1
        i=0
        # khome's cleanup stops the browser for up to 5 s and then kills it, so
        # the TERM needs room for that before the KILL lands.
        while [ "$i" -lt 150 ] && kill -0 "$KHOME_PID" 2>/dev/null; do
            sleep 0.1
            i=$((i + 1))
        done
        kill -KILL "$KHOME_PID" >/dev/null 2>&1
        wait "$KHOME_PID" 2>/dev/null
    fi
    restore
    exit 0
}

# khome exits 75 for a home-only restart and 76 for a Kindle reboot.  These are
# KHOME_EXIT_REBOOT_HOME and KHOME_EXIT_REBOOT_KINDLE in
# launcher/src/state.h.  Every other status keeps the old behaviour: restore
# the stock framework and leave with that status.
KHOME_PID=
# A run that ended without restore() may have left khome's KOReader marker.
rm -f /var/tmp/khome.koreader-killed
trap 'on_signal' INT TERM HUP
# fd 9 is closed so a sleep left behind by restore() cannot hold the lock.
keepawake 9>&- &
KEEPAWAKE_PID=$!
# khome only exits 0, 75 or 76 on purpose, so a status above 128 is a signal
# death.  Bring khome back rather than dropping the owner on the stock screen,
# but a launcher that dies at every start must still fall back to stock.
crashes=0
crash_window=0
while :; do
    "$DIR/bin/khome" >>"$LOG" 2>&1 &
    KHOME_PID=$!
    wait "$KHOME_PID"
    rc=$?
    if [ "$rc" -eq 75 ]; then
        log "khome exited for a home restart"
        continue
    fi
    if [ "$rc" -le 128 ]; then
        break
    fi
    now=$(date +%s)
    if [ $((now - crash_window)) -gt 300 ]; then
        crash_window=$now
        crashes=0
    fi
    crashes=$((crashes + 1))
    if [ "$crashes" -gt 3 ]; then
        log "khome died $crashes times within 5 min, giving up"
        break
    fi
    log "khome died with status $rc, restarting ($crashes/3)"
done

if [ "$rc" -eq 76 ]; then
    log "khome exited for a Kindle reboot"
    restore
    /sbin/reboot
    exit $?
fi

restore
exit "$rc"
