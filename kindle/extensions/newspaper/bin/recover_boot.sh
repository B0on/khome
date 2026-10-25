#!/bin/sh
# Clear the PW2 "needs repair" crash counter and restore the stock UI.
# Safe to run from upstart after lab126_gui. Never starts the newspaper.
set -u

rm -f \
    /var/local/system/.framework_reboots \
    /var/local/system/.framework_retries \
    /var/local/system/framework_reboots \
    /var/local/system/framework_retries \
    /var/local/upstart/lab126_gui.restarts \
    2>/dev/null || true

killall -CONT mesquite 2>/dev/null || true
killall -CONT cvm 2>/dev/null || true

if command -v lipc-set-prop >/dev/null 2>&1; then
    lipc-set-prop com.lab126.pillow disableEnablePillow enable >/dev/null 2>&1 || true
    lipc-set-prop com.lab126.powerd preventScreenSaver 0 >/dev/null 2>&1 || true
    lipc-set-prop com.lab126.appmgrd start app://com.lab126.booklet.home >/dev/null 2>&1 || true
fi

if [ -x /etc/init.d/framework ]; then
    /etc/init.d/framework restart >/dev/null 2>&1 || true
fi

mkdir -p /mnt/us/newspaper 2>/dev/null || true
printf '%s\n' unkiosk > /mnt/us/newspaper/unkiosk 2>/dev/null || true
rm -f /mnt/us/newspaper/kiosk 2>/dev/null || true

if command -v mntroot >/dev/null 2>&1; then
    mntroot rw >/dev/null 2>&1 || true
    rm -f /etc/upstart/kindle-times.conf
    mntroot ro >/dev/null 2>&1 || true
    if command -v initctl >/dev/null 2>&1; then
        initctl stop kindle-times >/dev/null 2>&1 || true
    fi
fi

exit 0
