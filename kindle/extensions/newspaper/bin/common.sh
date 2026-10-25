#!/bin/sh
# Shared helpers for The Kindle Times KUAL extension (BusyBox ash).

NP_DATA_ROOT=${KINDLE_TIMES_ROOT:-/mnt/us/newspaper}
NP_LOG_FILE=$NP_DATA_ROOT/reader.log

np_log() {
    msg=$1
    printf '%s\n' "kindle-times: $msg" >&2
    if [ -n "$NP_LOG_FILE" ]; then
        printf '%s\n' "kindle-times: $msg" >> "$NP_LOG_FILE" 2>/dev/null || true
    fi
    if command -v logger >/dev/null 2>&1; then
        logger -t kindle-times -- "$msg" 2>/dev/null || true
    fi
}

np_find_command() {
    name=$1
    for candidate in \
        "/mnt/us/usbnet/bin/$name" \
        "/mnt/us/libkh/bin/$name" \
        "/var/local/kmc/bin/$name"
    do
        if [ -x "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    command -v "$name" 2>/dev/null || return 1
}

np_find_fbink() {
    np_find_command fbink
}

np_find_wget() {
    np_find_command wget
}

np_find_sha256() {
    tool=$(np_find_command sha256sum 2>/dev/null || true)
    if [ -n "$tool" ]; then
        printf '%s\n' "$tool"
        return 0
    fi
    if [ -x /mnt/us/usbnet/bin/busybox ]; then
        printf '%s\n' /mnt/us/usbnet/bin/busybox
        return 0
    fi
    return 1
}

np_sha256_file() {
    path=$1
    tool=$(np_find_sha256) || return 1
    case "$tool" in
        */busybox)
            "$tool" sha256sum "$path" | awk '{print $1}'
            ;;
        *)
            "$tool" "$path" | awk '{print $1}'
            ;;
    esac
}

np_show_message() {
    message=$1
    fbink=$(np_find_fbink 2>/dev/null) || {
        np_log "$message"
        return 1
    }
    "$fbink" -q -c -f -m -M -S 2 "$message" >/dev/null 2>&1 || np_log "$message"
}

np_read_server_url() {
    url_file=$NP_DATA_ROOT/server.url
    if [ ! -f "$url_file" ] || [ -L "$url_file" ] || [ ! -r "$url_file" ]; then
        return 1
    fi
    IFS= read -r url < "$url_file" || return 1
    case "$url" in
        http://*) ;;
        *) return 1 ;;
    esac
    while [ "${url%/}" != "$url" ]; do
        url=${url%/}
    done
    printf '%s\n' "$url"
}

np_json_string() {
    key=$1
    file=$2
    tr -d '\r\n' < "$file" |
        sed -n "s/.*\"$key\"[[:space:]]*:[[:space:]]*\"\([^\"]*\)\".*/\1/p"
}

np_join_url() {
    base=$1
    part=$2
    base=${base%/}
    case "$part" in
        /*) printf '%s%s\n' "$base" "$part" ;;
        *) printf '%s/%s\n' "$base" "$part" ;;
    esac
}

np_wifi_enable() {
    if [ "${NP_KEEP_WIFI:-0}" = 1 ]; then
        return 0
    fi
    if command -v lipc-set-prop >/dev/null 2>&1; then
        lipc-set-prop com.lab126.wifid enable 1 >/dev/null 2>&1 || true
        sleep 8
    fi
}

np_wifi_disable() {
    if [ "${NP_KEEP_WIFI:-0}" = 1 ]; then
        return 0
    fi
    if command -v lipc-set-prop >/dev/null 2>&1; then
        lipc-set-prop com.lab126.wifid enable 0 >/dev/null 2>&1 || true
    fi
}
