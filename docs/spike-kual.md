# KUAL thin client + LAN HTTP spike

Status: **PASS** (2026-09-10); superseded by the khome text edition (2026-10-24).

The original spike proved a KUAL extension, an offline-first FBInk PNG
reader, and checksummed LAN delivery. The PNG reader has been removed. The
device now uses `fetch.sh` for the text edition and
`launcher/src/kindletimes.c` to draw it. Only the fetch path below still
applies.

## Current device path

- KUAL → **The Kindle Times** → **Refresh edition** runs `refresh.sh` →
  `fetch.sh`.
- khome starts from `/mnt/us/emergency.sh` and draws
  `/mnt/us/newspaper/text/current`.
- `fetch.sh` verifies `latest-text.json`, `SHA256SUMS` and the bundle, then
  rotates `text/current`.
- Wi-Fi is on only around fetch; the device has no scheduler.
- Emergency Library: create `/mnt/us/khome/disable`.

## Server (<server-host> or Mac)

From the repo root:

```sh
python3 scripts/serve_editions.py --root out/editions --host 0.0.0.0 --port 8088
```

Kindle pointer URL:

```text
http://<server-ip>:8088/latest-text.json
```

Device config (`/mnt/us/newspaper/server.url`, no trailing slash):

```text
http://<server-ip>:8088
```

Stop with Ctrl+C.

Sync editions to the server host when generating there:

```sh
rsync -av out/editions/ <server-user>@<server-ip>:~/tools/kindle-newspaper/out/editions/
```

Server cron (the server host clock is UTC): `scripts/install_edition_cron.sh` installs
`07:00` / `18:00` JST jobs. Each run builds a text-v1 edition and **keeps the
last 2** `text-*` dirs. Logs: `logs/generate.log`.

## Remaining files

```
kindle/extensions/newspaper/
  config.xml, menu.json
  config/server.url.example
  bin/common.sh fetch.sh refresh.sh recover_boot.sh
scripts/serve_editions.py
scripts/seed_kindle.sh
docs/spike-kual.md
```

## Known limits

- The PNG reader, its touch binary, and the kiosk scripts are gone. khome
  owns layout, taps and page turns for the text edition.
- Checksums and `text/current` rotation live in `fetch.sh`; `tests/test_text_fetch.sh`
  covers the fetch path.
- `recover_boot.sh` restores the stock UI after a boot loop and never starts
  the newspaper.
