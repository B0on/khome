#!/bin/sh
# Host test for fetch.sh: text-v1 install, idempotency, rotation, checksum refusal.
set -eu

REPO=$(CDPATH= cd "$(dirname "$0")/.." && pwd -P)
BIN=$REPO/kindle/extensions/newspaper/bin
SH=${SH:-/bin/sh}

tmp=$(mktemp -d)
editions=$tmp/editions
data=$tmp/data
state=$tmp/state
mkdir -p "$editions" "$data" "$state" "$tmp/bin"

server_pid=
cleanup() {
    if [ -n "$server_pid" ]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -rf "$tmp"
}
trap cleanup EXIT

fail=0
ok() {
    printf 'ok %s\n' "$1"
}
bad() {
    printf 'FAIL %s\n' "$1"
    fail=1
}
check() {
    msg=$1
    shift
    if "$@"; then ok "$msg"; else bad "$msg"; fi
}
check_false() {
    msg=$1
    shift
    if "$@"; then bad "$msg"; else ok "$msg"; fi
}
same() {
    msg=$1
    want=$2
    got=$3
    if [ "$want" = "$got" ]; then
        ok "$msg"
    else
        bad "$msg (want '$want', got '$got')"
    fi
}

json_field() {
    python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))[sys.argv[2]])' "$1" "$2"
}

cat > "$tmp/make.py" <<'PY'
import sys
from datetime import datetime, timedelta
from pathlib import Path
from zoneinfo import ZoneInfo

repo = Path(sys.argv[1])
editions = Path(sys.argv[2])
step = int(sys.argv[3])

sys.path.insert(0, str(repo / "scripts"))
import text_edition as te

JST = ZoneInfo("Asia/Tokyo")
now = datetime(2026, 10, 24, 18, 0, tzinfo=JST) + timedelta(minutes=step)

body = "これはテスト記事の本文です。" * 20
articles = []
for index in range(2):
    number = step * 10 + index
    candidate = te.Candidate(
        "時事通信",
        "jiji",
        "政治",
        "見出し%d" % number,
        "https://example.com/jiji/%d" % number,
        now - timedelta(hours=index),
        index,
    )
    paragraphs = [te.sanitize(body)]
    articles.append(
        te.Article(candidate, paragraphs, len("\n".join(paragraphs)), body[:50], 100 - index)
    )
summary = te.publish_edition(editions, articles, now)
print(str(summary["path"]))
PY

make_edition() {
    python3 "$tmp/make.py" "$REPO" "$editions" "$1"
}

cat > "$tmp/bin/wget" <<'SH'
#!/bin/sh
# Stand-in for the Kindle's busybox wget: -q -T <sec> -O <file> <url>.
out=
url=
while [ $# -gt 0 ]; do
    case "$1" in
        -O) out=$2; shift 2 ;;
        -T) shift 2 ;;
        -q) shift ;;
        *) url=$1; shift ;;
    esac
done
[ -n "$out" ] && [ -n "$url" ] || exit 2
curl -fsS --max-time 20 -o "$out" "$url"
SH
chmod +x "$tmp/bin/wget"

port=$(python3 - <<'PY'
import socket

sock = socket.socket()
sock.bind(("127.0.0.1", 0))
print(sock.getsockname()[1])
sock.close()
PY
)

python3 "$REPO/scripts/serve_editions.py" --root "$editions" --host 127.0.0.1 --port "$port" \
    >"$tmp/server.log" 2>&1 &
server_pid=$!
printf 'http://127.0.0.1:%s\n' "$port" > "$data/server.url"

python3 - "$port" <<'PY'
import socket
import sys
import time

port = int(sys.argv[1])
for _ in range(100):
    try:
        socket.create_connection(("127.0.0.1", port), 0.2).close()
        break
    except OSError:
        time.sleep(0.05)
else:
    raise SystemExit("server did not start")
PY

fetch_run() {
    log=$1
    code=0
    KINDLE_TIMES_ROOT=$data KINDLE_TIMES_STATE=$state NP_KEEP_WIFI=1 \
        PATH=$tmp/bin:$PATH "$SH" "$BIN/fetch.sh" --quiet >"$log.out" 2>"$log" || code=$?
    return "$code"
}

if fetch_run "$tmp/log0"; then
    bad "empty server: fetch exits nonzero"
else
    ok "empty server: fetch exits nonzero"
fi
check_false "empty server: no text current" test -d "$data/text/current"
check "empty server: failure names latest-text.json" grep -q 'text edition: could not download latest-text.json' "$tmp/log0"

text_a=$(make_edition 1)
text_a_id=${text_a#text-}

if fetch_run "$tmp/log1"; then
    ok "text edition fetch exits 0"
else
    bad "text edition fetch exits 0"
fi
same "text current id matches the served edition" "$text_a_id" "$(json_field "$data/text/current/manifest.json" id)"
same "text current format is text-v1" "text-v1" "$(json_field "$data/text/current/manifest.json" format)"
same "text pointer id matches" "$text_a_id" "$(json_field "$data/text/latest-text.json" id)"
same "text index lists both articles" "2" "$(wc -l < "$data/text/current/index.tsv" | tr -d ' ')"
check "text bundle has an article file" test -f "$data/text/current/articles/jiji-001.txt"
check "text bundle verifies with SHA256SUMS" sh -c 'cd "$1" && shasum -a 256 -c SHA256SUMS >/dev/null' sh "$data/text/current"
check_false "no text previous yet" test -d "$data/text/previous"
check "install is logged" grep -q "Edition $text_a_id installed" "$tmp/log1"

printf 'keep\n' > "$data/text/current/.idempotent"

if fetch_run "$tmp/log2"; then
    ok "second fetch exits 0"
else
    bad "second fetch exits 0"
fi
check "second fetch left text current alone" test -f "$data/text/current/.idempotent"
check_false "second fetch did not rotate text" test -d "$data/text/previous"
check "second fetch reports already current" grep -q "Edition $text_a_id is already current" "$tmp/log2"

text_b=$(make_edition 2)
text_b_id=${text_b#text-}

if fetch_run "$tmp/log3"; then
    ok "new text edition fetch exits 0"
else
    bad "new text edition fetch exits 0"
fi
same "text current advanced" "$text_b_id" "$(json_field "$data/text/current/manifest.json" id)"
same "text previous holds the old edition" "$text_a_id" "$(json_field "$data/text/previous/manifest.json" id)"
check "text rotation moved the old bundle to previous" test -f "$data/text/previous/.idempotent"
check_false "text current has no stale marker" test -f "$data/text/current/.idempotent"

text_c=$(make_edition 3)
article=$(ls "$editions/$text_c/articles" | head -n 1)
printf 'tampered\n' >> "$editions/$text_c/articles/$article"

if fetch_run "$tmp/log4"; then
    bad "tampered article fetch exits nonzero"
else
    ok "tampered article fetch exits nonzero"
fi
same "tampered article is refused" "$text_b_id" "$(json_field "$data/text/current/manifest.json" id)"
same "tampered article leaves text previous alone" "$text_a_id" "$(json_field "$data/text/previous/manifest.json" id)"
same "tampered article logs one line" "1" "$(grep -c 'text edition:' "$tmp/log4" || true)"
check "tampered article names the checksum failure" grep -q 'text edition: SHA256SUMS verification failed' "$tmp/log4"

text_d=$(make_edition 4)
python3 - "$editions/$text_d/SHA256SUMS" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
lines = path.read_text(encoding="utf-8").splitlines()
digest, rel = lines[0].split("  ", 1)
lines[0] = ("0" if digest[0] != "0" else "1") + digest[1:] + "  " + rel
path.write_text("\n".join(lines) + "\n", encoding="utf-8")
PY

if fetch_run "$tmp/log5"; then
    bad "broken SHA256SUMS fetch exits nonzero"
else
    ok "broken SHA256SUMS fetch exits nonzero"
fi
same "broken SHA256SUMS is refused" "$text_b_id" "$(json_field "$data/text/current/manifest.json" id)"
same "broken SHA256SUMS leaves text previous alone" "$text_a_id" "$(json_field "$data/text/previous/manifest.json" id)"
same "broken SHA256SUMS logs one line" "1" "$(grep -c 'text edition:' "$tmp/log5" || true)"
check "broken SHA256SUMS names the pointer mismatch" grep -q 'text edition: SHA256SUMS does not match latest-text.json' "$tmp/log5"

make_edition 5 >/dev/null
python3 - "$editions/latest-text.json" <<'PY'
import json
import sys

path = sys.argv[1]
data = json.load(open(path))
data["path"] = "../escape"
open(path, "w").write(json.dumps(data, indent=2) + "\n")
PY

if fetch_run "$tmp/log6"; then
    bad "path traversal pointer fetch exits nonzero"
else
    ok "path traversal pointer fetch exits nonzero"
fi
same "path traversal pointer is refused" "$text_b_id" "$(json_field "$data/text/current/manifest.json" id)"
check "path traversal pointer is named" grep -q 'text edition: latest-text.json is invalid' "$tmp/log6"
check_false "path traversal pointer wrote nothing outside text" test -e "$data/escape"

if [ "$fail" -ne 0 ]; then
    exit 1
fi
printf '%s\n' 'all text edition fetch checks passed'
