# Task list

## Spec / research

- [x] Capture product goals in `docs/goals.md`
- [x] Assess feasibility from prior art (InkBrief, OpenReader, voyage-weather, RSS terms)
- [x] Brush up `docs/spec.md` (mode, pipeline, rendering, gestures, jailbreak path)
- [x] Front-page hierarchy: mixed sources, importance falls as you turn (spec §5.1)

## Next (implementation, not started)

- [x] Jailbreak + renameotabin + KUAL + FBInk on the PW4 (prerequisite)
- [x] Server skeleton at `~/tools/kindle-newspaper` (venv, text edition)
- [x] RSS fetch + trafilatura extraction for NHK / BBC (prove full text)
- [x] Text edition layout in khome (`kindletimes.c`)
- [x] Japanese **full article** text via static ITmedia/Zenn (NHK stays lead-only)
- [x] Edition packer (`manifest.json` + SHA-256, `latest-text.json`)
- [x] KUAL fetch client: wget, checksummed text-v1 install
- [x] Dedicated kiosk: khome autostart from `/mnt/us/emergency.sh`, HOME/exit → p01, `/mnt/us/khome/disable` for Library
- [x] Server cron 07:00 / 18:00 JST text edition generate; keep last 2 bundles

## Later

- [ ] RTC timed fetch (07:05 / 18:05 JST) while kiosk stays on
- [ ] English edition tree in the v1 bundle

## Done this round

- [x] Article tap → text reader pages (ITmedia/Zenn bodies)
