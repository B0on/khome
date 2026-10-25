# New device UX — three concepts (2026-10-23)

The device stops being a Kindle. It boots into our own software, shows no
Kindle or Amazon wording, and speaks English only. This file defines three
directions to mock up so the owner can pick one.

## Hardware and rendering limits (all concepts)

- Paperwhite 2 panel, 758 × 1024 at 212 dpi. Mockups are drawn at
  1072 × 1448 and scale by 0.707, as in `design/system/`.
- 16 grays, no shadows, blur, gradients or transparency. Shapes are
  rectangles, lines, circles, text (stb_truetype) and pre-rendered PNG icons.
  Fonts are the ones in `design/system/fonts/`.
- e-ink: a full refresh flashes, a partial refresh is cheap. Anything that
  changes every minute must sit in one small rectangle.
- Touch only, plus one power button. No home button, no audio.

## Shared across all concepts

- **Name.** Placeholder `FOLIO` until the owner picks a name. It replaces
  every `KINDLE` masthead and footer.
- **Boot.** khome starts at boot and paints its own splash at once. The
  stock UI is never seen in normal use.
- **Sleep.** Power button shows our sleep screen. Waking returns to exactly
  where you were.
- **Quick panel.** Pull down from the top edge on any screen, inside apps
  too: time, battery, Wi-Fi on/off, front light −/+, and a Home button. In the
  browser the same pull also shows the tabs, so one gesture means "controls"
  everywhere.
- **Settings stays small.** Wi-Fi, front light, newspaper sync, About.
  Restart and "start the stock system" move to a hidden Maintenance page
  (hold the version line for 3 s), because an owner who never wants the
  Kindle should not see a door back to it.
- **Content.** Newspaper (twice-daily editions from the home server), comics
  (KOReader), web (browser with favorites).

## A. Today — the device is a daily edition

Home is a cover page for today. A masthead with the name and date, the big
clock, then "This morning's paper" with the lead headline and two more (tap
one to open the paper at that article). Below it a "Continue" strip: the
comic you were reading with its page, and the last web page. A bottom dock
holds Paper, Comics, Web, Settings. It feels like picking up a fresh paper,
not unlocking a phone.

Screens: home, quick panel over home, sleep (the cover, with a "sleeping"
band at the bottom).

## B. Cards — a glanceable desk

Home is a grid of large cards and no app icons. Each card is a live item:
time and date, the latest paper (edition time and lead headline), the comic
in progress (title, volume, page, a progress bar), and one card per web
favorite. Tapping a card opens that exact item. Cards sit on an 8 px grid
with 3 px rules, so the screen reads like a printed set of index cards.

Screens: home, quick panel over home, sleep (time and date card enlarged,
the rest hidden).

## C. Ambient — a calm object first

Home is an ambient page: very large time, the date, one line of today's lead
headline, battery. Nothing else. Tap anywhere and a launcher sheet rises from
the bottom with four big rows (Paper, Comics, Web, Settings) and what is new
in each ("07:00 edition", "Vol. 3, p. 37"). The device looks like a clock or
a picture frame until you ask it for something.

Screens: home (ambient), launcher sheet open, sleep (the ambient page with
a "sleeping" mark; nearly identical on purpose).

## Sample content for mockups

Use made-up but plausible content. Paper headlines such as "City council
approves new tram line", "Rain expected through the weekend", "Local library
extends evening hours". Comic "Blue Harbor", Vol. 3, p. 37 of 180. Web
favorites "Hacker News", "Wikipedia", "Weather". Wi-Fi "Home-2G", battery
58 %, time 07:42, Thursday, 23 October.
