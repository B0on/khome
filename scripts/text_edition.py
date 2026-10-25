#!/usr/bin/env python3
"""Build one text edition that the Kindle renders itself.

Collects Japanese news text, keeps only articles with a full body, orders them
with one section lead per section up front, and publishes a text-v1 bundle:
edition.tsv, index.tsv, articles/<id>.txt, manifest.json and SHA256SUMS.
latest-text.json carries the bundle id, size hash, path and manifest hash the
device verifies before it swaps text/current.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import time
import urllib.robotparser
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any
from urllib.parse import urljoin, urlsplit
from zoneinfo import ZoneInfo

JST = ZoneInfo("Asia/Tokyo")
REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_ROOT = REPO_ROOT / "out" / "editions"

USER_AGENT = "kindle-newspaper-spike/0.1"
REQUEST_GAP_SECONDS = 1.0
MIN_BODY_CHARS = 200
MAX_ARTICLES = 30
LEAD_CHARS = 120
MIN_LEAD_CHARS = 40
KEEP_EDITIONS = 2
JIJI_PER_SECTION = 7
FNN_LIMIT = 24
ITMEDIA_LIMIT = 18
MAX_ITMEDIA_PAGES = 3
FEED_LIMIT = 18
TECH_MIN = 5
SECTION_CAPS = {"文化": 1}
ITMEDIA_FEED = "https://rss.itmedia.co.jp/rss/2.0/itmedia_all.xml"
ITMEDIA_EXCLUDED_PATHS = ("/ait/", "/aiplus/")
PCWATCH_FEED = "https://pc.watch.impress.co.jp/data/rss/1.0/pcw/feed.rdf"
NAZOLOGY_FEED = "https://nazology.kusuguru.co.jp/feed"
NATALIE_FEED = "https://natalie.mu/music/feed/google_index/type/news"

FORMAT = "text-v1"
TEXT_DIR_RE = re.compile(r"^text-(\d{8}-\d{4})$")
PUBLISHED_RE = re.compile(r"^\d{2}:\d{2}$")
FULLWIDTH_ASCII = {
    **{0xFF10 + i: ord("0") + i for i in range(10)},
    **{0xFF21 + i: ord("A") + i for i in range(26)},
    **{0xFF41 + i: ord("a") + i for i in range(26)},
}

SOURCE_WEIGHT = {
    "時事通信": 800,
    "FNN": 800,
    "ITmedia": 600,
    "PC Watch": 600,
    "ナゾロジー": 400,
    "ナタリー": 400,
}
JA_KEYWORDS = ("死亡", "災害", "警報", "選挙", "首相", "速報", "事故")
LONG_BODY_CHARS = 2000
SHOPPING_TITLE = re.compile(r"お買い得|タイムセール|セール(?!ス)|特価|最安値?")

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
SIDEBAR_MARKERS = (
    "関連記事",
    "あわせて読みたい",
    "おすすめ記事",
    "注目の記事",
    "この記事の著者",
    "あわせて読みたい記事",
    "ITmedia Keywords",
    "会員限定",
    "続きを読む",
)
RELATED_HEADLINE = re.compile(r"^【")
METADATA_PATTERNS = (
    re.compile(r"^[\d年月日時分秒・\s]+$"),
    re.compile(r"^(配信|更新|記事提供|公開日|最終更新)"),
    re.compile(r"^写真"),
    re.compile(r"^動画"),
)
CHARSET_PARAM = re.compile(r"charset\s*=\s*[\"']?([A-Za-z0-9_.:-]+)", re.I)
MARKUP_CHARSET = re.compile(
    rb"<meta[^>]+charset\s*=\s*[\"']?\s*([A-Za-z0-9_.:-]+)"
    rb"|<\?xml[^>]+encoding\s*=\s*[\"']([A-Za-z0-9_.:-]+)",
    re.I,
)
SHIFT_JIS_NAMES = {
    "shift-jis",
    "shift_jis",
    "shiftjis",
    "sjis",
    "x-sjis",
    "ms_kanji",
    "windows-31j",
    "cp932",
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
    article_id: str = ""


@dataclass
class SourceStats:
    name: str
    candidates: int = 0
    attempted: int = 0
    skipped_previous: int = 0
    pages_ok: int = 0
    body_ok: int = 0
    written: int = 0
    dropped: dict[str, int] = field(default_factory=dict)

    def drop(self, reason: str) -> None:
        self.dropped[reason] = self.dropped.get(reason, 0) + 1


def edition_id_now(now: datetime | None = None) -> tuple[str, str]:
    ts = now or datetime.now(JST)
    ts = ts.replace(tzinfo=JST) if ts.tzinfo is None else ts.astimezone(JST)
    return ts.strftime("%Y%m%d-%H%M"), ts.strftime("%Y-%m-%dT%H:%M:%S") + "+09:00"


def to_halfwidth(text: str) -> str:
    return text.translate(FULLWIDTH_ASCII)


def sanitize(text: str) -> str:
    """Half-width ASCII, no tabs or newlines: the string is safe for any field."""
    return re.sub(r"[\t\r\n]+", " ", to_halfwidth(str(text))).strip()


def make_lead(paragraphs: list[str], limit: int = LEAD_CHARS) -> str:
    text = "".join(paragraphs)
    if len(text) <= limit:
        return text
    ends = [index + 1 for index, char in enumerate(text) if char in "。！？"]
    before = [end for end in ends if end <= limit]
    if before and before[-1] >= MIN_LEAD_CHARS:
        return text[: before[-1]]
    return text[:limit]


def published_hhmm(published: datetime) -> str:
    if published.tzinfo is None:
        published = published.replace(tzinfo=JST)
    return published.astimezone(JST).strftime("%H:%M")


def compute_score(article: Article, now: datetime) -> float:
    candidate = article.candidate
    score = float(SOURCE_WEIGHT.get(candidate.source, 300))
    score += max(0, 50 - candidate.rank * 5)
    published = candidate.published
    if published.tzinfo is None:
        published = published.replace(tzinfo=JST)
    age_days = (now - published).total_seconds() / 86400
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


def order_articles(articles: list[Article], limit: int = MAX_ARTICLES) -> list[Article]:
    """Importance order: section leads, the technology floor, then score order."""
    ranked = sorted(
        articles,
        key=lambda a: (-a.score, -a.candidate.published.timestamp(), a.candidate.url),
    )
    leads: dict[str, Article] = {}
    for article in ranked:
        leads.setdefault(article.candidate.section, article)
    picked = list(leads.values())
    picked_urls = {article.candidate.url for article in picked}
    tech_count = sum(1 for article in picked if article.candidate.section == "テクノロジー")
    for article in ranked:
        if tech_count >= TECH_MIN:
            break
        if article.candidate.section != "テクノロジー" or article.candidate.url in picked_urls:
            continue
        picked.append(article)
        picked_urls.add(article.candidate.url)
        tech_count += 1
    picked.extend(article for article in ranked if article.candidate.url not in picked_urls)

    counts: dict[str, int] = {}
    mixed: list[Article] = []
    for article in picked:
        section = article.candidate.section
        cap = SECTION_CAPS.get(section)
        if cap is not None and counts.get(section, 0) >= cap:
            continue
        counts[section] = counts.get(section, 0) + 1
        mixed.append(article)
        if len(mixed) >= limit:
            break
    return mixed


def assign_ids(articles: list[Article]) -> None:
    counters: dict[str, int] = {}
    for article in articles:
        key = article.candidate.key
        counters[key] = counters.get(key, 0) + 1
        article.article_id = f"{key}-{counters[key]:03d}"


def prepare_articles(
    articles: list[Article],
    previous_urls: set[str],
    limit: int = MAX_ARTICLES,
) -> list[Article]:
    fresh = [
        article
        for article in articles
        if article.candidate.url not in previous_urls and article.chars >= MIN_BODY_CHARS
    ]
    selected = order_articles(fresh, limit)
    assign_ids(selected)
    return selected


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def atomic_write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(f".{path.name}.tmp-{os.getpid()}")
    try:
        tmp.write_text(text, encoding="utf-8")
        os.replace(tmp, path)
    finally:
        if tmp.exists():
            tmp.unlink()


def write_json(path: Path, payload: object) -> None:
    atomic_write_text(path, json.dumps(payload, ensure_ascii=False, indent=2) + "\n")


def read_json(path: Path) -> object | None:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def article_text(article: Article) -> str:
    candidate = article.candidate
    header = (
        f"{sanitize(candidate.title)}\n"
        f"{sanitize(candidate.source)}\t"
        f"{published_hhmm(candidate.published)}\t"
        f"{sanitize(candidate.url)}\n"
        "\n"
    )
    body = "\n".join(sanitize(paragraph) for paragraph in article.paragraphs)
    return header + body + "\n"


def index_row(article: Article) -> str:
    candidate = article.candidate
    return "\t".join(
        (
            article.article_id,
            sanitize(candidate.section),
            sanitize(candidate.source),
            published_hhmm(candidate.published),
            str(article.chars),
            sanitize(candidate.title),
            sanitize(article.lead),
        )
    )


def write_bundle(root: Path, articles: list[Article], now: datetime | None = None) -> dict[str, object]:
    stamp = now or datetime.now(JST)
    if stamp.tzinfo is None:
        stamp = stamp.replace(tzinfo=JST)
    edition_id, generated_at = edition_id_now(stamp)
    # Two runs inside one minute must not overwrite a published bundle.
    while (root / f"text-{edition_id}").exists():
        stamp += timedelta(minutes=1)
        edition_id, generated_at = edition_id_now(stamp)
    edition_name = f"text-{edition_id}"
    edition_dir = root / edition_name
    staging = root / f".{edition_name}.tmp-{os.getpid()}"

    if staging.exists():
        shutil.rmtree(staging)
    (staging / "articles").mkdir(parents=True)

    try:
        (staging / "edition.tsv").write_text(
            f"id\t{edition_id}\ngenerated\t{generated_at}\n", encoding="utf-8"
        )
        (staging / "index.tsv").write_text(
            "\n".join(index_row(article) for article in articles) + "\n", encoding="utf-8"
        )
        for article in articles:
            (staging / "articles" / f"{article.article_id}.txt").write_text(
                article_text(article), encoding="utf-8"
            )

        files = sorted(
            ["edition.tsv", "index.tsv", "manifest.json"]
            + [f"articles/{article.article_id}.txt" for article in articles]
        )
        manifest = {
            "id": edition_id,
            "generated_at": generated_at,
            "format": FORMAT,
            "article_count": len(articles),
            "files": files,
        }
        (staging / "manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )

        sums = [f"{sha256_file(staging / rel)}  {rel}" for rel in sorted(files)]
        (staging / "SHA256SUMS").write_text("\n".join(sums) + "\n", encoding="utf-8")

        os.replace(staging, edition_dir)
    except BaseException:
        shutil.rmtree(staging, ignore_errors=True)
        raise

    bundle_hash = sha256_file(edition_dir / "SHA256SUMS")
    manifest_hash = sha256_file(edition_dir / "manifest.json")
    latest = {
        "id": edition_id,
        "sha256": bundle_hash,
        "generated_at": generated_at,
        "path": edition_name,
        "format": FORMAT,
        "manifest_sha256": manifest_hash,
    }
    write_json(root / "latest-text.json", latest)
    return latest


def save_state(root: Path, articles: list[Article], latest: dict[str, object]) -> None:
    state = {
        "version": 1,
        "edition_id": latest["id"],
        "generated_at": latest["generated_at"],
        "urls": sorted(article.candidate.url for article in articles),
    }
    write_json(root / "text-state.json", state)


def urls_from_edition(edition_dir: Path) -> set[str]:
    urls: set[str] = set()
    manifest = read_json(edition_dir / "manifest.json")
    rels: list[str] = []
    if isinstance(manifest, dict) and isinstance(manifest.get("files"), list):
        rels = [str(rel) for rel in manifest["files"] if str(rel).endswith(".txt")]
    if not rels and (edition_dir / "articles").is_dir():
        rels = [f"articles/{path.name}" for path in sorted((edition_dir / "articles").glob("*.txt"))]
    for rel in rels:
        try:
            lines = (edition_dir / rel).read_text(encoding="utf-8").splitlines()
        except OSError:
            continue
        if len(lines) < 2:
            continue
        parts = lines[1].split("\t")
        if len(parts) >= 3 and parts[2]:
            urls.add(parts[2])
    return urls


def load_previous_urls(root: Path) -> set[str]:
    """URLs of the last published edition, reconciled from disk if state is stale."""
    latest = read_json(root / "latest-text.json")
    if not isinstance(latest, dict) or not latest.get("id"):
        return set()
    state = read_json(root / "text-state.json")
    if (
        isinstance(state, dict)
        and state.get("edition_id") == latest["id"]
        and isinstance(state.get("urls"), list)
    ):
        return {str(url) for url in state["urls"] if url}
    if latest.get("path"):
        return urls_from_edition(root / str(latest["path"]))
    return set()


def list_text_editions(root: Path) -> list[Path]:
    if not root.is_dir():
        return []
    dirs = [
        child
        for child in root.iterdir()
        if child.is_dir() and not child.is_symlink() and TEXT_DIR_RE.match(child.name)
    ]
    return sorted(dirs, key=lambda path: path.name, reverse=True)


def prune_editions(root: Path, current: Path, keep: int = KEEP_EDITIONS) -> list[str]:
    keep_set = {current.resolve()}
    removed: list[str] = []
    for edition_dir in list_text_editions(root):
        if edition_dir.resolve() in keep_set:
            continue
        if len(keep_set) >= keep:
            removed.append(edition_dir.name)
            continue
        keep_set.add(edition_dir.resolve())
    for name in removed:
        shutil.rmtree(root / name, ignore_errors=True)
    return removed


def publish_edition(
    root: Path,
    articles: list[Article],
    now: datetime | None = None,
    *,
    previous_urls: set[str] | None = None,
    limit: int = MAX_ARTICLES,
) -> dict[str, object]:
    root.mkdir(parents=True, exist_ok=True)
    previous = load_previous_urls(root) if previous_urls is None else set(previous_urls)
    selected = prepare_articles(articles, previous, limit)
    if not selected:
        return {"empty_refused": True, "error": "no new article with a full body"}

    latest = write_bundle(root, selected, now)
    save_state(root, selected, latest)
    removed = prune_editions(root, root / str(latest["path"]))
    return {
        "empty_refused": False,
        "edition_id": latest["id"],
        "path": latest["path"],
        "article_count": len(selected),
        "sha256": latest["sha256"],
        "urls": [article.candidate.url for article in selected],
        "removed_editions": removed,
    }


def verify_edition(edition_dir: Path) -> list[str]:
    problems: list[str] = []

    def problem(message: str) -> None:
        problems.append(message)

    edition_file = edition_dir / "edition.tsv"
    index_file = edition_dir / "index.tsv"
    manifest_file = edition_dir / "manifest.json"
    sums_file = edition_dir / "SHA256SUMS"
    for path in (edition_file, index_file, manifest_file, sums_file):
        if not path.is_file():
            problem(f"missing {path.name}")
    if problems:
        return problems

    edition_lines = edition_file.read_text(encoding="utf-8").splitlines()
    if len(edition_lines) != 2:
        problem(f"edition.tsv has {len(edition_lines)} lines, want 2")
    else:
        if not edition_lines[0].startswith("id\t"):
            problem("edition.tsv first line is not id")
        if not edition_lines[1].startswith("generated\t"):
            problem("edition.tsv second line is not generated")
        if not edition_lines[1].endswith("+09:00"):
            problem("edition.tsv generated is not +09:00")

    index_rows: list[list[str]] = []
    index_lines = index_file.read_text(encoding="utf-8").splitlines()
    for line_no, line in enumerate(index_lines, start=1):
        if not line:
            problem(f"index.tsv line {line_no} is empty")
            continue
        parts = line.split("\t")
        if len(parts) != 7:
            problem(f"index.tsv line {line_no} has {len(parts)} columns, want 7")
            continue
        article_id, _section, _source, published, chars, _title, _lead = parts
        if not PUBLISHED_RE.match(published):
            problem(f"index.tsv line {line_no} published is not HH:MM: {published!r}")
        if not chars.isdigit():
            problem(f"index.tsv line {line_no} chars is not an integer: {chars!r}")
        elif int(chars) < MIN_BODY_CHARS:
            problem(f"index.tsv line {line_no} chars is below {MIN_BODY_CHARS}")
        if len(parts[6]) > LEAD_CHARS:
            problem(f"index.tsv line {line_no} lead is longer than {LEAD_CHARS}")
        index_rows.append(parts)

    if not index_rows:
        problem("index.tsv has no articles")

    seen_ids: set[str] = set()
    article_ids = [row[0] for row in index_rows]
    for article_id in article_ids:
        if article_id in seen_ids:
            problem(f"duplicate article id {article_id}")
        seen_ids.add(article_id)

    for article_id in article_ids:
        path = edition_dir / "articles" / f"{article_id}.txt"
        if not path.is_file():
            problem(f"missing articles/{article_id}.txt")
            continue
        lines = path.read_text(encoding="utf-8").splitlines()
        if len(lines) < 3:
            problem(f"{path.name} has no body")
            continue
        if lines[2] != "":
            problem(f"{path.name} third line is not empty")
        header = lines[1].split("\t")
        if len(header) != 3:
            problem(f"{path.name} header has {len(header)} columns, want 3")
        body = "\n".join(lines[3:])
        if len(body) < MIN_BODY_CHARS:
            problem(f"{path.name} body is {len(body)} chars, below {MIN_BODY_CHARS}")

    manifest = read_json(manifest_file)
    if not isinstance(manifest, dict):
        problem("manifest.json is not an object")
    else:
        if manifest.get("format") != FORMAT:
            problem(f"manifest format is {manifest.get('format')!r}, want {FORMAT}")
        if manifest.get("article_count") != len(index_rows):
            problem("manifest article_count does not match index.tsv")
        if edition_lines and manifest.get("id") != edition_lines[0].split("\t", 1)[-1]:
            problem("manifest id does not match edition.tsv")
        if not isinstance(manifest.get("files"), list):
            problem("manifest files is not a list")
        else:
            listed = {str(rel) for rel in manifest["files"]}
            actual = {
                str(path.relative_to(edition_dir))
                for path in edition_dir.rglob("*")
                if path.is_file() and path.name != "SHA256SUMS"
            }
            if listed != actual:
                problem(f"manifest files mismatch: listed {sorted(listed)}, actual {sorted(actual)}")

    sums_ok = True
    summed: set[str] = set()
    for line_no, line in enumerate(sums_file.read_text(encoding="utf-8").splitlines(), start=1):
        if not line:
            continue
        parts = line.split("  ", 1)
        if len(parts) != 2:
            problem(f"SHA256SUMS line {line_no} is malformed")
            sums_ok = False
            continue
        expected, rel = parts
        summed.add(rel)
        target = edition_dir / rel
        if not target.is_file():
            problem(f"SHA256SUMS references missing {rel}")
            sums_ok = False
        elif sha256_file(target) != expected:
            problem(f"SHA256SUMS hash mismatch for {rel}")
            sums_ok = False
    if isinstance(manifest, dict) and isinstance(manifest.get("files"), list):
        if summed != {str(rel) for rel in manifest["files"]}:
            problem("SHA256SUMS does not cover exactly the manifest files")
            sums_ok = False
    if sums_ok:
        latest = read_json(edition_dir.parent / "latest-text.json")
        if isinstance(latest, dict) and latest.get("path") == edition_dir.name:
            if latest.get("sha256") != sha256_file(sums_file):
                problem("latest-text.json sha256 does not match SHA256SUMS")
            if isinstance(manifest, dict) and latest.get("id") != manifest.get("id"):
                problem("latest-text.json id does not match manifest id")

    for path in sorted(edition_dir.rglob("*")):
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        bad = sorted({char for char in text if ord(char) in FULLWIDTH_ASCII})
        if bad:
            problem(f"{path.relative_to(edition_dir)} has full-width ASCII: {''.join(bad)}")

    return problems


def normalize_charset(name: str) -> str:
    key = name.strip().strip("\"'").lower()
    if key in SHIFT_JIS_NAMES:
        return "cp932"
    return name.strip().strip("\"'") or "utf-8"


def response_text(response: Any) -> str:
    """Decode by declared charset, then markup metadata, then chardet's guess."""
    encoding = ""
    match = CHARSET_PARAM.search(response.headers.get("Content-Type", ""))
    if match:
        encoding = match.group(1)
    if not encoding:
        match = MARKUP_CHARSET.search(response.content[:8192])
        if match:
            encoding = (match.group(1) or match.group(2) or b"").decode("ascii", "ignore")
    if not encoding:
        encoding = response.apparent_encoding or "utf-8"
    try:
        return response.content.decode(normalize_charset(encoding), errors="replace")
    except LookupError:
        return response.content.decode("utf-8", errors="replace")


class Fetcher:
    def __init__(self) -> None:
        import requests

        self._requests = requests
        self.session = requests.Session()
        self.session.headers["User-Agent"] = USER_AGENT
        self.robots: dict[str, urllib.robotparser.RobotFileParser] = {}
        self.delays: dict[str, float] = {}
        self.last_hit: dict[str, float] = {}
        self.requests_by_source: dict[str, int] = {}

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
        try:
            response = self.session.get(url, timeout=20)
        except self._requests.RequestException:
            return None
        if response.status_code != 200:
            return None
        return response_text(response)


def feed_datetime(entry: object) -> datetime:
    get = getattr(entry, "get")
    stamp = get("published_parsed") or get("updated_parsed")
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
        try:
            root = ET.fromstring(text)
        except ET.ParseError:
            continue
        rows: list[tuple[datetime, str, str]] = []
        for node in root.findall("sm:url", NS):
            link = node.findtext("sm:loc", namespaces=NS)
            published = node.findtext("news:news/news:publication_date", namespaces=NS)
            title = node.findtext("news:news/news:title", namespaces=NS)
            if not link or not published or not title:
                continue
            try:
                stamp = datetime.fromisoformat(published)
            except ValueError:
                continue
            rows.append((stamp, title, link))
        rows.sort(key=lambda row: row[0], reverse=True)
        for rank, (published, title, link) in enumerate(rows[:JIJI_PER_SECTION]):
            out.append(Candidate("時事通信", "jiji", section, title, link, published, rank))
    return out


def collect_fnn(fetcher: Fetcher) -> list[Candidate]:
    import feedparser

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


TITLE_SEPARATOR_CHARS = "-－‐‑‒–—―−|｜"


def strip_source_suffix(title: str, source: str) -> str:
    """Drop the feed's trailing " - Source" / " | Source" from a title."""
    source = source.strip()
    if not source:
        return title
    pattern = re.compile(
        rf"\s*[{re.escape(TITLE_SEPARATOR_CHARS)}]\s*{re.escape(source)}\s*$"
    )
    match = pattern.search(title)
    if not match:
        return title
    stripped = title[: match.start()].strip()
    return stripped or title


def feed_candidates(
    fetcher: Fetcher,
    source: str,
    key: str,
    section: str,
    url: str,
    *,
    limit: int = FEED_LIMIT,
    skip_paths: tuple[str, ...] = (),
) -> list[Candidate]:
    import feedparser

    text = fetcher.get(url, source)
    if not text:
        return []
    parsed = feedparser.parse(text)
    out: list[Candidate] = []
    for rank, entry in enumerate(parsed.entries):
        if len(out) >= limit:
            break
        link = (entry.get("link") or "").strip()
        title = strip_source_suffix((entry.get("title") or "").strip(), source)
        if not link or not title:
            continue
        if any(path in urlsplit(link).path for path in skip_paths):
            continue
        out.append(Candidate(source, key, section, title, link, feed_datetime(entry), rank))
    return out


def collect_itmedia(fetcher: Fetcher) -> list[Candidate]:
    return feed_candidates(
        fetcher,
        "ITmedia",
        "itm",
        "テクノロジー",
        ITMEDIA_FEED,
        limit=ITMEDIA_LIMIT,
        skip_paths=ITMEDIA_EXCLUDED_PATHS,
    )


def collect_pcwatch(fetcher: Fetcher) -> list[Candidate]:
    return feed_candidates(fetcher, "PC Watch", "pcw", "テクノロジー", PCWATCH_FEED)


def collect_nazology(fetcher: Fetcher) -> list[Candidate]:
    return feed_candidates(fetcher, "ナゾロジー", "nazo", "科学", NAZOLOGY_FEED)


def collect_natalie(fetcher: Fetcher) -> list[Candidate]:
    return feed_candidates(fetcher, "ナタリー", "nat", "文化", NATALIE_FEED)


def text_with_breaks(node: Any) -> str:
    """Text of a node with <br> kept as a newline; text_content() drops it."""
    parts: list[str] = []

    def walk(element: Any) -> None:
        if element.tag == "br":
            parts.append("\n")
        if element.text:
            parts.append(element.text)
        for child in element:
            if isinstance(child.tag, str):
                walk(child)
            if child.tail:
                parts.append(child.tail)

    walk(node)
    return "".join(parts)


def nodes_to_paragraphs(nodes: list[object]) -> list[str]:
    paragraphs: list[str] = []
    for node in nodes:
        for chunk in text_with_breaks(node).split("\n"):
            text = re.sub(r"[\t\r\n]+", " ", chunk).strip(" \u3000")
            if text:
                paragraphs.append(text)
    return paragraphs


def trafilatura_paragraphs(html: str) -> list[str]:
    import trafilatura

    text = trafilatura.extract(
        html,
        include_comments=False,
        include_tables=False,
        favor_precision=True,
    )
    return [line.strip() for line in (text or "").splitlines() if line.strip()]


def extract_jiji(html: str) -> list[str]:
    import lxml.html

    doc = lxml.html.fromstring(html)
    nodes = doc.xpath(
        '//div[contains(concat(" ", normalize-space(@class), " "), " ArticleText ")]'
        "//p[not(ancestor::figcaption)"
        ' and not(contains(concat(" ", normalize-space(@class), " "), " ArticleTextTab "))]'
    )
    return nodes_to_paragraphs(nodes)


def extract_fnn(html: str) -> list[str]:
    import lxml.html

    doc = lxml.html.fromstring(html)
    best: list[str] = []
    for div in doc.xpath('//div[normalize-space(@class)="article-body"]'):
        paragraphs = nodes_to_paragraphs(div.xpath(".//p[not(ancestor::figcaption)]"))
        if sum(len(p) for p in paragraphs) > sum(len(p) for p in best):
            best = paragraphs
    return best


def strip_metadata_lines(text: str) -> str:
    lines: list[str] = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if any(pattern.search(line) for pattern in METADATA_PATTERNS):
            continue
        if len(line) < 8 and re.search(r"\d{1,2}[:：]\d{2}", line):
            continue
        lines.append(line)
    return "\n".join(lines)


def clean_itmedia_body(text: str) -> str:
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    kept: list[str] = []
    for line in lines:
        if any(marker in line for marker in SIDEBAR_MARKERS):
            break
        if RELATED_HEADLINE.match(line) and kept:
            break
        kept.append(line)
    return "\n".join(kept).strip()


def has_ancestor(node: Any, *, element_id: str = "", class_marker: str = "") -> bool:
    parent = node.getparent()
    while parent is not None:
        if element_id and parent.get("id") == element_id:
            return True
        if class_marker:
            classes = f" {(parent.get('class') or '')} "
            if f" {class_marker} " in classes:
                return True
        parent = parent.getparent()
    return False


def xpath_paragraphs(html: str, xpath: str) -> list[str]:
    import lxml.html

    doc = lxml.html.fromstring(html)
    return nodes_to_paragraphs(doc.xpath(xpath))


def extract_itmedia_master(html: str) -> list[str]:
    import lxml.html

    doc = lxml.html.fromstring(html)
    nodes = doc.xpath('//div[@id="masterBody"]//p')
    body = [
        node
        for node in nodes
        if has_ancestor(node, element_id="cmsBody")
        and not has_ancestor(node, element_id="cmsCopyright")
    ]
    if not body:
        return []
    cleaned = clean_itmedia_body(strip_metadata_lines("\n".join(nodes_to_paragraphs(body))))
    return [line.strip() for line in cleaned.splitlines() if line.strip()]


def extract_itmedia(html: str) -> list[str]:
    master = extract_itmedia_master(html)
    if sum(len(paragraph) for paragraph in master) >= MIN_BODY_CHARS:
        return master
    cleaned = clean_itmedia_body(strip_metadata_lines("\n".join(trafilatura_paragraphs(html))))
    return [line.strip() for line in cleaned.splitlines() if line.strip()]


NAZOLOGY_BODY_CLASSES = ("post-lead__contents", "post-body__contents")
NAZOLOGY_STOP_MARKERS = ("関連記事", "あわせて読みたい", "人気記事", "この記事をシェア")


def extract_nazology(html: str) -> list[str]:
    import lxml.html

    doc = lxml.html.fromstring(html)
    nodes = [
        node
        for node in doc.xpath("//article//p")
        if any(has_ancestor(node, class_marker=marker) for marker in NAZOLOGY_BODY_CLASSES)
    ]
    kept: list[str] = []
    for paragraph in nodes_to_paragraphs(nodes):
        if any(marker in paragraph for marker in NAZOLOGY_STOP_MARKERS):
            break
        kept.append(paragraph)
    return kept


PCWATCH_BODY_XPATH = (
    '//div[contains(concat(" ", normalize-space(@class), " "), " mainContents ")]//p'
)


def extract_pcwatch(html: str) -> list[str]:
    return xpath_paragraphs(html, PCWATCH_BODY_XPATH)


NATALIE_BODY_XPATH = (
    '//*[contains(concat(" ", normalize-space(@class), " "), " NA_article_body ")]'
)
NATALIE_NOISE = re.compile(r"^(［?高画質で見る］?|画像ギャラリー（全\d+件）|タグ)$")
NATALIE_STOP_MARKERS = (
    "関連する特集・インタビュー",
    "この記事の画像",
    "ナタリーを優先ソース",
    "フォローして最新ニュース",
)


def extract_natalie(html: str) -> list[str]:
    import lxml.html

    doc = lxml.html.fromstring(html)
    kept: list[str] = []
    for body in doc.xpath(NATALIE_BODY_XPATH):
        for paragraph in nodes_to_paragraphs(body.xpath(".//p")):
            paragraph = re.sub(r"\s*［高画質で見る］\s*$", "", paragraph).strip()
            if not paragraph or NATALIE_NOISE.match(paragraph):
                continue
            if any(marker in paragraph for marker in NATALIE_STOP_MARKERS):
                return kept
            kept.append(paragraph)
    return kept


def strip_title_and_footer(paragraphs: list[str], title: str) -> list[str]:
    normalized_title = re.sub(r"\s+", "", title)
    kept: list[str] = []
    seen: set[str] = set()
    for paragraph in paragraphs:
        paragraph = re.sub(r"（\d+/\d+ ページ）$", "", paragraph).strip()
        normalized = re.sub(r"\s+", "", paragraph)
        if (
            normalized_title
            and normalized.startswith(normalized_title)
            and len(normalized) <= len(normalized_title) + 12
        ):
            continue
        if paragraph.startswith("Copyright ©"):
            continue
        if len(paragraph) > 20 and paragraph in seen:
            continue
        seen.add(paragraph)
        kept.append(paragraph)
    return kept


EXTRACTORS = {
    "jiji": extract_jiji,
    "fnn": extract_fnn,
    "itm": extract_itmedia,
    "pcw": extract_pcwatch,
    "nazo": extract_nazology,
    "nat": extract_natalie,
}


def itmedia_next_page(html: str, current_url: str) -> str | None:
    import lxml.html

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
    except Exception:
        paragraphs = []
    if candidate.key == "itm":
        visited = {candidate.url}
        try:
            next_url = itmedia_next_page(html, candidate.url)
        except Exception:
            next_url = None
        while next_url and next_url not in visited and len(visited) < MAX_ITMEDIA_PAGES:
            visited.add(next_url)
            try:
                page = fetcher.get(next_url, candidate.source)
            except RobotsDenied:
                break
            if not page:
                break
            try:
                paragraphs.extend(extract_itmedia(page))
                next_url = itmedia_next_page(page, next_url)
            except Exception:
                break
    if sum(len(p) for p in paragraphs) < MIN_BODY_CHARS:
        try:
            fallback = trafilatura_paragraphs(html)
        except Exception:
            fallback = []
        if sum(len(p) for p in fallback) > sum(len(p) for p in paragraphs):
            paragraphs = fallback
    return strip_title_and_footer(paragraphs, candidate.title), None


def truncation_marker(paragraphs: list[str]) -> str:
    for marker in ("続きを読む", "会員限定"):
        if any(marker in paragraph for paragraph in paragraphs):
            return marker
    if paragraphs and "有料" in paragraphs[-1]:
        return "有料"
    return ""


def harvest(
    fetcher: Fetcher,
    candidates: list[Candidate],
    previous_urls: set[str],
    now: datetime,
) -> tuple[list[Article], dict[str, SourceStats]]:
    stats: dict[str, SourceStats] = {}
    for candidate in candidates:
        entry = stats.setdefault(candidate.source, SourceStats(candidate.source))
        entry.candidates += 1

    seen: set[str] = set(previous_urls)
    articles: list[Article] = []
    for candidate in candidates:
        entry = stats[candidate.source]
        if candidate.url in seen:
            entry.skipped_previous += 1
            continue
        seen.add(candidate.url)
        entry.attempted += 1
        try:
            paragraphs, error = fetch_paragraphs(fetcher, candidate)
        except Exception:
            paragraphs, error = None, "error"
        if error:
            entry.drop(error)
            continue
        entry.pages_ok += 1
        assert paragraphs is not None
        paragraphs = [sanitize(paragraph) for paragraph in paragraphs]
        paragraphs = [paragraph for paragraph in paragraphs if paragraph]
        if not paragraphs:
            entry.drop("empty")
            continue
        chars = len("\n".join(paragraphs))
        if chars < MIN_BODY_CHARS:
            entry.drop("short")
            continue
        marker = truncation_marker(paragraphs)
        if marker:
            entry.drop(f"truncated:{marker}")
            continue
        entry.body_ok += 1
        article = Article(candidate, paragraphs, chars, make_lead(paragraphs))
        article.score = compute_score(article, now)
        articles.append(article)
    return dedupe_by_title(articles, stats), stats


def dedupe_by_title(articles: list[Article], stats: dict[str, SourceStats]) -> list[Article]:
    unique: list[Article] = []
    seen: set[str] = set()
    for article in articles:
        normalized = re.sub(r"\s+", "", article.candidate.title)
        if normalized in seen:
            stats[article.candidate.source].drop("duplicate_title")
            continue
        seen.add(normalized)
        unique.append(article)
    return unique


def is_shopping_title(title: str) -> bool:
    return SHOPPING_TITLE.search(title) is not None


def collect_all(fetcher: Fetcher) -> tuple[list[Candidate], dict[str, str]]:
    errors: dict[str, str] = {}
    candidates: list[Candidate] = []
    for name, collector in (
        ("時事通信", collect_jiji),
        ("FNN", collect_fnn),
        ("ITmedia", collect_itmedia),
        ("PC Watch", collect_pcwatch),
        ("ナゾロジー", collect_nazology),
        ("ナタリー", collect_natalie),
    ):
        try:
            items = collector(fetcher)
        except Exception as exc:
            errors[name] = f"{type(exc).__name__}: {exc}"
            continue
        kept = [item for item in items if not is_shopping_title(item.title)]
        if not kept:
            errors[name] = "no candidates"
        candidates.extend(kept)
    return candidates, errors


def summary_json(
    summary: dict[str, object], stats: dict[str, SourceStats], errors: dict[str, str]
) -> dict[str, object]:
    return {
        **summary,
        "sources": {
            name: {
                "candidates": entry.candidates,
                "attempted": entry.attempted,
                "skipped_previous": entry.skipped_previous,
                "pages_ok": entry.pages_ok,
                "body_ok": entry.body_ok,
                "written": entry.written,
                "dropped": entry.dropped,
            }
            for name, entry in stats.items()
        },
        "source_errors": errors,
    }


def run(root: Path, limit: int = MAX_ARTICLES) -> int:
    started = time.monotonic()
    now = datetime.now(JST)
    previous = load_previous_urls(root)
    fetcher = Fetcher()
    candidates, errors = collect_all(fetcher)
    articles, stats = harvest(fetcher, candidates, previous, now)
    if not articles:
        print(
            json.dumps(
                summary_json({"empty_refused": True, "error": "no article with a full body"}, stats, errors),
                ensure_ascii=False,
                indent=2,
            )
        )
        return 1

    summary = publish_edition(root, articles, previous_urls=previous, limit=limit)
    if summary.get("empty_refused"):
        print(json.dumps(summary_json(summary, stats, errors), ensure_ascii=False, indent=2))
        return 1

    selected_urls = set(summary["urls"])  # type: ignore[arg-type]
    for entry in stats.values():
        entry.written = sum(
            1
            for article in articles
            if article.candidate.source == entry.name and article.candidate.url in selected_urls
        )
    summary["elapsed_seconds"] = round(time.monotonic() - started, 1)
    summary["requests"] = dict(fetcher.requests_by_source)
    print(json.dumps(summary_json(summary, stats, errors), ensure_ascii=False, indent=2))
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Build a text-v1 Kindle Times edition.")
    parser.add_argument(
        "--root",
        type=Path,
        default=DEFAULT_ROOT,
        help="Edition root (default: out/editions)",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=MAX_ARTICLES,
        help=f"Maximum articles per edition (default: {MAX_ARTICLES})",
    )
    parser.add_argument("--verify", type=Path, help="Check an existing text-<id> directory and exit")
    args = parser.parse_args(argv)

    if args.verify is not None:
        problems = verify_edition(args.verify)
        print(json.dumps({"ok": not problems, "problems": problems}, ensure_ascii=False, indent=2))
        return 0 if not problems else 1

    root = args.root.expanduser()
    if args.limit < 1:
        parser.error("--limit must be positive")
    return run(root, args.limit)


if __name__ == "__main__":
    raise SystemExit(main())
