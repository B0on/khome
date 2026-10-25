# Kindle Newspaper Tablet — Product Specification

Status: text edition 2026-10-24 (khome kiosk; PNG reader removed).
Device: Kindle Paperwhite 2 (Pinot/Wario), firmware 5.12.2.1.1.
Server: Ubuntu 24.04 host `<server-host>` at `<server-ip>` (`~/tools/kindle-newspaper`).

## 1. Overview

Turn a jailbroken Kindle Paperwhite 2 into a personal newspaper tablet.
A home Ubuntu server fetches curated news, extracts article bodies, and
builds a checksummed **text edition** (`text-v1`). The Kindle downloads the
bundle and khome lays out and draws each page locally with its own type and
rules.

Japanese and English are **two separate editions from different sources**,
not translations of the same articles. Editions are generated twice a day.
This Paperwhite is a **dedicated Kindle Times kiosk**: the paper is the
boot UI. The stock Library is reachable only via `/mnt/us/khome/disable`.

## 2. Feasibility verdict

**v1 is feasible** as a **dedicated kiosk** (khome draws the text edition;
`/mnt/us/khome/disable` returns to the Library). The stock Library remains
an emergency exit, not the daily path.

| Goal | Verdict | Condition |
|---|---|---|
| Newspaper look (text layout) | Feasible | khome draws the masthead, leads, rules and page numbers from `index.tsv`. |
| JA + EN editions | Feasible | Different source sets, both built ahead of time. |
| Page turn by tap | Feasible | Proven on-device (`OpenReader`, `kdashboard`, `InkBrief`). |
| Full article from tap | Feasible | `index.tsv` row + `articles/<id>.txt` body. RSS alone is not enough (see §6). |
| Language toggle | Feasible | Both languages already on device; no network. |
| Twice-daily generation | Feasible | Server cron. Independent of the Kindle. |
| Offline reading | Feasible | Last good bundle kept on device. |
| Battery discipline | Feasible | Wi-Fi on only during fetch; e-ink holds the image. |
| “Already on screen when I pick it up” | **v1 kiosk** | Jailbreak bridge starts khome after `framework_ready`; `preventScreenSaver`. No RTC fetch yet. |
| Asahi / Nikkei full articles | **Unreliable** | RSS is headlines; many bodies are paywalled. NHK is the JP backbone. |

Closest prior art for the **delivery path**: [InkBrief](https://github.com/Lzchyi/inkbrief)
(checksummed releases, last-good fallback, owned install paths). This device
copies that delivery discipline, then takes the weather-station step:
**replace the Kindle UI** so the paper is what you see after boot. Creating
`/mnt/us/khome/disable` restores the Library.

## 3. Hardware / infrastructure

| Item | Value |
|---|---|
| Device | Kindle Paperwhite 2 (Pinot/Wario, 758×1024, 212 ppi) |
| Serial | `(redacted)` |
| Firmware | 5.12.2.1.1 (372058 001) |
| Jailbreak path | **KindleBreak** (5.12.2.1.1 is in the supported table). WatchThis is for 5.12.2.2 / 5.13.4–5.14.2, not this build. |
| Post-jailbreak | Hotfix + MRPI + **coplate KUAL** + **renameotabin** (OTA will wipe the jailbreak) + USBNetwork + FBInk |
| Display | 6" E-Ink Carta, **212 ppi**, **758×1024**, 16 gray levels |
| Radio | 2.4 GHz Wi-Fi only. Server must be on a 2.4 GHz LAN SSID. |
| User storage | ~3.4 GB FAT32 (`/mnt/us`). Keep 2 editions; prune older. |
| Server | Ubuntu 24.04.3 on `<server-ip>` (`<server-host>`). Python 3.12 installed. Project dir: `~/tools/kindle-newspaper`. |

Do **not** put Python 2.7 on the Kindle. Device code is shell plus the
khome C launcher. All fetching and extraction stay on the server; the device
lays out and draws the text edition. NiLuJe Python 3 exists for this
firmware class, but a thin client is simpler and more robust.

## 4. Product decisions

### 4.1 Text rendering on the device (confirmed)

No existing Kindle viewer renders a true two-column newspaper:

- Native KF8/AZW3: `column-count` ignored on e-ink; two-column exists only
  on Scribe (KFX, landscape).
- KOReader (crengine): no column model; EPUB `column-count` ignored.
- inkread: 2-column on Supernote/Android, not Kindle.

**Decision:** the server publishes a `text-v1` bundle and khome draws each
page from `index.tsv` and the article bodies. Khome owns the newspaper
layout, type sizes and page breaks, so the device needs no server-rendered
page images.

### 4.2 Operating mode: dedicated kiosk (v2 enabled)

This Paperwhite is the dedicated-device unit. It is no longer a book
reader. Pick it up → the latest cached edition is on screen.

**This unit:** newspaper is the boot UI (the mode spec used to call v2).

1. The jailbreak bridge job (`/etc/upstart/bridge.conf`) runs
   `/mnt/us/emergency.sh` on every `framework_ready`. It waits for the stock
   home, then starts `/mnt/us/khome/khome.sh`.
2. khome draws `/mnt/us/newspaper/text/current` and paints the cached
   `text-v1` edition before any network I/O.
3. `fetch.sh` installs a newer bundle into `newspaper/text/current` when
   khome opens NEWS or the KUAL **Refresh edition** item runs.
4. `preventScreenSaver 1`; mesquite/cvm STOP while the paper is up.
5. HOME, top-left exit, and left-edge on p01 **return to newspaper p01**,
   not the Library.
6. `khome.sh` restores the framework on exit. The bridge starts khome once
   per boot.
7. Emergency Library only: create `/mnt/us/khome/disable` on USB or over
   SSH, then reboot; or delete `/mnt/us/emergency.sh`.
8. KUAL remains installed for **Refresh edition**.

Right-edge chrome is **always** next page. Article hit-boxes must not
steal the right 20% strip.

**Not required for this kiosk:** RTC alarm fetch (07:05 / 18:05 JST).
Fetch still happens when khome opens NEWS / Refresh.

### 4.3 Gestures: tap zones, not swipe (v1)

| Gesture | v1 | Why |
|---|---|---|
| Tap right 20% | Next page | Proven; matches newspaper muscle memory |
| Tap left 20% | Previous page | Same |
| Tap article rectangle | Open article pages | Layout hit-test (`kdashboard`, `InkBrief`) |
| Tap language control | Switch `ja` / `en` | Cover + a persistent header chip |
| Tap HOME / top-left | Newspaper p01 | Kiosk; Library only via `/mnt/us/khome/disable` |
| Swipe | Out of scope | Needs gesture detection; extra failure mode |

Touch path: khome reads `/dev/input/event*` (OpenReader uses `event1`;
discover via `/proc/bus/input/devices`, never hard-code `eventN`). Map
raw coords through the current framebuffer rotation, then hit-test the
rectangles of the page khome drew.

### 4.4 Edition format: text-v1, not a screenshot

`scripts/text_edition.py` writes the bundle on the server. It fetches the
feeds, extracts bodies with trafilatura, drops articles under 200 characters,
dedupes against the last edition, orders by section and score, then writes:

- `index.tsv`: one row per story, tab separated.
- `articles/<id>.txt`: the full body.
- `manifest.json`, `SHA256SUMS` and `latest-text.json`: identity and hashes.

**Load rule (`<server-host>` is a shared always-on box):** no browser, no Docker and no
headless Chromium run during an edition. Extraction is static HTTP plus
trafilatura. Do not keep a screenshot container idle.

### 4.5 Fetch trigger: on app open (v1)

Server cron is the source of truth (07:00 and 18:00 JST). The Kindle never
runs a scheduler in v1. Manual “Refresh” stays in the KUAL menu.

Cover page **must** show the edition timestamp (e.g. `2026-09-10 07:00 JST`)
so the user knows how stale the paper is.

## 5. User experience

### 5.1 Layout

Khome draws each page at 758×1024 from the edition's index and bodies:

- Masthead: title, date, edition stamp, source attribution
- Columns, rules between columns, page number
- Headlines + lead (2–4 sentences). Full body is **not** on the section page
- Japanese body type: Noto Serif CJK JP (or similar) at ≥ 9 pt equivalent
  at 300 ppi; headlines in a heavier gothic. High-contrast black on white.
  No gray-on-gray, no hairline rules thinner than 2 px.

**Visual language (v1):** NYT-print structure on 6″ e-ink. Blackletter
nameplate (**The Kindle Times**, original — not the NYT wordmark), ears,
Oxford rules, English dateline, left-aligned spanning lede, drop cap,
2-column lead, then a dense 3-column well. No cards or buttons; jumps
read `Continued on Page A4`. Specimen: `launcher/tests/golden/device/news-front.png`.

#### Front page (p01) — mixed sources, visual hierarchy

| Slot | Role | Visual |
|---|---|---|
| Kicker | Single most important story | Largest headline; may span both columns or the top ~40% |
| Secondaries | Next 2–3 stories, **different sources/beats if possible** | Medium headline + lead, one column each |
| Briefs | Remaining high-score items that still belong on p01 | Small headline, 1–2 lines |

Source mix on p01 is required when more than one source has items (NHK +
ITmedia/Zenn on JA; BBC + Guardian/Reuters on EN). Do not fill the front
with eight same-size NHK teasers.

The text edition publishes only items with a full body. NHK teaser-only
items are dropped rather than shown; ITmedia, Zenn and other full-body
sources are tappable.

#### Inside pages — importance falls as you turn

Later pages are **less important**, not a topic encyclopedia. A light beat
label (国際 / 経済 / テック) is allowed as a kicker line; page order is
**scored**, not “always p02 = international”.

| Page | What belongs there |
|---|---|
| p01 | Highest-score mix, any beat |
| p02 | Next tier (still “should read”) |
| p03 | Mid tier / remaining national + economy |
| p04 | Tech / AI, tool notes, longer Zenn |
| p05+ | Culture, misc, overflow briefs |

A 11k-char Zenn post almost never belongs on p01; it starts on p04+ as a
feature. An NHK disaster or ITmedia national scoop can beat a tech post
onto the front even if the tech article is longer.

Article bodies come from `articles/<id>.txt`; khome paginates them and caps
the reader pages.

### 5.2 Language

`[日本語 | English]` on the cover and as a small chip on every section
page. Switching reloads the same page index in the other tree. If a
language is missing (failed generation), the chip is disabled and the
cover states it.

### 5.3 Reading loop

Offline-first paint → optional version check → read → tap article →
back to the originating section page. Failed network never blanks the
screen.

## 6. News pipeline (this is the real work)

RSS is a **catalog**, not a newspaper. Official feeds give title + link
+ a short description. Asahi’s public RSS page states it provides
**headlines and links**. Full article view therefore requires a second
step: fetch the article URL and extract the main text.

### 6.1 Extractor

Server: Python 3 + `feedparser` + **trafilatura** (best-in-class main-text
extraction; used by Kindle-oriented aggregators). Per feed:

1. Fetch RSS with timeout (10s) and isolation (one feed must not kill the job).
2. Deduplicate by canonical URL / title fingerprint across feeds.
3. Fetch article HTML; extract title, date, body.
4. If extraction is empty, paywalled, or shorter than ~200 characters,
   drop the story; the text edition publishes only full bodies.
5. **Score and place by importance** (v1, no LLM). Higher score → earlier
   page and larger slot. Beat (国際/経済/テック) is a label, not the
   primary sort key. LLM front-page editor is v2.

   v1 score (higher = more important), then stable-sort:

   - **Source weight:** NHK 主要 > NHK 国際/経済 > ITmedia news > ITmedia AI+
     > BBC/Guardian/Reuters (EN edition) > Zenn / GitHub releases
   - **Feed rank:** earlier RSS item = higher (NHK cat0 order is editorial)
   - **Recency:** last 12h boosts; older than 36h decays
   - **Keyword boost (JA):** 死亡, 災害, 警報, 選挙, 首相, 速報, 事故
   - **Keyword boost (EN):** dead, war, election, crash, alert
   - **Length penalty on p01:** bodies > ~2k chars are features → inside
     pages unless source is NHK/ITmedia news and feed rank is 1–2
   - **Section leads:** each section's best article moves to the front of
     the order, then the rest follow by score (`order_articles` in
     `scripts/text_edition.py`). A large importance gap always wins over
     source mix.

   Fill p01 (kicker + secondaries + briefs) until the template is full,
   then p02, p03, … in remaining score order.

### 6.2 Japanese sources (v1)

Prefer **public, extractable** bodies. NHK is the **headline backbone**
(leads on section pages). Tappable **full articles** in Japanese come from
static HTML sources (ITmedia, Zenn), not from NHK ONE.

| Role | Source | Feed | Notes |
|---|---|---|---|
| Top / general | NHK 主要 | `https://news.web.nhk/n-data/conf/na/rss/cat0.xml` | Static HTML: **lead/teaser only (~100 chars)**. Do not Playwright-fetch NHK (server load). Dropped for lack of a full body. |
| International | NHK 国際 | `https://news.web.nhk/n-data/conf/na/rss/cat5.xml` | Same; teaser-only |
| Economy | NHK 経済 | `https://news.web.nhk/n-data/conf/na/rss/cat4.xml` | Same |
| Science / culture | NHK 科学・文化 | `https://news.web.nhk/n-data/conf/na/rss/cat7.xml` | Maps to Culture + Tech overflow |
| Tech / AI full text | ITmedia AI+ | `https://rss.itmedia.co.jp/rss/2.0/aiplus.xml` | Proven 2026-09-10: 3/3 `real_body`, 564–1662 cleaned chars, static GET |
| General JP full text | ITmedia news | `https://rss.itmedia.co.jp/rss/2.0/news_bursts.xml` | Proven: 3/3 `real_body` |
| Tech / AI full text | Zenn LLM | `https://zenn.dev/topics/llm/feed` | Proven: 3/3 `real_body`, up to 11k chars. khome paginates the body. |
| Tool tracking | GitHub Releases Atom | e.g. ollama `releases.atom` | Proven |

**Do not depend on Asahi or Nikkei for full text in v1.** Asahi RSS is
headline+link and their terms disallow excerpting body text for
republication and programmatic redistribution. Fine for a personal
headline list later; not a v1 blocker. Nikkei free RSS is the same class.

NHK RSS terms: personal use only, no commercial redistribution. This
project is a **single-user LAN reader**, not a public site. Still
attribute sources on the cover (“Sources: NHK, BBC, …”).

NHK moved RSS URLs around the 2025-10 broadcast-law change. Health-check
the official index (`https://www.nhk.or.jp/toppage/rss/index.html`) and
fail a feed whose item links 404 (the legacy `www3` trap).

### 6.3 English sources (v1)

| Role | Source | Feed |
|---|---|---|
| Top / world | BBC News | official RSS |
| Top / world | The Guardian | official RSS |
| Top / world | Reuters | official RSS |
| Tech / AI | OpenAI `https://openai.com/news/rss.xml` | verified 2026-09-10 |
| Tech / AI | DeepMind `https://deepmind.google/blog/feed/basic/` | verified |
| Tech / AI | Hugging Face `https://huggingface.co/blog/feed.xml` | verified |
| Tech / AI | TechCrunch AI, MIT TR AI, Mistral | verified |

Excluded: arXiv cs.AI (volume), Anthropic (no official RSS).

### 6.4 Packing rules

- `index.tsv` carries up to 30 stories, ordered by section lead then score.
- Every published story has a full body in `articles/<id>.txt`; articles
  under 200 characters are dropped, so there are no lead-only rows.
- khome paginates a body into as many reader pages as it needs; the reader
  caps the page count so one long post cannot exhaust memory.
- One failed feed must not fail the edition. Cold start with zero sources
  must **not** publish a blank paper (InkBrief rule); keep last good.

## 7. Technical architecture

```
Ubuntu cron (07:00 / 18:00 JST)
  → fetch sources (isolated per source)
  → extract bodies (trafilatura)
  → select, dedupe, order, assign ids
  → write text-v1 bundle (edition.tsv, index.tsv, articles/*.txt,
    manifest.json, SHA256SUMS)
  → publish latest-text.json
  → HTTP on LAN (keep last 2 editions)

Kindle khome
  → load text/current immediately (offline first)
  → refresh.sh → fetch.sh: GET latest-text.json (id + sha256 + manifest hash)
  → if new: GET bundle, verify SHA256SUMS, rotate current → previous
  → kt_paper_load: read edition.tsv, index.tsv and article bodies
  → draw pages with the C layout; tap a story to open it
  → a failed fetch leaves text/current and text/previous untouched
```

| Layer | Choice |
|---|---|
| Server OS | Ubuntu 24.04 on `<server-host>` |
| Fetch / extract | Python 3.12 + feedparser + trafilatura (`scripts/text_edition.py`) |
| Edition format | `text-v1`: TSV index + UTF-8 Article text |
| Delivery | HTTP on LAN, HTTP/1.1 (Kindle `wget` is old). No HTTPS required on LAN. |
| Kindle display | khome canvas (`kindletimes.c`) to `/dev/fb0` |
| Kindle input | khome touch handling |
| Kindle runtime | khome C launcher + `fetch.sh` (ash). No on-device Python. |
| Wi-Fi | `lipc-set-prop com.lab126.wifid enable 1/0` around fetch; sleep ~8s after enable |
| Storage | `/mnt/us/newspaper/` (owned path, like InkBrief). Do not write into `documents/` books. |

### 7.1 Edition bundle

```
text-20260910-0700/
├── edition.tsv        id and generated timestamp
├── index.tsv          one story per row
├── manifest.json      format, id, article count, file list
├── SHA256SUMS         hash of every file above and below
└── articles/
    ├── jiji-001.txt
    └── …
```

`index.tsv` has seven tab-separated columns:

```
id  section  source  published(HH:MM)  chars  title  lead
```

Each body file starts with the title, a `source<TAB>published<TAB>url`
line, a blank line, then the paragraphs. `manifest.json` carries
`format: "text-v1"`, the edition id and the exact file list:

```json
{
  "id": "20260910-0700",
  "generated_at": "2026-09-10T07:00:00+09:00",
  "format": "text-v1",
  "article_count": 1,
  "files": ["edition.tsv", "index.tsv", "manifest.json", "articles/jiji-001.txt"]
}
```

The sibling `latest-text.json` is the device pointer:

```json
{
  "id": "20260910-0700",
  "sha256": "<sha256 of SHA256SUMS>",
  "generated_at": "2026-09-10T07:00:00+09:00",
  "path": "text-20260910-0700",
  "format": "text-v1",
  "manifest_sha256": "<sha256 of manifest.json>"
}
```

Kindle download: compare `id` + `sha256` from `latest-text.json` with the
local copy. Unchanged → no download, no flash. A text bundle is a few
hundred KB of UTF-8, well under the old image bundle.

### 7.2 Device lifecycle (while khome is up)

1. khome loads `text/current` before any network access.
2. `fetch.sh` enables Wi-Fi, checks `latest-text.json`, verifies the bundle,
   then rotates `current` to `previous`. `NP_KEEP_WIFI=1` leaves the radio on.
3. A failed fetch logs one `text edition:` line and exits nonzero; the cached
   edition stays on screen and `previous` is not rotated.
4. khome keeps the device awake while the paper is open; the emergency path
   still restores the stock Library.

## 8. Non-functional requirements

- **Offline first:** cached edition always readable.
- **Battery:** Wi-Fi only during version check + download. No ping-loop
  keepalive in v1. E-ink holds pixels at zero extra power.
- **Readability:** 300 ppi, 16-level gray, Japanese serif/gothic pairing,
  minimum stroke width 2 px.
- **Robustness:** per-feed timeout; last good edition on server and device;
  hash verify before swap; never publish empty.
- **Cadence:** 07:00 / 18:00 JST, configurable.
- **Safety:** OTA blocked; installer writes only owned paths;
  `/mnt/us/khome/disable` restores the Library without removing the jailbreak.
- **Legal:** personal LAN use; source names on the cover; no public
  redistribution of extracted article text.

## 9. Out of scope (v1)

- RTC timed fetch (paper still fetches on start / Refresh)
- Swipe gestures
- Article search / archive
- Images inside articles
- Machine translation between JA and EN
- Asahi / Nikkei full-text
- Push notifications
- Multiple users
- On-device Python
- Using the device as a normal book reader while kiosk is on

## 10. Prior art (surveyed 2026-09)

### 10.1 Closest to this product

| Project | What to steal |
|---|---|
| [InkBrief](https://github.com/Lzchyi/inkbrief) (2026) | 1072×1448 PNG pages, checksummed releases, last-good fallback, tap row, owned install paths (we kiosk instead of HOME-to-Library) |
| [OpenReader](https://github.com/terpinedream/OpenReader) | C touch reader on `/dev/input`, calibration, SIGSTOP framework, exit to Kindle OS |
| [kdashboard](https://github.com/thecodedose/kdashboard) | Tap coordinate → rectangle → action |
| kindle-voyage-weather | Wi-Fi on/off, never kill `powerd`, GC16, rtcwake (**v2 only**) |
| saturngod/kindle-family-dashboard | Version URL so unchanged images are not re-downloaded |
| KindleEar / OughtGather | RSS + full-text extract → Kindle, but they emit EPUB, not newspaper pages |

### 10.2 Mainstream news-to-Kindle (not our UX)

KindleEar, Calibre Fetch News, kindlefy, kindle-daily-digest, RSSPub —
email or OPDS of **single-column ebooks**. Useful for feed lists and
extraction lessons; they cannot deliver the newspaper flip.

## 11. What would make it better (ordered)

1. **Masthead with weather + battery** — device sends `?batt=NN` on fetch
   (kindle4-weather-display trick); next edition draws a real battery pip.
2. **Overnight RTC fetch** on the existing kiosk (not a mode toggle).
3. **LLM front-page editor** (v2) — pick 5 top stories, write 2-sentence
   leads in the paper’s voice. Deterministic ranking first.
4. **Photos** — one masthead image, Floyd–Steinberg dithered to 16 gray.
5. **Continuation lines** (“story continues p.4”) like a real broadsheet.
6. **Section color bars** as distinct hatch/weight, since there is no color.

## 12. Open questions (remaining)

Resolved in this draft: edition format (text-v1), gestures (tap zones),
fetch trigger (on open), cover timestamp (yes), device runtime (no
Python).

Resolved by the text edition work:

- `text_edition.py` publishes edition.tsv, index.tsv, article bodies,
  manifest.json and SHA256SUMS; `latest-text.json` carries the pointer.
- **NHK static trafilatura is teaser-only** (~100 cleaned chars), so NHK
  stories are not published without a full body.
- ITmedia and Zenn static fetch yield full bodies; khome paginates them.
- A failed fetch leaves `text/current` and `text/previous` untouched.

Still open, decide in prototyping:

- Japanese full article: **ITmedia + Zenn static fetch** (proven). Do not
  Playwright NHK.
- Strip trafilatura metadata junk from BBC first lines before leads
- Touch event node on this PW2 (`event1` vs other)
- Default language (ja vs last-used)

## 13. Jailbreak notes (prerequisite, not the product)

1. Confirm firmware still `5.12.2.1.1` before any experiment.
2. KindleBreak → hotfix → MRPI → coplate KUAL.
3. **renameotabin immediately.** Wi-Fi without this can OTA-kill the JB.
4. USBNetwork + FBInk from NiLuJe snapshots (coplate / PW2-and-up).
5. Airplane mode except during newspaper fetch is the safe default.

Jailbreak is a one-time physical risk. The newspaper installer must refuse
to run if FBInk / KUAL are missing, and must never touch `documents/` books.
