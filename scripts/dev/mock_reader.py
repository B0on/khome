#!/usr/bin/env python3
"""Draw Kindle Times reader mocks at the Paperwhite 2 panel size (758x1024)."""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import sys
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ModuleNotFoundError:
    _venv = Path(__file__).resolve().parents[2] / ".venv" / "bin" / "python"
    if _venv.exists() and Path(sys.executable) != _venv:
        os.execv(str(_venv), [str(_venv), str(Path(__file__).resolve()), *sys.argv[1:]])
    sys.exit("Pillow is required. Run with .venv/bin/python or install Pillow.")

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "private" / "docs-mock-news-v2"
FONT_DIR = ROOT / "design" / "system" / "fonts"

W, H = 758, 1024
M = 32
FOOTER_RULE = 946
DATE = "2026年10月24日"
DATE_LONG = "2026年10月24日 土曜日"

INK = 0
GRAY = 72
MID = 112
DIM = 152
HAIR = 196
READ = 178
PALE = 226
PAPER = 255

LEAD_FORBIDDEN = frozenset(
    "、。，．・：；？！ー）」』】〕〉》］｝”’"
    "ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ々゛゜"
)
TAIL_FORBIDDEN = frozenset("（「『【〔〈《［｛“‘")
HANGING = frozenset("、。，．")
OIDASHI = LEAD_FORBIDDEN.difference(HANGING)
# Digits bind to these suffixes so a line never breaks between 5323 and 万件.
NUMBER_SUFFIX = frozenset("万億千百十件人円年月日時分秒点間半末回個台本冊割倍歳度階位")

FONT_FILES = {
    "serif": "NotoSerifJP-Regular.ttf",
    "serif_b": "NotoSerifJP-Bold.ttf",
    "sans": "NotoSansJP-Medium.ttf",
}


@lru_cache(maxsize=None)
def font(kind: str, size: int):
    return ImageFont.truetype(str(FONT_DIR / FONT_FILES[kind]), size)


# ---------------------------------------------------------------------------
# Line breaking. The tables and the shape of wrap_paragraph are meant to port
# to C almost line for line.
# ---------------------------------------------------------------------------


def _tokens(text: str) -> list[str]:
    """Split text into Japanese characters and runs of ASCII alphanumerics."""
    out: list[str] = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch.isascii() and ch.isalnum():
            j = i + 1
            while j < len(text) and text[j].isascii() and text[j].isalnum():
                j += 1
            if text[j - 1].isdigit():
                k = j
                while k < len(text) and text[k] in NUMBER_SUFFIX:
                    k += 1
                    while k < len(text) and text[k].isascii() and text[k].isdigit():
                        k += 1
                j = k
            out.append(text[i:j])
            i = j
        else:
            out.append(ch)
            i += 1
    return out


def _kinsoku(lines: list[str]) -> list[str]:
    """Move opening brackets down and hang single forbidden leading marks."""
    for i in range(len(lines) - 1):
        while len(lines[i]) > 1 and lines[i][-1] in TAIL_FORBIDDEN:
            mark = lines[i][-1]
            lines[i] = lines[i][:-1]
            lines[i + 1] = mark + lines[i + 1]
    for i in range(1, len(lines)):
        run = 0
        while run < len(lines[i]) and lines[i][run] in LEAD_FORBIDDEN:
            run += 1
        if run == 1:
            lines[i - 1] += lines[i][0]
            lines[i] = lines[i][1:]
        elif run > 1:
            while lines[i] and lines[i][0] in LEAD_FORBIDDEN and len(lines[i - 1]) > 1:
                mark = lines[i - 1][-1]
                lines[i - 1] = lines[i - 1][:-1]
                lines[i] = mark + lines[i]
            if lines[i] and lines[i][0] in LEAD_FORBIDDEN:
                lines[i - 1] += lines[i]
                lines[i] = ""
    for i in range(len(lines) - 1):
        while len(lines[i]) > 1 and lines[i][-1] in TAIL_FORBIDDEN:
            mark = lines[i][-1]
            lines[i] = lines[i][:-1]
            lines[i + 1] = mark + lines[i + 1]
    return [line for line in lines if line]


def wrap_paragraph(text: str, width: float, measure) -> list[str]:
    """Greedy wrap with hanging punctuation and word-level ASCII breaks."""
    lines: list[str] = []
    tokens = _tokens(text)
    cur: list[str] = []
    used = 0.0
    i = 0
    while i < len(tokens):
        tok = tokens[i]
        tok_w = measure(tok)
        if cur and used + tok_w > width:
            lines.append("".join(cur).rstrip())
            cur = []
            used = 0.0
            if tok == " ":
                i += 1
            continue
        if not cur and tok == " ":
            i += 1
            continue
        if not cur and tok_w > width:
            for ch in tok:
                ch_w = measure(ch)
                if cur and used + ch_w > width:
                    lines.append("".join(cur))
                    cur = []
                    used = 0.0
                cur.append(ch)
                used += ch_w
            i += 1
            continue
        cur.append(tok)
        used += tok_w
        i += 1
    if cur:
        lines.append("".join(cur).rstrip())
    return _kinsoku(lines)


def paginate(lines: list[str], lines_per_page: int) -> list[list[str]]:
    return [lines[i : i + lines_per_page] for i in range(0, len(lines), lines_per_page)] or [[]]


def wrap_paragraph_ab(text: str, width: float, measure) -> list[str]:
    """Fusion layout: only punctuation hangs; ー, closers and small kana are pushed out."""
    lines: list[str] = []
    tokens = _tokens(text)
    cur: list[str] = []
    used = 0.0
    i = 0
    while i < len(tokens):
        tok = tokens[i]
        tok_w = measure(tok)
        if cur and used + tok_w > width:
            if cur[-1] in TAIL_FORBIDDEN and len(cur) > 1:
                carry = [cur.pop()]
                used -= measure(carry[0])
                lines.append("".join(cur).rstrip())
                cur = carry
                used = measure(carry[0])
                continue
            if tok in HANGING:
                cur.append(tok)
                used += tok_w
                i += 1
                continue
            if tok in OIDASHI and len(cur) > 1:
                carry: list[str] = []
                while len(cur) > 1:
                    carry.insert(0, cur.pop())
                    if carry[0] not in LEAD_FORBIDDEN:
                        break
                while len(cur) > 1 and cur[-1] in TAIL_FORBIDDEN:
                    carry.insert(0, cur.pop())
                lines.append("".join(cur).rstrip())
                cur = carry
                used = sum(measure(part) for part in cur)
                continue
            lines.append("".join(cur).rstrip())
            cur = []
            used = 0.0
            if tok == " ":
                i += 1
            continue
        if not cur and tok == " ":
            i += 1
            continue
        if not cur and tok_w > width:
            for ch in tok:
                ch_w = measure(ch)
                if cur and used + ch_w > width:
                    lines.append("".join(cur))
                    cur = []
                    used = 0.0
                cur.append(ch)
                used += ch_w
            i += 1
            continue
        cur.append(tok)
        used += tok_w
        i += 1
    if cur:
        lines.append("".join(cur).rstrip())
    for i in range(len(lines) - 1):
        while len(lines[i]) > 1 and lines[i][-1] in TAIL_FORBIDDEN:
            mark = lines[i][-1]
            lines[i] = lines[i][:-1]
            lines[i + 1] = mark + lines[i + 1]
    return lines


def justify_gap(line: str, face, width: float):
    if len(line) < 2 or line[-1] in HANGING or line[-1] in TAIL_FORBIDDEN:
        return None
    widths = [face.getlength(ch) for ch in line]
    extra = width - sum(widths)
    if extra <= 0.5:
        return None
    return extra / (len(line) - 1)


def draw_justified(draw, x: int, y: int, line: str, face, fill: int, width: float):
    gap = justify_gap(line, face, width)
    if gap is None:
        draw.text((x, y), line, font=face, fill=fill)
        return
    cx = float(x)
    for ch in line:
        draw.text((cx, y), ch, font=face, fill=fill)
        cx += face.getlength(ch) + gap


def wrap_limited_ab(text: str, face, width: float, max_lines: int) -> list[str]:
    lines = wrap_paragraph_ab(text, width, face.getlength)
    if len(lines) >= 2 and 0 < len(lines[-1]) < 3:
        while len(lines[-1]) < 3 and len(lines[-2]) > 1:
            ch = lines[-2][-1]
            if ch in LEAD_FORBIDDEN:
                block = lines[-2][-2:]
                if len(block) < 2 or block[0] in LEAD_FORBIDDEN or face.getlength(block + lines[-1]) > width:
                    break
                lines[-2] = lines[-2][:-2]
                lines[-1] = block + lines[-1]
                continue
            if face.getlength(ch + lines[-1]) > width or ch in TAIL_FORBIDDEN:
                break
            lines[-2] = lines[-2][:-1]
            lines[-1] = ch + lines[-1]
    if lines:
        lines = [lines[0]] + [line.lstrip("　 ") for line in lines[1:]]
    if len(lines) <= max_lines:
        return lines
    lines = lines[:max_lines]
    tail = lines[-1]
    n = len(tail)
    while n > 0 and face.getlength(tail[:n].rstrip("　 ") + "…") > width:
        n -= 1
    lines[-1] = tail[:n].rstrip("　 ") + "…"
    return lines


def _is_japanese(text: str) -> bool:
    for ch in text:
        code = ord(ch)
        if 0x3040 <= code <= 0x30FF or 0x3400 <= code <= 0x9FFF:
            return True
    return False


def indent_paragraph(text: str) -> str:
    if _is_japanese(text) and not text.startswith("　"):
        return "　" + text
    return text


def first_sentences(text: str, count: int) -> str:
    parts = re.split(r"(?<=[。！？])", text.strip())
    return "".join(parts[:count]).strip()


# ---------------------------------------------------------------------------
# Text and rule helpers
# ---------------------------------------------------------------------------


def canvas():
    img = Image.new("L", (W, H), PAPER)
    return img, ImageDraw.Draw(img)


def hline(draw, y: int, x0: int = M, x1: int = W - M, weight: int = 1, fill: int = INK):
    if weight <= 1:
        draw.line((x0, y, x1 - 1, y), fill=fill)
    else:
        draw.rectangle((x0, y, x1 - 1, y + weight - 1), fill=fill)


def block(draw, x: int, y: int, lines: list[str], face, fill: int = INK, leading: int = 0):
    leading = leading or int(face.size * 1.4)
    for i, line in enumerate(lines):
        draw.text((x, y + i * leading), line, font=face, fill=fill)
    return y + max(0, len(lines) - 1) * leading + face.size


def ellipsize(text: str, face, width: float) -> str:
    if face.getlength(text) <= width:
        return text
    n = len(text)
    while n > 0 and face.getlength(text[:n].rstrip("　 ") + "…") > width:
        n -= 1
    return text[:n].rstrip("　 ") + "…"


def wrap_limited(text: str, face, width: float, max_lines: int) -> list[str]:
    lines = wrap_paragraph(text, width, face.getlength)
    if len(lines) <= max_lines:
        return lines
    lines = lines[:max_lines]
    tail = lines[-1]
    n = len(tail)
    while n > 0 and face.getlength(tail[:n].rstrip("　 ") + "…") > width:
        n -= 1
    lines[-1] = tail[:n].rstrip("　 ") + "…"
    return lines


def tracked(draw, x: int, y: int, text: str, face, fill: int = INK, spacing: int = 1):
    for ch in text:
        draw.text((x, y), ch, font=face, fill=fill)
        x += face.getlength(ch) + spacing


def tracked_width(text: str, face, spacing: int = 1) -> float:
    return sum(face.getlength(ch) for ch in text) + spacing * (len(text) - 1)


def tracked_right(draw, x: int, y: int, text: str, face, fill: int = INK, spacing: int = 1):
    tracked(draw, int(x - tracked_width(text, face, spacing)), y, text, face, fill, spacing)


# ---------------------------------------------------------------------------
# Story data
# ---------------------------------------------------------------------------


@dataclass
class Item:
    title: str
    section: str
    time: str
    source: str
    minutes: int
    summary: str
    article: dict


FICTIONAL_SOURCE = "Example News"


def fake_paragraphs(title: str, section: str, count: int = 10) -> list[str]:
    """Deterministic copy for the mocks. No real article text is used."""
    paragraphs = [
        f"これは{section}面の見出しと本文の組み方を確かめるための架空の記事である。"
        "実在の出来事、人物、団体とは関係がない。",
    ]
    for i in range(1, count):
        paragraphs.append(
            f"第{i}段落。架空市の担当者は「これは公開用のダミー記事であり、"
            "実在の出来事や人物とは関係ない」と説明した。紙面の行数と余白を"
            "確かめるため、同じ長さの文を並べている。"
        )
    return paragraphs


def fake_article(title: str, section: str) -> dict:
    paragraphs = fake_paragraphs(title, section)
    body = "\n".join(paragraphs)
    return {
        "title": title,
        "feed": FICTIONAL_SOURCE,
        "body": body,
        "lead_preview": paragraphs[0],
        "char_count": len(body),
    }


def _norm(text: str) -> str:
    return re.sub(r"\s+", "", text)


def article_paragraphs(article: dict) -> list[str]:
    body = article.get("body") or article.get("lead_preview") or ""
    title = _norm(article["title"])
    out: list[str] = []
    for raw in body.split("\n"):
        text = raw.strip()
        if not text:
            continue
        if _norm(text).startswith(title):
            continue
        if text.startswith("Copyright") or "All Rights Reserved" in text:
            continue
        out.append(text)
    return out


def summary_for(article: dict) -> str:
    lead = (article.get("lead_preview") or "").strip()
    if not lead or _norm(lead).startswith(_norm(article["title"])[:18]):
        paragraphs = article_paragraphs(article)
        lead = first_sentences("".join(paragraphs), 1)
    return lead.replace("\u2026", "")


def minutes_for(article: dict) -> int:
    chars = article.get("char_count") or article.get("cleaned_char_count") or len(article.get("body") or "")
    return max(1, round(chars / 400))


def item(title: str, section: str, time: str) -> Item:
    article = fake_article(title, section)
    return Item(
        title=title,
        section=section,
        time=time,
        source=FICTIONAL_SOURCE,
        minutes=minutes_for(article),
        summary=summary_for(article),
        article=article,
    )


@dataclass
class ProbeItem:
    id: str
    section: str
    source: str
    published: str
    chars: int
    title: str
    lead: str
    paragraphs: list[str]

    @property
    def minutes(self) -> int:
        return max(1, math.ceil(self.chars / 500))


_FULLWIDTH_ALNUM = {}
for _start, _end, _offset in ((0xFF10, 0xFF19, 0xFEE0), (0xFF21, 0xFF3A, 0xFEE0), (0xFF41, 0xFF5A, 0xFEE0)):
    for _code in range(_start, _end + 1):
        _FULLWIDTH_ALNUM[_code] = _code - _offset


def halfwidth_alnum(text: str) -> str:
    return text.translate(_FULLWIDTH_ALNUM)


FICTIONAL_PROBE = [
    ("社会", "架空市で住民票システム障害、復旧作業続く"),
    ("政治", "架空市議会、減税条例案を可決"),
    ("国際", "架空国との通商交渉が難航"),
    ("スポーツ", "架空選手、世界選手権で金メダル"),
    ("経済", "政府、架空燃料補助の見直しを発表"),
    ("経済", "架空自動車大手、生産体制を縮小"),
    ("テック", "架空AI企業、新モデルを公開"),
    ("経済", "架空通貨、対ドルで上昇"),
    ("社会", "架空チケット詐欺、注意呼びかけ"),
    ("テック", "架空大規模言語モデル、仕組みを解説"),
    ("テック", "架空サイバー攻撃ツール、解析進む"),
    ("テック", "架空ベータ版、無償提供を開始"),
    ("テック", "架空コードレビュー、合格率を調査"),
    ("テック", "架空の軌跡、研究チームが発表"),
    ("テック", "架空モデル内部、観測手法を提案"),
]


def load_probe():
    items: list[ProbeItem] = []
    for number, (section, title) in enumerate(FICTIONAL_PROBE, 1):
        paragraphs = fake_paragraphs(title, section, 14)
        body = "\n".join(paragraphs)
        items.append(
            ProbeItem(
                id=f"ex-{number:03d}",
                section=section,
                source=FICTIONAL_SOURCE,
                published=f"0{5 - number % 3}:{number * 3 % 60:02d}",
                chars=len(body),
                title=title,
                lead=paragraphs[0],
                paragraphs=paragraphs,
            )
        )
    return items


# ---------------------------------------------------------------------------
# Screens
# ---------------------------------------------------------------------------


def brand_line(draw, small: bool = False):
    face = font("serif_b", 22 if small else 24)
    hline(draw, 24)
    draw.text((M, 30), "Kindle Times", font=face, fill=INK)
    draw.text((W - M, 36), DATE, font=font("sans", 14), fill=GRAY, anchor="ra")
    hline(draw, 58)


def draw_a_front(top: Item, secondary: list[Item]):
    img, d = canvas()
    hline(d, 24)
    d.text((W // 2, 34), "Kindle Times", font=font("serif_b", 68), fill=INK, anchor="ma")
    d.text((M, 116), DATE_LONG, font=font("sans", 17), fill=GRAY)
    d.text((W // 2, 116), "朝刊", font=font("sans", 17), fill=INK, anchor="ma")
    d.text((W - M, 118), "第 1 面", font=font("sans", 14), fill=GRAY, anchor="ra")
    hline(d, 144, weight=3)
    hline(d, 150)

    tracked(d, M, 172, top.section, font("sans", 15), INK, 2)
    d.text((W - M, 174), "TOP STORY", font=font("sans", 12), fill=GRAY, anchor="ra")

    head = font("serif_b", 44)
    head_lines = wrap_limited(top.title, head, W - 2 * M, 3)
    y = block(d, M, 194, head_lines, head, INK, 54) + 14

    lead_face = font("serif", 22)
    lead_text = first_sentences("".join(article_paragraphs(top.article)), 2)
    lead_lines = wrap_limited(lead_text, lead_face, W - 2 * M, 4)
    y = block(d, M, y, lead_lines, lead_face, INK, 36) + 8
    d.text((M, y), f"{top.source}  {top.time}", font=font("sans", 15), fill=GRAY)
    d.text((W - M, y), f"約{top.minutes}分", font=font("sans", 15), fill=GRAY, anchor="ra")
    y += 26
    hline(d, y)
    y += 18

    for story in secondary:
        hline(d, y - 9, fill=HAIR)
        story_face = font("serif_b", 27)
        lines = wrap_limited(story.title, story_face, W - 2 * M, 2)
        y = block(d, M, y, lines, story_face, INK, 33) + 6
        d.text((M, y), f"{story.source}  {story.time}", font=font("sans", 14), fill=GRAY)
        y += 21

    hline(d, 958)
    d.text((M, 968), "Kindle Times", font=font("sans", 12), fill=GRAY)
    d.text((W - M, 966), "1", font=font("sans", 15), fill=GRAY, anchor="ra")
    return img


def draw_a_section(lead: Item, stories: list[Item]):
    img, d = canvas()
    brand_line(d, small=True)
    d.text((M, 62), "経済", font=font("serif_b", 62), fill=INK)
    tracked_right(d, W - M, 82, "ECONOMY", font("sans", 12), GRAY, 3)
    d.text((W - M, 104), "第 2 面", font=font("sans", 13), fill=GRAY, anchor="ra")
    hline(d, 144, weight=3)
    hline(d, 150)

    tracked(d, M, 168, "ECONOMY / LEAD", font("sans", 12), GRAY, 2)
    head = font("serif_b", 36)
    head_lines = wrap_limited(lead.title, head, W - 2 * M, 2)
    y = block(d, M, 190, head_lines, head, INK, 44) + 10

    lead_face = font("serif", 21)
    lead_lines = wrap_limited(lead.summary, lead_face, W - 2 * M, 3)
    y = block(d, M, y, lead_lines, lead_face, INK, 33) + 8
    d.text((M, y), f"{lead.source}  {lead.time}", font=font("sans", 14), fill=GRAY)
    d.text((W - M, y), f"約{lead.minutes}分", font=font("sans", 14), fill=GRAY, anchor="ra")
    y += 24
    hline(d, y)
    y += 16

    for story in stories:
        hline(d, y - 8, fill=HAIR)
        story_face = font("serif_b", 26)
        lines = wrap_limited(story.title, story_face, W - 2 * M, 2)
        y = block(d, M, y, lines, story_face, INK, 32) + 6
        d.text((M, y), f"{story.source}  {story.time}", font=font("sans", 13), fill=GRAY)
        y += 21

    hline(d, 958)
    d.text((M, 968), "Kindle Times", font=font("sans", 12), fill=GRAY)
    d.text((W - M, 966), "2", font=font("sans", 15), fill=GRAY, anchor="ra")
    return img


def draw_article(
    item: Item,
    section: str,
    capacity_cap: int,
    next_link: bool = False,
    progress=None,
    head_size: int = 42,
    head_leading: int = 50,
):
    img, d = canvas()
    hline(d, 24)
    d.text((M, 30), "Kindle Times", font=font("serif_b", 21), fill=INK)
    d.text((W - M, 34), section, font=font("sans", 14), fill=GRAY, anchor="ra")
    hline(d, 58)

    y = 70
    if progress is not None:
        d.text((W - M, 68), f"{round(progress * 100)}% 読了", font=font("sans", 12), fill=GRAY, anchor="ra")
        bar = int((W - 2 * M) * progress)
        d.rectangle((M, 88, W - M - 1, 90), fill=PALE)
        d.rectangle((M, 88, M + bar, 90), fill=INK)
        y = 102

    tracked(d, M, y, f"{section} / {item.source}", font("sans", 12), GRAY, 1)
    y += 22
    head = font("serif_b", head_size)
    head_lines = wrap_limited(item.title, head, W - 2 * M, 3)
    y = block(d, M, y, head_lines, head, INK, head_leading) + 10
    meta = f"{DATE} {item.time} ・ 約{item.minutes}分"
    d.text((M, y), meta, font=font("sans", 13), fill=GRAY)
    y += 24
    hline(d, y, weight=2 if head_size >= 40 else 1)
    y += 16

    d.text((W - M, y - 14), "前ページから", font=font("sans", 12), fill=MID, anchor="ra")
    body = font("serif", 30)
    lines: list[str] = []
    for para in article_paragraphs(item.article):
        lines.extend(wrap_paragraph(indent_paragraph(para), W - 2 * M, body.getlength))
    lines_per_page = min(capacity_cap, (FOOTER_RULE - 16 - y) // 51)
    if lines_per_page < 1:
        sys.exit("article body has no room")
    pages = paginate(lines, lines_per_page)
    if len(pages) != 4:
        sys.exit(f"{item.title}: {len(pages)} pages, expected 4 (capacity {lines_per_page})")
    block(d, M, y, pages[1], body, INK, 51)

    hline(d, FOOTER_RULE)
    d.text((M, 958), "2 / 4", font=font("sans", 17), fill=INK)
    if next_link:
        d.text((W - M, 956), "次の記事 ›", font=font("sans", 18), fill=INK, anchor="ra")
    else:
        d.text((W - M, 960), "Kindle Times", font=font("sans", 12), fill=GRAY, anchor="ra")
    return img


def draw_b_item(d, story: Item, number: int, top: int, read: bool = False):
    ink = DIM if read else INK
    meta = READ if read else GRAY
    d.text((M, top + 14), f"{number:02d}", font=font("sans", 24), fill=ink)
    d.text((80, top), f"{story.section} ・ {story.source}", font=font("sans", 12), fill=meta)
    if read:
        d.text((W - M, top), "既読", font=font("sans", 12), fill=meta, anchor="ra")
    head = font("serif_b", 27)
    lines = wrap_limited(story.title, head, W - M - 80, 2)
    block(d, 80, top + 20, lines, head, ink, 34)
    text = ellipsize(story.summary, font("serif", 19), W - M - 80)
    d.text((80, top + 96), text, font=font("serif", 19), fill=meta)
    hline(d, top + 138, x0=80, x1=W - M, fill=PALE if read else HAIR)


def draw_b_front(stories: list[Item], start: int, title_size: int, subtitle: str, page: int, read_count: int = 0):
    img, d = canvas()
    brand_line(d)
    last = start + len(stories) - 1
    d.text((M, 72), "今朝の10本", font=font("serif_b", title_size), fill=INK)
    d.text((W - M, 78), f"{start:02d}—{last:02d}" if start > 1 else "01—05", font=font("sans", 24), fill=GRAY, anchor="ra")
    tracked(d, M, 128 if start == 1 else 124, "BRIEF / 2026.10.24", font("sans", 11), MID, 2)
    d.text((M, 148 if start == 1 else 142), subtitle, font=font("serif", 18), fill=GRAY)
    hline(d, 180 if start == 1 else 166)

    y = 200 if start == 1 else 180
    for offset, story in enumerate(stories):
        draw_b_item(d, story, start + offset, y, read=offset < read_count)
        y += 148

    hline(d, 958)
    d.text((M, 968), "Kindle Times", font=font("sans", 12), fill=GRAY)
    d.text((W - M, 966), f"{page} / 2", font=font("sans", 15), fill=GRAY, anchor="ra")
    return img


C_TABS = ["総合", "政治", "経済", "国際", "社会", "テック", "スポーツ"]


def draw_tabs(d, selected: str, y: int = 66):
    face = font("sans", 18)
    x = M
    for tab in C_TABS:
        width = int(face.getlength(tab)) + 22
        if tab == selected:
            d.rectangle((x, y, x + width - 1, y + 33), fill=INK)
        d.text((x + 11, y + 7), tab, font=face, fill=PAPER if tab == selected else GRAY)
        x += width + 4


def draw_c_index(selected: str, stories: list[Item], page: int):
    img, d = canvas()
    brand_line(d)
    draw_tabs(d, selected)
    hline(d, 104, weight=2)
    d.text((M, 116), selected, font=font("serif_b", 28), fill=INK)
    d.text((W - M, 122), f"{len(stories)}本", font=font("sans", 13), fill=GRAY, anchor="ra")
    hline(d, 154)

    y = 164
    for number, story in enumerate(stories, 1):
        d.text((M, y + 4), f"{number:02d}", font=font("sans", 14), fill=MID)
        head = font("serif_b", 25)
        d.text((68, y), ellipsize(story.title, head, W - M - 68 - 22), font=head, fill=INK)
        d.text(
            (68, y + 32),
            f"{story.source} ・ {story.time} ・ 約{story.minutes}分",
            font=font("sans", 14),
            fill=GRAY,
        )
        d.text((W - M, y + 12), "›", font=font("sans", 16), fill=MID, anchor="ra")
        hline(d, y + 60, fill=HAIR)
        y += 66

    hline(d, 958)
    d.text((M, 968), "Kindle Times", font=font("sans", 12), fill=GRAY)
    d.text((W - M, 966), f"{page} / 2", font=font("sans", 15), fill=GRAY, anchor="ra")
    return img


# ---------------------------------------------------------------------------
# AB fusion screens
# ---------------------------------------------------------------------------


def draw_ab_row(
    d,
    story: ProbeItem,
    number: int,
    top: int,
    height: int,
    head_size: int,
    head_lead: int,
    summary_size: int,
    read: bool = False,
):
    ink = DIM if read else INK
    meta = READ if read else GRAY
    d.text((M, top + 10), f"{number:02d}", font=font("sans", 22), fill=ink)
    d.text(
        (78, top),
        f"{story.section} ・ {story.source} ・ {story.published}",
        font=font("sans", 12),
        fill=meta,
    )
    if read:
        d.text((W - M, top), "既読", font=font("sans", 12), fill=meta, anchor="ra")
    head = font("serif_b", head_size)
    lines = wrap_limited_ab(story.title, head, W - M - 78 - 18, 2)
    block(d, 78, top + 18, lines, head, ink, head_lead)
    summary_face = font("serif", summary_size)
    d.text((78, top + 18 + 2 * head_lead + 8), ellipsize(story.lead, summary_face, W - M - 78 - 18), font=summary_face, fill=meta)
    d.text((W - M, top + 24), "›", font=font("sans", 15), fill=MID, anchor="ra")
    hline(d, top + height - 10, x0=78, x1=W - M, fill=PALE if read else HAIR)


def draw_ab1_front(stories: list[ProbeItem]):
    top, rows = stories[0], stories[1:4]
    img, d = canvas()
    hline(d, 24)
    d.text((W // 2, 34), "Kindle Times", font=font("serif_b", 68), fill=INK, anchor="ma")
    d.text((M, 116), DATE_LONG, font=font("sans", 17), fill=GRAY)
    d.text((W // 2, 116), "朝刊", font=font("sans", 17), fill=INK, anchor="ma")
    d.text((W - M, 118), "第 1 面", font=font("sans", 14), fill=GRAY, anchor="ra")
    hline(d, 144, weight=3)
    hline(d, 150)

    d.text((M, 166), "01", font=font("sans", 21), fill=INK)
    d.text((64, 170), top.section, font=font("sans", 13), fill=GRAY)
    d.text((W - M, 172), "TOP STORY", font=font("sans", 12), fill=GRAY, anchor="ra")
    head = font("serif_b", 40)
    head_lines = wrap_limited_ab(top.title, head, W - 2 * M, 4)
    y = block(d, M, 192, head_lines, head, INK, 50) + 10
    lead = font("serif", 22)
    lead_lines = wrap_limited_ab(top.lead, lead, W - 2 * M, 3)
    y = block(d, M, y, lead_lines, lead, INK, 36) + 8
    d.text((M, y), f"{top.source}  {top.published}", font=font("sans", 14), fill=GRAY)
    d.text((W - M, y), f"約{top.minutes}分", font=font("sans", 14), fill=GRAY, anchor="ra")
    y += 24
    hline(d, y)
    y += 16

    for index, story in enumerate(rows, 2):
        draw_ab_row(d, story, index, y, 124, 27, 32, 18)
        y += 124

    hline(d, 958)
    d.text((M, 968), "Kindle Times", font=font("sans", 12), fill=GRAY)
    d.text((W - M, 966), "1 / 2", font=font("sans", 15), fill=GRAY, anchor="ra")
    return img


def draw_ab1_list(stories: list[ProbeItem]):
    img, d = canvas()
    hline(d, 24)
    d.text((M, 30), "Kindle Times", font=font("serif_b", 22), fill=INK)
    d.text((W // 2, 34), "朝刊", font=font("sans", 12), fill=GRAY, anchor="ma")
    d.text((W - M, 34), DATE, font=font("sans", 13), fill=GRAY, anchor="ra")
    hline(d, 58)
    d.text((M, 68), "05—10", font=font("serif_b", 28), fill=INK)
    d.text((W - M, 76), "既読 2本", font=font("sans", 13), fill=GRAY, anchor="ra")
    hline(d, 104)

    y = 116
    for offset, story in enumerate(stories, 5):
        draw_ab_row(d, story, offset, y, 136, 27, 32, 18, read=offset < 7)
        y += 136

    hline(d, 958)
    d.text((M, 968), "Kindle Times", font=font("sans", 12), fill=GRAY)
    d.text((W - M, 966), "2 / 2", font=font("sans", 15), fill=GRAY, anchor="ra")
    return img


def draw_ab2_front(stories: list[ProbeItem]):
    top, rows = stories[0], stories[1:5]
    img, d = canvas()
    hline(d, 24)
    d.text((M, 30), "Kindle Times", font=font("serif_b", 34), fill=INK)
    d.text((W - M, 38), DATE, font=font("sans", 14), fill=GRAY, anchor="ra")
    hline(d, 68)
    d.text((M, 80), "今朝の10本", font=font("serif_b", 40), fill=INK)
    d.text((W - M, 84), "01—05", font=font("sans", 20), fill=GRAY, anchor="ra")
    tracked(d, M, 128, "BRIEF / 2026.10.24", font("sans", 11), MID, 2)
    hline(d, 148)

    d.text((M, 158), "01", font=font("sans", 21), fill=INK)
    d.text(
        (64, 162),
        f"{top.section} ・ {top.source} ・ {top.published}",
        font=font("sans", 13),
        fill=GRAY,
    )
    d.text((W - M, 162), f"約{top.minutes}分", font=font("sans", 13), fill=GRAY, anchor="ra")
    head = font("serif_b", 38)
    head_lines = wrap_limited_ab(top.title, head, W - 2 * M, 4)
    y = block(d, M, 182, head_lines, head, INK, 45) + 10
    lead = font("serif", 19)
    lead_lines = wrap_limited_ab(top.lead, lead, W - 2 * M, 2)
    y = block(d, M, y, lead_lines, lead, INK, 29) + 8
    hline(d, y)
    y += 14

    for index, story in enumerate(rows, 2):
        draw_ab_row(d, story, index, y, 124, 27, 32, 19)
        y += 124

    hline(d, 958)
    d.text((M, 968), "Kindle Times", font=font("sans", 12), fill=GRAY)
    d.text((W - M, 966), "1 / 2", font=font("sans", 15), fill=GRAY, anchor="ra")
    return img


def draw_ab2_list(stories: list[ProbeItem]):
    img, d = canvas()
    brand_line(d)
    d.text((M, 72), "今朝の10本", font=font("serif_b", 36), fill=INK)
    d.text((W - M, 78), "06—10", font=font("sans", 22), fill=GRAY, anchor="ra")
    tracked(d, M, 122, "BRIEF / 2026.10.24", font("sans", 11), MID, 2)
    hline(d, 152)

    y = 164
    for offset, story in enumerate(stories, 6):
        draw_ab_row(d, story, offset, y, 148, 27, 33, 19, read=offset < 8)
        y += 148

    hline(d, 958)
    d.text((M, 968), "Kindle Times", font=font("sans", 12), fill=GRAY)
    d.text((W - M, 966), "2 / 2", font=font("sans", 15), fill=GRAY, anchor="ra")
    return img


def ab_article_lines(story: ProbeItem, face, width: float):
    lines: list[tuple[str, bool]] = []
    for para in story.paragraphs:
        wrapped = wrap_paragraph_ab(indent_paragraph(para), width, face.getlength)
        for index, line in enumerate(wrapped):
            if index > 0:
                line = line.lstrip("　 ")
            justify = index < len(wrapped) - 1 and line[-1] not in HANGING
            lines.append((line, justify))
    return lines


def draw_ab_article(story: ProbeItem, page: int, total: int, next_story=None):
    img, d = canvas()
    hline(d, 24)
    source_face = font("sans", 13)
    head_face = font("serif_b", 15)
    lead_in = f"{story.source} ・ "
    d.text((M, 34), lead_in, font=source_face, fill=GRAY)
    avail = W - M - (M + source_face.getlength(lead_in))
    d.text((M + source_face.getlength(lead_in), 32), ellipsize(story.title, head_face, avail), font=head_face, fill=INK)
    hline(d, 62)

    body = font("serif", 30)
    width = W - 2 * M
    lines = ab_article_lines(story, body, width)
    capacity = min(15, (FOOTER_RULE - 16 - 80) // 51)
    pages = paginate(lines, capacity)
    if len(pages) != total:
        sys.exit(f"{story.id}: {len(pages)} pages, expected {total} (capacity {capacity})")
    current = pages[page - 1]
    for index, (line, justify) in enumerate(current):
        y = 80 + index * 51
        if justify:
            draw_justified(d, M, y, line, body, INK, width)
        else:
            d.text((M, y), line, font=body, fill=INK)

    if page == total and next_story is not None:
        bottom = 80 + max(0, len(current) - 1) * 51 + 30
        box = bottom + 36
        if box + 70 > FOOTER_RULE:
            sys.exit(f"{story.id}: no room for next story on final page")
        hline(d, box)
        d.text((M, box + 12), "次の記事 ›", font=font("sans", 18), fill=INK)
        next_face = font("serif_b", 19)
        d.text((M, box + 42), ellipsize(next_story.title, next_face, width), font=next_face, fill=INK)

    hline(d, FOOTER_RULE)
    d.text((M, 958), f"{page} / {total}", font=font("sans", 17), fill=INK)
    d.text((W - M, 960), "Kindle Times", font=font("sans", 12), fill=GRAY, anchor="ra")
    return img


# ---------------------------------------------------------------------------
# N screens: three newspaper directions for the same probe data
# ---------------------------------------------------------------------------


N_BOTTOM = 942  # footer rule sits at 958, so keep 16px clear above it
N1_INDEX_TOP = 740


def n_scratch():
    return ImageDraw.Draw(Image.new("L", (1, 1)))


def group_by_section(stories: list[ProbeItem]) -> list[tuple[str, list[ProbeItem]]]:
    order: list[str] = []
    buckets: dict[str, list[ProbeItem]] = {}
    for story in stories:
        if story.section not in buckets:
            buckets[story.section] = []
            order.append(story.section)
        buckets[story.section].append(story)
    return [(section, buckets[section]) for section in order]


def n_head(d, x: int, y: int, title: str, size: int, max_lines: int, width: float, leading: int | None = None):
    face = font("serif_b", size)
    lines = wrap_limited_ab(title, face, width, max_lines)
    leading = leading or round(size * 1.32)
    block(d, x, y, lines, face, INK, leading)
    return y + max(0, len(lines) - 1) * leading + size, len(lines)


def n_summary(d, x: int, y: int, text: str, size: int, max_lines: int, width: float, leading: int | None = None, fill: int = INK):
    face = font("serif", size)
    lines = wrap_limited_ab(text, face, width, max_lines)
    leading = leading or round(size * 1.48)
    for index, line in enumerate(lines):
        line_y = y + index * leading
        if index < len(lines) - 1 and line[-1] not in HANGING:
            draw_justified(d, x, line_y, line, face, fill, width)
        else:
            d.text((x, line_y), line, font=face, fill=fill)
    return y + max(0, len(lines) - 1) * leading + size, len(lines)


def n_meta(d, x: int, y: int, story: ProbeItem, size: int = 11, fill: int = GRAY):
    d.text(
        (x, y),
        f"{story.source}　{story.published}　・　約{story.minutes}分",
        font=font("sans", size),
        fill=fill,
    )


def n_footer(d, page: int, total: int = 2, rule: bool = True):
    if rule:
        hline(d, 958)
    d.text((M, 968), "Kindle Times", font=font("sans", 12), fill=GRAY)
    d.text((W - M, 966), f"{page} / {total}", font=font("sans", 14), fill=GRAY, anchor="ra")


# --- N1: general paper -------------------------------------------------------


def draw_n1_masthead(d):
    hline(d, 20)
    d.text((W // 2, 30), "Kindle Times", font=font("serif_b", 62), fill=INK, anchor="ma")
    d.text((M, 124), DATE_LONG, font=font("sans", 13), fill=GRAY)
    d.text((W // 2, 124), "朝刊", font=font("sans", 14), fill=INK, anchor="ma")
    d.text((W - M, 124), "第 12,874 号", font=font("sans", 13), fill=GRAY, anchor="ra")
    hline(d, 150, weight=3)
    hline(d, 156)


def draw_n1_brief(d, story: ProbeItem, number: int, y: int, summary_lines: int = 2):
    d.text(
        (M, y),
        f"{number:02d}　{story.section}　・　{story.source}　・　{story.published}",
        font=font("sans", 11),
        fill=GRAY,
    )
    y += 19
    y, _ = n_head(d, M, y, story.title, 24, 2, W - 2 * M, 31)
    y += 7
    y, _ = n_summary(d, M, y, story.lead, 16, summary_lines, W - 2 * M, 23)
    return y + 18


def draw_n1_index(d, stories: list[ProbeItem], top: int):
    bottom = 932
    d.rectangle((M, top, W - M - 1, bottom), outline=INK)
    d.text((M + 14, top + 10), "きょうの紙面", font=font("serif_b", 19), fill=INK)
    d.text((W - M - 14, top + 14), "2 面", font=font("sans", 11), fill=GRAY, anchor="ra")
    y = top + 40
    for story in stories[:5]:
        d.text((M + 14, y + 3), story.section, font=font("sans", 10), fill=GRAY)
        title_face = font("serif_b", 14)
        d.text((M + 58, y), ellipsize(story.title, title_face, W - M - 58 - 48), font=title_face, fill=INK)
        d.text((W - M - 14, y + 2), "2", font=font("sans", 10), fill=GRAY, anchor="ra")
        y += 24
    d.text((M + 14, bottom - 22), "社会・政治・国際・経済・スポーツ", font=font("sans", 10), fill=GRAY)


def draw_n1_front(stories: list[ProbeItem], index_stories: list[ProbeItem]):
    top_story, secondary = stories[0], stories[1:3]
    img, d = canvas()
    draw_n1_masthead(d)

    y = 172
    d.text((M, y), f"01　{top_story.section}　・　{top_story.source}", font=font("sans", 12), fill=GRAY)
    y += 22
    y, _ = n_head(d, M, y, top_story.title, 36, 3, W - 2 * M, 45)
    y += 12
    y, _ = n_summary(d, M, y, top_story.lead, 21, 3, W - 2 * M, 31, fill=INK)
    y += 10
    n_meta(d, M, y, top_story, 12)
    y += 22
    hline(d, y)
    y += 22

    overflow: list[ProbeItem] = []
    for offset, story in enumerate(secondary):
        number = offset + 2
        if draw_n1_brief(n_scratch(), story, number, y) > N1_INDEX_TOP - 12:
            overflow = secondary[offset:]
            break
        y = draw_n1_brief(d, story, number, y)
        hline(d, y)
        y += 18

    draw_n1_index(d, index_stories, max(y + 12, N1_INDEX_TOP))
    n_footer(d, 1)
    return img, overflow, 932


def draw_n1_list(stories: list[ProbeItem]):
    img, d = canvas()
    hline(d, 20)
    d.text((M, 28), "Kindle Times", font=font("serif_b", 28), fill=INK)
    d.text((W // 2, 36), "朝刊", font=font("sans", 12), fill=GRAY, anchor="ma")
    d.text((W - M, 36), DATE, font=font("sans", 12), fill=GRAY, anchor="ra")
    hline(d, 70)
    d.text((M, 84), "第 2 面　総合", font=font("serif_b", 30), fill=INK)
    d.text((W - M, 94), "05—09", font=font("sans", 17), fill=GRAY, anchor="ra")
    hline(d, 132)

    y = 148
    overflow: list[ProbeItem] = []
    for offset, story in enumerate(stories):
        if draw_n1_brief(n_scratch(), story, 5 + offset, y, summary_lines=3) > N_BOTTOM:
            overflow = stories[offset:]
            break
        y = draw_n1_brief(d, story, 5 + offset, y, summary_lines=3)
        hline(d, y)
        y += 18

    n_footer(d, 2)
    return img, overflow, y


# --- N2: no rules ------------------------------------------------------------


def draw_n2_masthead(d):
    d.text((W // 2, 40), "Kindle Times", font=font("serif_b", 56), fill=INK, anchor="ma")
    d.text((M, 116), "朝刊", font=font("sans", 11), fill=GRAY)
    d.text((W // 2, 112), DATE_LONG, font=font("sans", 13), fill=GRAY, anchor="ma")
    d.text((W - M, 116), "第 1 面", font=font("sans", 11), fill=GRAY, anchor="ra")


def draw_n2_story(d, story: ProbeItem, y: int, head_size: int, summary_size: int, head_lines: int, summary_lines: int, gap_after: int):
    d.text((M, y), story.section, font=font("sans", 11), fill=GRAY)
    y += 18
    y, _ = n_head(d, M, y, story.title, head_size, head_lines, W - 2 * M, round(head_size * 1.3))
    y += round(head_size * 0.36)
    y, _ = n_summary(d, M, y, story.lead, summary_size, summary_lines, W - 2 * M, round(summary_size * 1.45))
    y += round(summary_size * 0.4)
    n_meta(d, M, y, story, 10)
    return y + 14 + gap_after


def draw_n2_front(stories: list[ProbeItem]):
    top_story, secondary = stories[0], stories[1:4]
    img, d = canvas()
    draw_n2_masthead(d)

    y = 154
    d.text((M, y), top_story.section, font=font("sans", 12), fill=GRAY)
    y += 22
    y, _ = n_head(d, M, y, top_story.title, 36, 3, W - 2 * M, 46)
    y += 14
    y, _ = n_summary(d, M, y, top_story.lead, 20, 3, W - 2 * M, 29)
    y += 12
    n_meta(d, M, y, top_story, 11)
    y += 54

    specs = [(27, 18, 2, 2, 44), (24, 17, 2, 2, 38), (21, 16, 2, 2, 0)]
    overflow: list[ProbeItem] = []
    for offset, (story, spec) in enumerate(zip(secondary, specs)):
        if draw_n2_story(n_scratch(), story, y, *spec) > N_BOTTOM:
            overflow = secondary[offset:]
            break
        y = draw_n2_story(d, story, y, *spec)
    n_footer(d, 1, rule=False)
    return img, overflow, y


def draw_n2_list(stories: list[ProbeItem]):
    img, d = canvas()
    d.text((M, 34), "Kindle Times", font=font("serif_b", 24), fill=INK)
    d.text((W // 2, 40), "朝刊", font=font("sans", 11), fill=GRAY, anchor="ma")
    d.text((W - M, 40), DATE, font=font("sans", 11), fill=GRAY, anchor="ra")
    d.text((M, 76), "続き", font=font("serif_b", 32), fill=INK)
    d.text((W - M, 86), "05—09", font=font("sans", 15), fill=GRAY, anchor="ra")

    y = 138
    overflow: list[ProbeItem] = []
    for offset, story in enumerate(stories):
        if draw_n2_story(n_scratch(), story, y, 23, 16, 2, 2, 30) > N_BOTTOM:
            overflow = stories[offset:]
            break
        y = draw_n2_story(d, story, y, 23, 16, 2, 2, 30)
    n_footer(d, 2, rule=False)
    return img, overflow, y


# --- N3: box and pillar ------------------------------------------------------


def draw_n3_masthead(d):
    hline(d, 24, weight=3)
    d.text((M, 38), "Kindle Times", font=font("serif_b", 40), fill=INK)
    d.text((W - M, 48), DATE, font=font("sans", 12), fill=GRAY, anchor="ra")
    d.text((W - M, 68), "朝刊　第 1 面", font=font("sans", 11), fill=GRAY, anchor="ra")
    hline(d, 100, weight=2)


def draw_n3_box(d, story: ProbeItem):
    top, bottom = 112, 356
    d.rectangle((M, top, W - M - 1, bottom), outline=INK, width=2)
    d.rectangle((M + 2, top + 2, M + 8, bottom - 2), fill=INK)
    x = M + 24
    d.text((x, top + 14), f"01　{story.section}", font=font("sans", 11), fill=GRAY)
    y, _ = n_head(d, x, top + 36, story.title, 30, 3, W - M - 24 - x, 38)
    y += 10
    y, _ = n_summary(d, x, y, story.lead, 16, 2, W - M - 24 - x, 23)
    y += 8
    n_meta(d, x, y, story, 10)


def draw_n3_section(
    d,
    section: str,
    stories: list[ProbeItem],
    y: int,
    head_size: int = 20,
    head_lead: int = 26,
    summary_size: int = 14,
    summary_lead: int = 19,
    meta_size: int = 10,
    gap: int = 18,
):
    hline(d, y, weight=2)
    d.text((M, y + 6), section, font=font("serif_b", 19), fill=INK)
    d.text((W - M, y + 11), f"{len(stories)}本", font=font("sans", 10), fill=GRAY, anchor="ra")
    y += 34
    for story in stories:
        y, _ = n_head(d, M, y, story.title, head_size, 2, W - 2 * M, head_lead)
        y += 8
        y, _ = n_summary(d, M, y, story.lead, summary_size, 2, W - 2 * M, summary_lead)
        y += 5
        d.text((M, y), f"{story.source}　{story.published}", font=font("sans", meta_size), fill=GRAY)
        y += meta_size + 12
    return y + gap


def draw_n3_front(top_story: ProbeItem, groups: list[tuple[str, list[ProbeItem]]]):
    img, d = canvas()
    draw_n3_masthead(d)
    draw_n3_box(d, top_story)
    used = {top_story.id}
    overflow: list[tuple[str, list[ProbeItem]]] = []
    y = 372
    for offset, (section, stories) in enumerate(groups):
        if draw_n3_section(n_scratch(), section, stories, y) > N_BOTTOM:
            overflow = groups[offset:]
            break
        y = draw_n3_section(d, section, stories, y)
        used.update(story.id for story in stories)
    n_footer(d, 1)
    return img, used, overflow, y


def draw_n3_list(groups: list[tuple[str, list[ProbeItem]]]):
    img, d = canvas()
    hline(d, 20, weight=3)
    d.text((M, 32), "Kindle Times", font=font("serif_b", 28), fill=INK)
    d.text((W - M, 40), DATE, font=font("sans", 11), fill=GRAY, anchor="ra")
    hline(d, 70, weight=2)
    d.text((M, 84), "第 2 面", font=font("serif_b", 30), fill=INK)
    d.text((W - M, 94), "05—09", font=font("sans", 15), fill=GRAY, anchor="ra")
    y = 134
    overflow: list[tuple[str, list[ProbeItem]]] = []
    for offset, (section, stories) in enumerate(groups):
        trial = draw_n3_section(
            n_scratch(),
            section,
            stories,
            y,
            head_size=22,
            head_lead=29,
            summary_size=15,
            summary_lead=20,
            meta_size=11,
            gap=24,
        )
        if trial > N_BOTTOM:
            overflow = groups[offset:]
            break
        y = draw_n3_section(
            d,
            section,
            stories,
            y,
            head_size=22,
            head_lead=29,
            summary_size=15,
            summary_lead=20,
            meta_size=11,
            gap=24,
        )
    n_footer(d, 2)
    return img, overflow, y


# ---------------------------------------------------------------------------
# Composite comparisons and output
# ---------------------------------------------------------------------------


def save(img, path: Path):
    img.save(path, "PNG", optimize=True, dpi=(212, 212))


def comparison(files: list[str], labels: list[str], path: Path, scale: float = 0.42):
    tw, th = int(W * scale), int(H * scale)
    margin, gap, band = 24, 18, 38
    sheet = Image.new("L", (margin * 2 + tw * 3 + gap * 2, margin * 2 + band + th), PAPER)
    d = ImageDraw.Draw(sheet)
    for i, (name, label) in enumerate(zip(files, labels)):
        x = margin + i * (tw + gap)
        d.text((x, margin + 4), label, font=font("serif_b", 20), fill=INK)
        page = Image.open(OUT / name).convert("L").resize((tw, th), Image.Resampling.LANCZOS)
        sheet.paste(page, (x, margin + band))
        d.rectangle((x, margin + band, x + tw - 1, margin + band + th - 1), outline=HAIR)
    save(sheet, path)


def render_all():
    top = item("架空市で住民票システム障害、復旧作業続く", "社会", "05:12")
    a_second = [
        item("架空県に大型台風接近、沿岸部に避難指示", "社会", "05:04"),
        item("架空市議会、減税条例案を可決", "政治", "04:58"),
        item("架空国との通商交渉が難航", "国際", "05:01"),
        item("架空選手、世界選手権で金メダル", "スポーツ", "05:09"),
    ]
    a_economy = [
        item("政府、架空燃料補助の見直しを発表", "経済", "04:41"),
        item("架空自動車大手、生産体制を縮小", "経済", "04:36"),
        item("架空アパレル、再生手続きを申請", "経済", "04:22"),
        item("架空食品チェーン、回収対象を拡大", "経済", "04:18"),
        item("架空電機、子会社の事業を譲渡", "テック", "05:00"),
        item("架空カーシェア、利用者に補償", "経済", "05:12"),
    ]

    b_stories = [
        top,
        item("架空県に大型台風接近、沿岸部に避難指示", "社会", "05:04"),
        item("架空市議会、減税条例案を可決", "政治", "04:58"),
        item("架空AI企業、新モデルを公開", "テック", "05:06"),
        item("架空通貨、対ドルで上昇", "経済", "04:50"),
        item("架空電機、子会社の事業を譲渡", "テック", "05:00"),
        item("架空チケット詐欺、注意呼びかけ", "社会", "04:44"),
        item("架空国との通商交渉が難航", "国際", "05:01"),
        item("架空選手、世界選手権で金メダル", "スポーツ", "05:09"),
        item("架空カーシェア、利用者に補償", "経済", "05:12"),
    ]

    c_sogo = [
        top,
        item("架空県に大型台風接近、沿岸部に避難指示", "社会", "05:04"),
        item("架空市議会、減税条例案を可決", "政治", "04:58"),
        item("架空国との通商交渉が難航", "国際", "05:01"),
        item("架空通貨、対ドルで上昇", "経済", "04:50"),
        item("架空AI企業、新モデルを公開", "テック", "05:06"),
        item("架空選手、世界選手権で金メダル", "スポーツ", "05:09"),
        item("架空電機、子会社の事業を譲渡", "テック", "05:00"),
        item("架空チケット詐欺、注意呼びかけ", "社会", "04:44"),
        item("架空自動車大手、生産体制を縮小", "国際", "04:36"),
        item("架空大規模言語モデル、仕組みを解説", "テック", "05:15"),
    ]
    c_tech = [
        item("架空AI企業、新モデルを公開", "テック", "05:06"),
        item("架空サイバー攻撃ツール、解析進む", "テック", "05:02"),
        item("架空電機、子会社の事業を譲渡", "テック", "05:00"),
        item("架空ベータ版、無償提供を開始", "テック", "04:55"),
        item("架空コードレビュー、合格率を調査", "テック", "04:52"),
        item("架空の軌跡、研究チームが発表", "テック", "04:48"),
        item("架空大規模言語モデル、仕組みを解説", "テック", "05:15"),
        item("架空モデル内部、観測手法を提案", "テック", "04:40"),
        item("架空形式検証、実用化へ", "テック", "04:37"),
        item("架空パスワード管理、新機能", "テック", "04:33"),
        item("架空ホワイトハッカー、育成計画", "テック", "04:30"),
    ]

    OUT.mkdir(parents=True, exist_ok=True)
    files = {
        "a-1-front.png": draw_a_front(top, a_second),
        "a-2-economy.png": draw_a_section(a_economy[0], a_economy[1:]),
        "a-3-article.png": draw_article(top, "社会", 11),
        "b-1-brief.png": draw_b_front(b_stories[:5], 1, 44, "昨夜から今朝までのニュースを、10本に。", 1),
        "b-2-brief-read.png": draw_b_front(
            b_stories[5:], 6, 38, "続き。読み終えた2本は薄く表示している。", 2, read_count=2
        ),
        "b-3-article.png": draw_article(b_stories[9], "経済", 11, next_link=True),
        "c-1-index.png": draw_c_index("総合", c_sogo, 1),
        "c-2-index-tech.png": draw_c_index("テック", c_tech, 2),
        "c-3-article.png": draw_article(
            c_tech[2], "テック", 12, progress=0.35, head_size=38, head_leading=44
        ),
    }
    for name, image in files.items():
        save(image, OUT / name)

    comparison(
        ["a-1-front.png", "b-1-brief.png", "c-1-index.png"],
        ["A 朝刊", "B ブリーフ", "C 索引"],
        OUT / "compare-fronts.png",
    )
    comparison(
        ["a-3-article.png", "b-3-article.png", "c-3-article.png"],
        ["A 朝刊 / 記事", "B ブリーフ / 記事", "C 索引 / 記事"],
        OUT / "compare-articles.png",
    )

    probe = load_probe()
    top10 = probe[:10]
    article = next((story for story in probe if story.id == "ex-014"), None)
    next_article = next((story for story in probe if story.id == "ex-015"), None)
    if article is None or next_article is None:
        sys.exit("probe data is missing the article fixtures ex-014 or ex-015")
    ab_files = {
        "ab1-1-front.png": draw_ab1_front(top10[:4]),
        "ab1-2-list.png": draw_ab1_list(top10[4:10]),
        "ab2-1-brief.png": draw_ab2_front(top10[:5]),
        "ab2-2-list.png": draw_ab2_list(top10[5:10]),
        "ab-3-article.png": draw_ab_article(article, 2, 4),
        "ab-4-article-last.png": draw_ab_article(article, 4, 4, next_story=next_article),
    }
    for name, image in ab_files.items():
        save(image, OUT / name)
    comparison(
        ["ab1-1-front.png", "ab2-1-brief.png", "ab-3-article.png"],
        ["AB1 1面", "AB2 ブリーフ", "記事 2ページ"],
        OUT / "compare-ab.png",
        scale=0.6,
    )

    n_front = top10[:4]
    n_list = top10[4:9]

    n1_front_img, n1_front_over, _ = draw_n1_front(n_front, n_list)
    n1_list_img, _, _ = draw_n1_list(n_list + n1_front_over)
    n2_front_img, n2_front_over, _ = draw_n2_front(n_front)
    n2_list_img, _, _ = draw_n2_list(n_list + n2_front_over)
    n3_front_img, n3_used, _, _ = draw_n3_front(
        top10[0],
        [
            ("政治", [top10[1], top10[2]]),
            ("国際", [top10[3]]),
            ("経済", [top10[6]]),
            ("スポーツ", [top10[8]]),
        ],
    )
    n3_list_img, _, _ = draw_n3_list(group_by_section([story for story in top10 if story.id not in n3_used]))

    n_files = {
        "n1-1-front.png": n1_front_img,
        "n1-2-list.png": n1_list_img,
        "n2-1-front.png": n2_front_img,
        "n2-2-list.png": n2_list_img,
        "n3-1-front.png": n3_front_img,
        "n3-2-list.png": n3_list_img,
    }
    for name, image in n_files.items():
        save(image, OUT / name)
    comparison(
        ["n1-1-front.png", "n2-1-front.png", "n3-1-front.png"],
        ["N1 一般紙", "N2 罫線なし", "N3 囲み・柱"],
        OUT / "compare-n.png",
        scale=0.52,
    )
    print(f"wrote {len(files) + 2 + len(ab_files) + 1 + len(n_files) + 1} PNGs to {OUT.relative_to(ROOT)}")


# ---------------------------------------------------------------------------
# Self test
# ---------------------------------------------------------------------------


def selftest():
    face = font("serif", 30)
    measure = face.getlength
    width = W - 2 * M
    cases = [
        "これはテストです。" * 20,
        "「あいうえおかきくけこ」、さしすせそ。たちつてと。" * 10,
        "あ" * 22 + "。" + "い" * 30,
        "あ" * 22 + "。」" + "い" * 30,
        "あ" * 22 + "ー" + "い" * 30,
        "あ" * 22 + "っ" + "い" * 30,
        "あ" * 22 + "「" + "い" * 30,
        "日本語の文章にABC123とverylongwordwithoutspacesを混ぜる。" * 8,
    ]
    checks = 0
    for text in cases:
        lines = wrap_paragraph(text, width, measure)
        assert lines, f"no lines for {text[:12]}"
        assert "".join(lines).replace(" ", "") == text.replace(" ", ""), "text changed during wrap"
        for line in lines:
            assert line and line[0] not in LEAD_FORBIDDEN, f"line starts forbidden: {line[:12]}"
            checks += 1
        for line in lines[:-1]:
            assert line[-1] not in TAIL_FORBIDDEN, f"line ends with opening bracket: {line[-12:]}"
        for line in lines:
            assert measure(line) <= width + measure("あ"), f"line overflows: {line[:12]}"

    long_word = wrap_paragraph("A" * 80, 200, measure)
    assert all(measure(line) <= 200 for line in long_word), "long word was not split"

    pages = paginate(list(range(30)), 13)
    assert [len(page) for page in pages] == [13, 13, 4], "pagination count changed"
    assert paginate([], 13) == [[]], "empty pagination changed"

    ab_cases = [
        "あ" * 23 + "ー" + "い" * 30,
        "あ" * 23 + "」" + "い" * 30,
        "あ" * 23 + "っ" + "い" * 30,
        "あ" * 23 + "・" + "い" * 30,
        "あ" * 23 + "。" + "い" * 30,
        "あ" * 22 + "。」" + "い" * 30,
        "「あいうえおかきくけこ」、さしすせそ。たちつてと。" * 10,
    ]
    ab_lines = 0
    for text in ab_cases:
        lines = wrap_paragraph_ab(text, width, measure)
        assert lines, f"AB wrap produced no lines for {text[:12]}"
        assert "".join(lines).replace(" ", "") == text.replace(" ", ""), "AB wrap changed text"
        for line in lines:
            assert line and line[0] not in LEAD_FORBIDDEN, f"AB line starts forbidden: {line[:12]}"
            assert line[0] not in "ー」っ", f"AB line starts with pushed-out mark: {line[:12]}"
            assert line[-1] not in TAIL_FORBIDDEN, f"AB line ends with opening bracket: {line[-12:]}"
            if line[-1] not in HANGING:
                assert measure(line) <= width + 0.5, f"AB line overflows: {line[:12]}"
            ab_lines += 1

    justified = 0
    checked = 0
    for story in load_probe():
        for line, justify in ab_article_lines(story, face, width):
            checked += 1
            assert line, f"{story.id}: empty AB line"
            assert line[0] not in "ー」っ", f"{story.id}: line starts with pushed-out mark: {line[:12]}"
            assert line[0] not in LEAD_FORBIDDEN, f"{story.id}: line starts forbidden: {line[:12]}"
            if line[-1] not in HANGING:
                assert measure(line) <= width + 0.5, f"{story.id}: line overflows: {line[-12:]}"
            else:
                assert not justify, f"{story.id}: hanging punctuation line was justified"
            if justify:
                gap = justify_gap(line, face, width)
                edge = sum(face.getlength(ch) for ch in line) + (gap or 0.0) * (len(line) - 1)
                assert abs(edge - width) <= 1, f"{story.id}: justified edge {edge} != {width}"
                justified += 1
    assert justified > 0, "no justified lines in selftest"

    print(
        f"selftest: {len(cases)} texts, {checks} lines, no forbidden line starts; "
        f"AB {len(ab_cases)} texts, {ab_lines} lines; probe {checked} lines, "
        f"{justified} justified, no pushed-out starts or overflows"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true", help="check the line breaker and exit")
    args = parser.parse_args()
    if args.selftest:
        selftest()
    else:
        render_all()


if __name__ == "__main__":
    main()
