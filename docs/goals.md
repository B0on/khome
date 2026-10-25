# Kindle Newspaper Tablet — Goals & Priorities

## 1. Vision

Turn a jailbroken Kindle Paperwhite 2 into a personal newspaper tablet.
Twice a day the home server publishes a fresh two-column edition
(Japanese and English as separate papers). Pick up the device → the
paper is already full-screen. HOME / top-left returns to newspaper p01.
The stock Library is emergency-only (`/mnt/us/khome/disable`).

“Already on screen when I pick it up” is the v1 kiosk (spec §4.2). RTC
timed fetch is still not a v1 goal.

## 2. Core experience (what the user does)

1. Boot (or start khome) → last edition paints immediately
2. If Wi-Fi is up, a newer edition is pulled in the background, then shown
3. p01 shows the day’s most important mix (not eight equal boxes)
4. Flip right: each page is less important; tap a tappable headline for the full article
5. Toggle `[日本語 | English]` at any time (both already on device)
6. HOME / top-left → p01. `/mnt/us/khome/disable` → Library. Wi-Fi only for fetch

## 3. What it must do (v1 essentials)

| # | Requirement | Detail |
|---|---|---|
| 1 | Newspaper look & feel | Text edition drawn by khome at 758×1024; masthead, leads, rules, page numbers |
| 2 | Front page hierarchy | p01: mixed sources, one kicker + secondaries + briefs. Later pages are less important as you turn, not a topic index |
| 3 | Full article view | Tap article region → text reader pages. RSS feeds are catalogs; bodies come from HTML extraction. Only full bodies are published. |
| 4 | Language switching | Two source-sets, both built server-side; not machine translation |
| 5 | Automatic generation | Server cron 07:00 / 18:00 JST. Kindle fetches on app open. |
| 6 | Offline reading | Last good bundle always readable |
| 7 | Battery discipline | Wi-Fi only during fetch; no ping-loop; e-ink holds the image |
| 8 | Robustness | Isolated feeds, hash-verified swap, never publish a blank edition |
| 9 | Dedicated kiosk | Bridge autostart after `framework_ready`; HOME/exit stay on the paper; `/mnt/us/khome/disable` is the only Library path |

## 4. What it must NOT do (v1)

- RTC timed fetch
- Swipe gestures
- Article search / archive
- Images inside articles
- Asahi / Nikkei full text (headline-only / skip)
- Machine translation
- Push notifications
- Multiple user profiles
- On-device Python

## 5. Priority order (if we have to cut)

1. **Newspaper look & feel** — the differentiator
2. **Japanese edition that is actually readable** (NHK full text, not headlines)
3. Page turning + article tap
4. English edition
5. Twice-daily server generation
6. Battery / Wi-Fi discipline

## 6. Decisions (locked in spec 2026-09-10)

- Rendering: text edition (`scripts/text_edition.py`) drawn by khome, not server screenshots
- Gestures: tap zones, not swipe
- Fetch: on app open, not device timer
- Cover: show edition timestamp
- Device: shell + khome C launcher; server does all fetching and extraction

Prototype still decides: PW2 touch node, type specimen.
