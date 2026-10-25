#!/usr/bin/env python3
"""Copy the README screenshots and build the hero strip.

Run from the repository root with the project virtualenv, which has Pillow:

    .venv/bin/python scripts/dev/make_readme_images.py

The seven screen images are copied from launcher/tests/golden/device/ so the
README always matches the reviewed device goldens.  hero.png lays home,
news-front, news-article and settings side by side on a white canvas, with a
thin gray frame around each 758x1024 screen.
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageOps

REPO = Path(__file__).resolve().parents[2]
SOURCE = REPO / "launcher" / "tests" / "golden" / "device"
OUT = REPO / "docs" / "images"

SCREENS = (
    "home",
    "news-front",
    "news-article",
    "news-list",
    "settings",
    "url-edit",
)

HERO_SCREENS = ("home", "news-front", "news-article", "settings")
HERO_WIDTH = 2000
HERO_MARGIN = 40
HERO_GAP = 24
HERO_BORDER = 2
SCREEN_W, SCREEN_H = 758, 1024
BORDER_VALUE = 170
PAPER = 255


def copy_screens() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for name in SCREENS:
        src = SOURCE / f"{name}.png"
        if not src.is_file():
            raise SystemExit(f"missing golden screen: {src}")
        (OUT / f"{name}.png").write_bytes(src.read_bytes())


def make_hero() -> None:
    count = len(HERO_SCREENS)
    cell_w = (HERO_WIDTH - 2 * HERO_MARGIN - (count - 1) * HERO_GAP) // count
    screen_w = cell_w - 2 * HERO_BORDER
    screen_h = round(screen_w * SCREEN_H / SCREEN_W)
    cell_h = screen_h + 2 * HERO_BORDER
    height = 2 * HERO_MARGIN + cell_h

    hero = Image.new("L", (HERO_WIDTH, height), PAPER)
    for index, name in enumerate(HERO_SCREENS):
        with Image.open(OUT / f"{name}.png") as opened:
            screen = opened.convert("L").resize(
                (screen_w, screen_h), Image.Resampling.LANCZOS
            )
        cell = ImageOps.expand(screen, border=HERO_BORDER, fill=BORDER_VALUE)
        x = HERO_MARGIN + index * (cell_w + HERO_GAP)
        hero.paste(cell, (x, HERO_MARGIN))

    # The panel shows 16 gray levels; store the resized strip as a 16-colour
    # palette so GitHub gets a small PNG instead of a full 8-bit grayscale one.
    hero = hero.quantize(colors=16, method=Image.Quantize.MEDIANCUT)
    hero.save(OUT / "hero.png", optimize=True)


def main() -> None:
    copy_screens()
    make_hero()
    print(f"wrote {len(SCREENS)} screens and hero.png to {OUT}")


if __name__ == "__main__":
    main()
