#!/usr/bin/env python3
"""Probe full-text Japanese news sources and write a device-format edition.

Sources: 時事通信 (sitemaps), FNN (RSS), ITmedia (RSS, existing cleaner).
Writes out/probe-v2/edition.tsv, index.tsv, articles/<id>.txt and report.json.
Re-execs into .venv when run with a Python that lacks the extractor deps.
"""

from __future__ import annotations

import json
import os
import re
import statistics
import sys
import time
import urllib.robotparser
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import urljoin, urlsplit
from zoneinfo import ZoneInfo

REPO_ROOT = Path(__file__).resolve().parents[2]


def _reexec_in_repo_venv() -> None:
    try:
        import feedparser  # noqa: F401
        import lxml  # noqa: F401
        import trafilatura  # noqa: F401
    except ModuleNotFoundError:
        venv = REPO_ROOT / ".venv"
        python = venv / "bin" / "python"
        if python.exists() and Path(sys.prefix).resolve() != venv.resolve():
            os.execv(str(python), [str(python), *sys.argv])
        raise


_reexec_in_repo_venv()

import feedparser
import lxml.html
import requests
import trafilatura

sys.path.insert(0, str(REPO_ROOT / "scripts"))
from text_edition import clean_itmedia_body, strip_metadata_lines

JST = ZoneInfo("Asia/Tokyo")
OUT_DIR = REPO_ROOT / "out" / "probe-v2"

# Honest UA, same as scripts/text_edition.py. No browser impersonation.
USER_AGENT = "kindle-newspaper-spike/0.1"
REQUEST_GAP_SECONDS = 1.0
MIN_BODY_CHARS = 200
MAX_ARTICLES = 60
JIJI_PER_SECTION = 7
FNN_LIMIT = 24
ITMEDIA_LIMIT = 18

# Same shape as the text edition's score weights.
SOURCE_WEIGHT = {"時事通信": 800, "FNN": 800, "ITmedia": 600}
# Keep the edition mixed when body_ok exceeds the target.
SOURCE_QUOTA = {"時事通信": 26, "FNN": 20, "ITmedia": 14}
JA_KEYWORDS = ("死亡", "災害", "警報", "選挙", "首相", "速報", "事故")
LONG_BODY_CHARS = 2000

JIJI_SECTIONS = (
    ("pol", "政治"),
    ("eco", "経済"),
    ("int", "国際"),
    ("soc", "社会"),
    ("spo", "スポーツ"),
)
FNN_SECTION_MAP = {
    "政治": "政治",
    "経済": "経済",
    "国際": "国際",
    "社会": "社会",
    "スポーツ": "スポーツ",
    "エンタメ": "文化",
    "テクノロジー": "テクノロジー",
}
NS = {
    "sm": "http://www.sitemaps.org/schemas/sitemap/0.9",
    "news": "http://www.google.com/schemas/sitemap-news/0.9",
}


class RobotsDenied(Exception):
    pass


@dataclass
class Candidate:
    source: str
    key: str
    section: str
    title: str
    url: str
    published: datetime
    rank: int


@dataclass
class Article:
    candidate: Candidate
    paragraphs: list[str]
    chars: int
    lead: str
    score: float = 0.0
    id: str = ""


@dataclass
class SourceStats:
    name: str
    candidates: int = 0
    attempted: int = 0
    pages_ok: int = 0
    body_ok: int = 0
    written: int = 0
    chars: list[int] = field(default_factory=list)
    dropped: dict[str, int] = field(default_factory=dict)
    duplicates: int = 0
    requests: int = 0
    seconds: float = 0.0

    def drop(self, reason: str) -> None:
        self.dropped[reason] = self.dropped.get(reason, 0) + 1


class Fetcher:
    def __init__(self) -> None:
        self.session = requests.Session()
        self.session.headers["User-Agent"] = USER_AGENT
        self.robots: dict[str, urllib.robotparser.RobotFileParser] = {}
        self.delays: dict[str, float] = {}
        self.last_hit: dict[str, float] = {}
        self.requests_by_source: dict[str, int] = {}
        self.requests_by_host: dict[str, int] = {}

    def _pace(self, host: str) -> None:
        gap = max(REQUEST_GAP_SECONDS, self.delays.get(host, 0.0))
        last = self.last_hit.get(host)
        if last is not None:
            wait = gap - (time.monotonic() - last)
            if wait > 0:
                time.sleep(wait)
        self.last_hit[host] = time.monotonic()

    def _allowed(self, url: str, source: str) -> bool:
        parts = urlsplit(url)
        host = parts.netloc
        if host not in self.robots:
            robots_url = f"{parts.scheme}://{host}/robots.txt"
            text = self.get(robots_url, source, check_robots=False)
            parser = urllib.robotparser.RobotFileParser()
            if text is None:
                parser.disallow_all = True
            else:
                parser.parse(text.splitlines())
                delay = parser.crawl_delay(USER_AGENT)
                if delay:
                    self.delays[host] = float(delay)
            self.robots[host] = parser
        return self.robots[host].can_fetch(USER_AGENT, url)

    def get(self, url: str, source: str, *, check_robots: bool = True) -> str | None:
        if check_robots and not self._allowed(url, source):
            raise RobotsDenied(url)
        host = urlsplit(url).netloc
        self._pace(host)
        self.requests_by_source[source] = self.requests_by_source.get(source, 0) + 1
        self.requests_by_host[host] = self.requests_by_host.get(host, 0) + 1
        try:
            resp = self.session.get(url, timeout=20)
        except requests.RequestException:
            return None
        if resp.status_code != 200:
            return None
        if not resp.encoding:
            resp.encoding = "utf-8"
        return resp.text


def sanitize(value: str) -> str:
    return re.sub(r"[\t\r\n]+", " ", str(value)).strip()


def feed_datetime(entry: feedparser.FeedParserDict) -> datetime:
    stamp = entry.get("published_parsed") or entry.get("updated_parsed")
    if not stamp:
        return datetime.now(JST)
    return datetime(*stamp[:6], tzinfo=timezone.utc).astimezone(JST)


def fnn_section(raw: str) -> str:
    first = (raw or "").split(",")[0].strip()
    return FNN_SECTION_MAP.get(first, "社会")


def collect_jiji(fetcher: Fetcher) -> list[Candidate]:
    out: list[Candidate] = []
    for code, section in JIJI_SECTIONS:
        url = f"https://www.jiji.com/sitemap/sitemap_{code}.xml"
        text = fetcher.get(url, "時事通信")
        if not text:
            continue
        root = ET.fromstring(text)
        rows: list[tuple[datetime, str, str]] = []
        for node in root.findall("sm:url", NS):
            link = node.findtext("sm:loc", namespaces=NS)
            published = node.findtext("news:news/news:publication_date", namespaces=NS)
            title = node.findtext("news:news/news:title", namespaces=NS)
            if not link or not published or not title:
                continue
            rows.append((datetime.fromisoformat(published), title, link))
        rows.sort(key=lambda row: row[0], reverse=True)
        for rank, (published, title, link) in enumerate(rows[:JIJI_PER_SECTION]):
            out.append(Candidate("時事通信", "jiji", section, title, link, published, rank))
    return out


def collect_fnn(fetcher: Fetcher) -> list[Candidate]:
    text = fetcher.get("https://www.fnn.jp/list/feed/rss", "FNN")
    if not text:
        return []
    parsed = feedparser.parse(text)
    out: list[Candidate] = []
    for rank, entry in enumerate(parsed.entries[:FNN_LIMIT]):
        link = (entry.get("link") or "").strip()
        title = (entry.get("title") or "").strip()
        if not link or not title:
            continue
        out.append(
            Candidate(
                "FNN",
                "fnn",
                fnn_section(entry.get("category", "")),
                title,
                link,
                feed_datetime(entry),
                rank,
            )
        )
    return out


def collect_itmedia(fetcher: Fetcher) -> list[Candidate]:
    text = fetcher.get("https://rss.itmedia.co.jp/rss/2.0/news_bursts.xml", "ITmedia")
    if not text:
        return []
    parsed = feedparser.parse(text)
    out: list[Candidate] = []
    for rank, entry in enumerate(parsed.entries[:ITMEDIA_LIMIT]):
        link = (entry.get("link") or "").strip()
        title = (entry.get("title") or "").strip()
        if not link or not title:
            continue
        out.append(
            Candidate(
                "ITmedia",
                "itm",
                "テクノロジー",
                title,
                link,
                feed_datetime(entry),
                rank,
            )
        )
    return out


def nodes_to_paragraphs(nodes: list[lxml.html.HtmlElement]) -> list[str]:
    paragraphs: list[str] = []
    for node in nodes:
        text = re.sub(r"[\t\r\n]+", " ", node.text_content()).strip(" \u3000")
        if text:
            paragraphs.append(text)
    return paragraphs


def trafilatura_paragraphs(html: str) -> list[str]:
    text = trafilatura.extract(
        html,
        include_comments=False,
        include_tables=False,
        favor_precision=True,
    )
    return [line.strip() for line in (text or "").splitlines() if line.strip()]


def extract_jiji(html: str) -> list[str]:
    doc = lxml.html.fromstring(html)
    nodes = doc.xpath(
        '//div[contains(concat(" ", normalize-space(@class), " "), " ArticleText ")]'
        "//p[not(ancestor::figcaption)"
        ' and not(contains(concat(" ", normalize-space(@class), " "), " ArticleTextTab "))]'
    )
    return nodes_to_paragraphs(nodes)


def extract_fnn(html: str) -> list[str]:
    doc = lxml.html.fromstring(html)
    best: list[str] = []
    for div in doc.xpath('//div[normalize-space(@class)="article-body"]'):
        paragraphs = nodes_to_paragraphs(div.xpath(".//p"))
        if sum(len(p) for p in paragraphs) > sum(len(p) for p in best):
            best = paragraphs
    return best


def extract_itmedia(html: str) -> list[str]:
    cleaned = clean_itmedia_body(strip_metadata_lines("\n".join(trafilatura_paragraphs(html))))
    return [line.strip() for line in cleaned.splitlines() if line.strip()]


def strip_title_and_footer(paragraphs: list[str], title: str) -> list[str]:
    normalized_title = re.sub(r"\s+", "", title)
    kept: list[str] = []
    seen: set[str] = set()
    for paragraph in paragraphs:
        paragraph = re.sub(r"（\d+/\d+ ページ）$", "", paragraph).strip()
        normalized = re.sub(r"\s+", "", paragraph)
        if normalized_title and normalized.startswith(normalized_title) and len(normalized) <= len(
            normalized_title
        ) + 12:
            continue
        if paragraph.startswith("Copyright ©"):
            continue
        if len(paragraph) > 20 and paragraph in seen:
            continue
        seen.add(paragraph)
        kept.append(paragraph)
    return kept


EXTRACTORS = {"jiji": extract_jiji, "fnn": extract_fnn, "itm": extract_itmedia}
MAX_ITMEDIA_PAGES = 3


def itmedia_next_page(html: str, current_url: str) -> str | None:
    doc = lxml.html.fromstring(html)
    for href in doc.xpath('//a[contains(@class, "p-pagination__controller")]/@href'):
        url = urljoin(current_url, href)
        if url != current_url:
            return url
    return None


def fetch_paragraphs(fetcher: Fetcher, candidate: Candidate) -> tuple[list[str] | None, str | None]:
    try:
        html = fetcher.get(candidate.url, candidate.source)
    except RobotsDenied:
        return None, "robots"
    if html is None or not html.strip():
        return None, "fetch_error"
    try:
        paragraphs = EXTRACTORS[candidate.key](html)
    except (lxml.etree.ParserError, ValueError):
        paragraphs = []
    if candidate.key == "itm":
        visited = {candidate.url}
        next_url = itmedia_next_page(html, candidate.url)
        while next_url and next_url not in visited and len(visited) < MAX_ITMEDIA_PAGES:
            visited.add(next_url)
            try:
                page = fetcher.get(next_url, candidate.source)
            except RobotsDenied:
                break
            if not page:
                break
            paragraphs.extend(extract_itmedia(page))
            next_url = itmedia_next_page(page, next_url)
    if sum(len(p) for p in paragraphs) < MIN_BODY_CHARS:
        fallback = trafilatura_paragraphs(html)
        if sum(len(p) for p in fallback) > sum(len(p) for p in paragraphs):
            paragraphs = fallback
    return strip_title_and_footer(paragraphs, candidate.title), None


def truncation_marker(paragraphs: list[str]) -> str:
    for marker in ("続きを読む", "会員限定"):
        if any(marker in p for p in paragraphs):
            return marker
    if paragraphs and "有料" in paragraphs[-1]:
        return "有料"
    return ""


def make_lead(paragraphs: list[str]) -> str:
    text = " ".join(paragraphs)
    sentences = [s.strip() for s in re.split(r"(?<=[。！？])", text) if s.strip()]
    lead = ""
    for sentence in sentences[:2]:
        merged = lead + sentence
        if len(merged) > 120:
            lead = merged[:120]
            break
        lead = merged
        if len(lead) >= 60:
            break
    return lead.strip()[:120]


def compute_score(article: Article, now: datetime) -> float:
    candidate = article.candidate
    score = float(SOURCE_WEIGHT.get(candidate.source, 300))
    score += max(0, 50 - candidate.rank * 5)
    age_days = (now - candidate.published).total_seconds() / 86400
    if age_days <= 0:
        score += 40
    elif age_days <= 1:
        score += 30
    elif age_days <= 2:
        score += 15
    elif age_days > 3:
        score -= 20
    text = candidate.title + article.lead
    for keyword in JA_KEYWORDS:
        if keyword in text:
            score += 35
    if article.chars > LONG_BODY_CHARS:
        score -= 150
    return score


def harvest(fetcher: Fetcher, candidates: list[Candidate], limit: int) -> tuple[list[Article], SourceStats]:
    name = candidates[0].source if candidates else "unknown"
    stats = SourceStats(name=name, candidates=len(candidates))
    started = time.monotonic()
    kept: list[Article] = []
    for candidate in candidates[:limit]:
        stats.attempted += 1
        paragraphs, error = fetch_paragraphs(fetcher, candidate)
        if error:
            stats.drop(error)
            continue
        stats.pages_ok += 1
        assert paragraphs is not None
        paragraphs = [sanitize(p) for p in paragraphs]
        paragraphs = [p for p in paragraphs if p]
        if not paragraphs:
            stats.drop("empty")
            continue
        chars = len("\n".join(paragraphs))
        if chars < MIN_BODY_CHARS:
            stats.drop("short")
            continue
        marker = truncation_marker(paragraphs)
        if marker:
            stats.drop(f"truncated:{marker}")
            continue
        stats.body_ok += 1
        stats.chars.append(chars)
        kept.append(Article(candidate, paragraphs, chars, make_lead(paragraphs)))
    stats.seconds = time.monotonic() - started
    return kept, stats


def main() -> int:
    started = time.monotonic()
    now = datetime.now(JST)
    fetcher = Fetcher()

    collectors = (
        (collect_jiji, JIJI_PER_SECTION * len(JIJI_SECTIONS)),
        (collect_fnn, FNN_LIMIT),
        (collect_itmedia, ITMEDIA_LIMIT),
    )

    articles: list[Article] = []
    stats: dict[str, SourceStats] = {}
    for collect, limit in collectors:
        source_started = time.monotonic()
        collected = collect(fetcher)
        kept, source_stats = harvest(fetcher, collected, limit)
        source_stats.seconds = time.monotonic() - source_started
        articles.extend(kept)
        stats[source_stats.name] = source_stats

    for article in articles:
        article.score = compute_score(article, now)
    articles.sort(key=lambda a: a.score, reverse=True)

    unique = dedupe_by_title(articles, stats)
    selected = select_articles(unique)
    counters: dict[str, int] = {}
    for article in selected:
        key = article.candidate.key
        counters[key] = counters.get(key, 0) + 1
        article.id = f"{key}-{counters[key]:03d}"
    for article in selected:
        stats[article.candidate.source].written += 1

    edition_id = now.strftime("%Y%m%d-%H%M")
    generated = now.isoformat(timespec="seconds")
    write_output(selected, edition_id, generated)

    elapsed = time.monotonic() - started
    for name, source_stats in stats.items():
        source_stats.requests = fetcher.requests_by_source.get(name, 0)

    report = {
        "generated": generated,
        "edition_id": edition_id,
        "elapsed_seconds": round(elapsed, 1),
        "total_requests": sum(fetcher.requests_by_source.values()),
        "requests_by_host": fetcher.requests_by_host,
        "written": len(selected),
        "body_ok": sum(s.body_ok for s in stats.values()),
        "duplicates_merged": sum(s.duplicates for s in stats.values()),
        "overflow_over_target": max(0, len(unique) - len(selected)),
        "sources": {
            name: {
                "candidates": s.candidates,
                "attempted": s.attempted,
                "pages_ok": s.pages_ok,
                "body_ok": s.body_ok,
                "written": s.written,
                "dropped": s.dropped,
                "duplicates": s.duplicates,
                "chars_median": int(statistics.median(s.chars)) if s.chars else 0,
                "chars_max": max(s.chars) if s.chars else 0,
                "requests": s.requests,
                "seconds": round(s.seconds, 1),
            }
            for name, s in stats.items()
        },
        "sections": count_sections(selected),
    }
    (OUT_DIR / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2))
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


def dedupe_by_title(articles: list[Article], stats: dict[str, SourceStats]) -> list[Article]:
    unique: list[Article] = []
    seen: set[str] = set()
    for article in articles:
        normalized = re.sub(r"\s+", "", article.candidate.title)
        if normalized in seen:
            stats[article.candidate.source].duplicates += 1
            continue
        seen.add(normalized)
        unique.append(article)
    return unique


def select_articles(articles: list[Article]) -> list[Article]:
    selected: list[Article] = []
    deferred: list[Article] = []
    counts: dict[str, int] = {}
    for article in articles:
        source = article.candidate.source
        if counts.get(source, 0) < SOURCE_QUOTA.get(source, MAX_ARTICLES):
            counts[source] = counts.get(source, 0) + 1
            selected.append(article)
        else:
            deferred.append(article)
    if len(selected) < MAX_ARTICLES:
        selected.extend(deferred[: MAX_ARTICLES - len(selected)])
    selected.sort(key=lambda a: (a.score, a.candidate.published), reverse=True)
    return selected[:MAX_ARTICLES]


def count_sections(articles: list[Article]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for article in articles:
        section = article.candidate.section
        counts[section] = counts.get(section, 0) + 1
    return counts


def write_output(articles: list[Article], edition_id: str, generated: str) -> None:
    articles_dir = OUT_DIR / "articles"
    articles_dir.mkdir(parents=True, exist_ok=True)
    for stale in articles_dir.glob("*.txt"):
        stale.unlink()

    (OUT_DIR / "edition.tsv").write_text(
        f"id\t{edition_id}\ngenerated\t{generated}\n", encoding="utf-8"
    )

    rows: list[str] = []
    for article in articles:
        candidate = article.candidate
        rows.append(
            "\t".join(
                (
                    article.id,
                    sanitize(candidate.section),
                    sanitize(candidate.source),
                    candidate.published.astimezone(JST).strftime("%H:%M"),
                    str(article.chars),
                    sanitize(candidate.title),
                    sanitize(article.lead),
                )
            )
        )
    (OUT_DIR / "index.tsv").write_text("\n".join(rows) + "\n", encoding="utf-8")

    for article in articles:
        candidate = article.candidate
        header = (
            f"{sanitize(candidate.title)}\n"
            f"{sanitize(candidate.source)}\t"
            f"{candidate.published.astimezone(JST).strftime('%H:%M')}\t"
            f"{candidate.url}\n"
            "\n"
        )
        body = "\n".join(article.paragraphs)
        (articles_dir / f"{article.id}.txt").write_text(header + body + "\n", encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
