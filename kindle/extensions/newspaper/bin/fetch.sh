#!/bin/sh
# Download and verify a newer text edition when latest-text.json changes.

set -eu

FETCH_SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd -P)
. "$FETCH_SCRIPT_DIR/common.sh"

display_result=0
case "${1:-}" in
    ''|--quiet) ;;
    --display)
        display_result=1
        ;;
    *)
        np_log "usage: fetch.sh [--display|--quiet]"
        exit 2
        ;;
esac

finish() {
    msg=$1
    code=$2
    np_log "$msg"
    if [ "$display_result" -eq 1 ]; then
        np_show_message "$msg" || true
    fi
    exit "$code"
}

valid_sha256() {
    hash=$1
    [ "${#hash}" -eq 64 ] || return 1
    case "$hash" in
        *[!0-9a-f]*) return 1 ;;
    esac
    return 0
}

verify_sha256sums() {
    root=$1
    sums_file=$2
    while IFS= read -r line || [ -n "$line" ]; do
        [ -n "$line" ] || continue
        expected=${line%% *}
        rel=${line#* }
        [ "$rel" != "$line" ] || return 1
        rel=${rel# }
        rel=${rel#\*}
        valid_sha256 "$expected" || return 1
        actual=$(np_sha256_file "$root/$rel") || return 1
        [ "$actual" = "$expected" ] || return 1
    done < "$sums_file"
    return 0
}

# text-v1 bundles chain hashes: latest-text.json hashes SHA256SUMS, which
# hashes every other file in the bundle.
text_valid_path() {
    case "$1" in
        text-????????-????) ;;
        *) return 1 ;;
    esac
    case "${1#text-}" in
        *[!0-9-]*) return 1 ;;
    esac
    return 0
}

text_fetch() {
    text_changed=0
    if [ -L "$text_root/current" ] || { [ -e "$text_root/current" ] && [ ! -d "$text_root/current" ]; }; then
        text_error="current is not a plain directory"
        return 1
    fi
    if ! mkdir -p "$text_stage"; then
        text_error="could not create the staging directory"
        return 1
    fi
    if ! "$wget" -q -T 30 -O "$text_stage/latest-text.json" \
        "$(np_join_url "$base_url" "latest-text.json")"; then
        text_error="could not download latest-text.json"
        return 1
    fi

    remote_text_id=$(np_json_string id "$text_stage/latest-text.json")
    remote_text_sha=$(np_json_string sha256 "$text_stage/latest-text.json")
    remote_text_path=$(np_json_string path "$text_stage/latest-text.json")
    remote_text_manifest=$(np_json_string manifest_sha256 "$text_stage/latest-text.json")
    remote_text_format=$(np_json_string format "$text_stage/latest-text.json")

    if [ "$remote_text_format" != text-v1 ] || [ -z "$remote_text_id" ] || \
        [ "$remote_text_path" != "text-$remote_text_id" ] || \
        ! text_valid_path "$remote_text_path" || \
        ! valid_sha256 "$remote_text_sha" || ! valid_sha256 "$remote_text_manifest"; then
        text_error="latest-text.json is invalid"
        return 1
    fi

    text_local_id=
    text_local_sha=
    if [ -f "$text_root/latest-text.json" ] && [ ! -L "$text_root/latest-text.json" ]; then
        text_local_id=$(np_json_string id "$text_root/latest-text.json")
        text_local_sha=$(np_json_string sha256 "$text_root/latest-text.json")
    fi
    text_current_id=
    if [ -d "$text_root/current" ] && [ -f "$text_root/current/manifest.json" ]; then
        text_current_id=$(np_json_string id "$text_root/current/manifest.json")
    fi
    if [ "$remote_text_id" = "$text_local_id" ] && \
        [ "$remote_text_sha" = "$text_local_sha" ] && \
        [ "$remote_text_id" = "$text_current_id" ]; then
        return 0
    fi

    text_bundle=$text_stage/$remote_text_path
    if ! mkdir -p "$text_bundle"; then
        text_error="could not create the staging directory"
        return 1
    fi
    if ! "$wget" -q -T 60 -O "$text_bundle/SHA256SUMS" \
        "$(np_join_url "$base_url" "$remote_text_path/SHA256SUMS")"; then
        text_error="could not download SHA256SUMS"
        return 1
    fi
    if [ "$(np_sha256_file "$text_bundle/SHA256SUMS")" != "$remote_text_sha" ]; then
        text_error="SHA256SUMS does not match latest-text.json"
        return 1
    fi
    if ! "$wget" -q -T 60 -O "$text_bundle/manifest.json" \
        "$(np_join_url "$base_url" "$remote_text_path/manifest.json")"; then
        text_error="could not download manifest.json"
        return 1
    fi
    if [ "$(np_sha256_file "$text_bundle/manifest.json")" != "$remote_text_manifest" ]; then
        text_error="manifest.json does not match latest-text.json"
        return 1
    fi
    if [ "$(np_json_string id "$text_bundle/manifest.json")" != "$remote_text_id" ] || \
        [ "$(np_json_string format "$text_bundle/manifest.json")" != text-v1 ]; then
        text_error="manifest.json is invalid"
        return 1
    fi

    while IFS= read -r line || [ -n "$line" ]; do
        [ -n "$line" ] || continue
        rel=${line#* }
        rel=${rel# }
        rel=${rel#\*}
        case "$rel" in
            ''|/*|*..*)
                text_error="SHA256SUMS has an invalid path"
                return 1
                ;;
        esac
        if ! mkdir -p "$text_bundle/$(dirname "$rel")"; then
            text_error="could not create a directory for $rel"
            return 1
        fi
        if ! "$wget" -q -T 120 -O "$text_bundle/$rel" \
            "$(np_join_url "$base_url" "$remote_text_path/$rel")"; then
            text_error="could not download $rel"
            return 1
        fi
    done < "$text_bundle/SHA256SUMS"

    if ! verify_sha256sums "$text_bundle" "$text_bundle/SHA256SUMS"; then
        text_error="SHA256SUMS verification failed"
        return 1
    fi

    if [ -d "$text_root/previous" ]; then
        rm -rf "$text_root/previous" || {
            text_error="could not clear the previous edition"
            return 1
        }
    fi
    if [ -d "$text_root/current" ]; then
        mv "$text_root/current" "$text_root/previous" || {
            text_error="could not rotate the current edition"
            return 1
        }
    fi
    if ! mv "$text_bundle" "$text_root/current"; then
        if [ ! -e "$text_root/current" ] && [ -d "$text_root/previous" ]; then
            mv "$text_root/previous" "$text_root/current" 2>/dev/null || true
        fi
        text_error="could not install the text edition"
        return 1
    fi
    if ! cp "$text_stage/latest-text.json" "$text_root/latest-text.json"; then
        text_error="could not record latest-text.json"
        return 1
    fi
    text_changed=1
    return 0
}

fetch_text_edition() {
    text_root=$NP_DATA_ROOT/text
    text_stage=$text_root/.stage-$$
    text_error=
    text_status=0
    text_fetch || text_status=$?
    rm -rf "$text_stage" 2>/dev/null || true
    return "$text_status"
}

base_url=$(np_read_server_url) || finish "server.url is missing or invalid" 2
wget=$(np_find_wget) || finish "wget not found" 3

mkdir -p "$NP_DATA_ROOT"

np_wifi_enable

text_status=0
fetch_text_edition || text_status=$?

np_wifi_disable

if [ "$text_status" -ne 0 ]; then
    finish "text edition: $text_error" "$text_status"
fi

if [ "$text_changed" -eq 1 ]; then
    finish "Edition $remote_text_id installed" 0
fi
finish "Edition $remote_text_id is already current" 0
