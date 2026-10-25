"""Tests for the text-v1 edition: contract, dedupe, pruning, verification."""

from __future__ import annotations

import json
import subprocess
import sys
from datetime import datetime, timedelta
from pathlib import Path
from zoneinfo import ZoneInfo

import pytest

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS))

import text_edition as te  # noqa: E402

JST = ZoneInfo("Asia/Tokyo")
NOW = datetime(2026, 10, 24, 18, 0, tzinfo=JST)
BODY = "２０２６年１０月２４日、これはテスト記事の本文です。" * 12


def make_article(
    number: int,
    *,
    section: str = "政治",
    source: str = "時事通信",
    key: str = "jiji",
    body: str = BODY,
    title: str | None = None,
    score: float = 0.0,
) -> te.Article:
    candidate = te.Candidate(
        source,
        key,
        section,
        title or f"見出し{number}",
        f"https://example.com/{key}/{number}",
        NOW - timedelta(hours=number),
        number,
    )
    paragraphs = [te.sanitize(part) for part in body.split("\n") if te.sanitize(part)]
    chars = len("\n".join(paragraphs))
    return te.Article(candidate, paragraphs, chars, te.make_lead(paragraphs), score)


def edition_dir(root: Path, summary: dict[str, object]) -> Path:
    return root / str(summary["path"])


def latest_bytes(root: Path) -> bytes:
    return (root / "latest-text.json").read_bytes()


def test_bundle_contract_and_shasum(tmp_path: Path) -> None:
    root = tmp_path / "editions"
    articles = [
        make_article(1, section="政治", score=120),
        make_article(2, section="経済", score=110),
        make_article(
            3,
            section="国際",
            body="最初の段落です。\n二つ目の段落です。\n" + BODY,
            score=100,
        ),
    ]
    summary = te.publish_edition(root, articles, NOW)
    assert summary["empty_refused"] is False
    folder = edition_dir(root, summary)

    edition = (folder / "edition.tsv").read_text(encoding="utf-8").splitlines()
    assert edition == ["id\t20261024-1800", "generated\t2026-10-24T18:00:00+09:00"]

    rows = (folder / "index.tsv").read_text(encoding="utf-8").splitlines()
    assert len(rows) == 3
    for row in rows:
        parts = row.split("\t")
        assert len(parts) == 7
        assert te.PUBLISHED_RE.match(parts[3])
        assert int(parts[4]) >= te.MIN_BODY_CHARS
        assert (folder / "articles" / f"{parts[0]}.txt").is_file()

    first_id = rows[0].split("\t")[0]
    article_lines = (folder / "articles" / f"{first_id}.txt").read_text(encoding="utf-8").splitlines()
    assert article_lines[0] == "見出し1"
    assert article_lines[1].split("\t") == ["時事通信", "17:00", "https://example.com/jiji/1"]
    assert article_lines[2] == ""
    assert len(article_lines) >= 4

    multiparagraph = (folder / "articles" / "jiji-003.txt").read_text(encoding="utf-8").splitlines()
    assert multiparagraph[3] == "最初の段落です。"
    assert multiparagraph[4] == "二つ目の段落です。"
    assert len(multiparagraph) == 6

    manifest = json.loads((folder / "manifest.json").read_text(encoding="utf-8"))
    assert manifest["format"] == "text-v1"
    assert manifest["article_count"] == 3
    assert manifest["id"] == "20261024-1800"
    assert manifest["generated_at"] == "2026-10-24T18:00:00+09:00"
    assert set(manifest["files"]) == {
        "edition.tsv",
        "index.tsv",
        "manifest.json",
        "articles/jiji-001.txt",
        "articles/jiji-002.txt",
        "articles/jiji-003.txt",
    }

    latest = json.loads((root / "latest-text.json").read_text(encoding="utf-8"))
    assert latest["id"] == "20261024-1800"
    assert latest["path"] == "text-20261024-1800"
    assert latest["format"] == "text-v1"
    assert latest["sha256"] == te.sha256_file(folder / "SHA256SUMS")
    assert latest["manifest_sha256"] == te.sha256_file(folder / "manifest.json")

    assert te.verify_edition(folder) == []

    checked = subprocess.run(
        ["shasum", "-a", "256", "-c", "SHA256SUMS"],
        cwd=folder,
        capture_output=True,
        text=True,
    )
    assert checked.returncode == 0, checked.stderr
    assert checked.stdout.count(": OK") == 6

    for path in folder.rglob("*"):
        if path.is_file():
            text = path.read_text(encoding="utf-8")
            assert not any(ord(char) in te.FULLWIDTH_ASCII for char in text), path


def test_fullwidth_tabs_and_newlines_are_cleaned(tmp_path: Path) -> None:
    root = tmp_path / "editions"
    candidate = te.Candidate(
        "ＦＮＮ",
        "fnn",
        "社会",
        "ＡＢＣ１２３\t見出し\n二行目",
        "https://example.com/fnn/1",
        NOW,
        0,
    )
    paragraphs = ["本文\tにタブ\nと改行。", "２つ目の段落。" + BODY]
    article = te.Article(candidate, paragraphs, 300, "Ｌｅａｄ１２３", 10)
    summary = te.publish_edition(root, [article], NOW)
    folder = edition_dir(root, summary)

    index_line = (folder / "index.tsv").read_text(encoding="utf-8").splitlines()[0]
    fields = index_line.split("\t")
    assert len(fields) == 7
    assert fields[1] == "社会"
    assert fields[2] == "FNN"
    assert fields[5] == "ABC123 見出し 二行目"
    assert fields[6] == "Lead123"
    assert "\n" not in index_line

    article_text = (folder / "articles" / "fnn-001.txt").read_text(encoding="utf-8")
    lines = article_text.splitlines()
    assert lines[0] == "ABC123 見出し 二行目"
    assert lines[3] == "本文 にタブ と改行。"
    assert not any(ord(char) in te.FULLWIDTH_ASCII for char in article_text)
    assert len(lines[3].split("\t")) == 1


def test_normalization_leaves_japanese_punctuation_alone() -> None:
    assert te.sanitize("（ＡＢＣ１２３）．、。ー") == "（ABC123）．、。ー"


def test_previous_edition_urls_are_excluded(tmp_path: Path) -> None:
    root = tmp_path / "editions"
    first = [make_article(1), make_article(2)]
    te.publish_edition(root, first, NOW)
    first_urls = {article.candidate.url for article in first}

    second = [make_article(1), make_article(2), make_article(3)]
    summary2 = te.publish_edition(root, second, NOW + timedelta(minutes=10))
    folder2 = edition_dir(root, summary2)
    rows2 = (folder2 / "index.tsv").read_text(encoding="utf-8").splitlines()

    assert summary2["article_count"] == 1
    assert summary2["urls"] == ["https://example.com/jiji/3"]
    assert len(rows2) == 1
    for url in first_urls:
        for row in rows2:
            assert url not in row

    state = json.loads((root / "text-state.json").read_text(encoding="utf-8"))
    assert state["edition_id"] == "20261024-1810"
    assert state["urls"] == ["https://example.com/jiji/3"]


def test_same_input_twice_does_not_corrupt_latest_or_state(tmp_path: Path) -> None:
    root = tmp_path / "editions"
    articles = [make_article(1), make_article(2)]
    summary1 = te.publish_edition(root, articles, NOW)
    folder1 = edition_dir(root, summary1)
    latest_before = latest_bytes(root)
    state_before = (root / "text-state.json").read_bytes()
    index_before = (folder1 / "index.tsv").read_bytes()

    summary2 = te.publish_edition(root, articles, NOW + timedelta(minutes=5))

    assert summary2["empty_refused"] is True
    assert latest_bytes(root) == latest_before
    assert (root / "text-state.json").read_bytes() == state_before
    assert (folder1 / "index.tsv").read_bytes() == index_before
    assert te.verify_edition(folder1) == []


def test_second_run_in_the_same_minute_gets_its_own_bundle(tmp_path: Path) -> None:
    root = tmp_path / "editions"
    te.publish_edition(root, [make_article(1)], NOW)
    summary = te.publish_edition(root, [make_article(1), make_article(2)], NOW)

    assert summary["edition_id"] == "20261024-1801"
    assert summary["article_count"] == 1
    assert (root / "text-20261024-1800").is_dir()
    assert (root / "text-20261024-1801").is_dir()
    assert te.verify_edition(root / "text-20261024-1801") == []


def test_state_is_reconciled_from_the_published_edition(tmp_path: Path) -> None:
    root = tmp_path / "editions"
    articles = [make_article(1), make_article(2)]
    te.publish_edition(root, articles, NOW)
    (root / "text-state.json").unlink()

    summary = te.publish_edition(root, articles, NOW + timedelta(minutes=5))
    assert summary["empty_refused"] is True


def test_section_leads_come_first_and_limit_holds(tmp_path: Path) -> None:
    root = tmp_path / "editions"
    sections = ["政治", "経済", "国際", "社会"]
    articles = []
    for index in range(35):
        articles.append(
            make_article(
                index,
                section=sections[index % len(sections)],
                score=1000 - index,
            )
        )
    summary = te.publish_edition(root, articles, NOW)
    rows = (edition_dir(root, summary) / "index.tsv").read_text(encoding="utf-8").splitlines()
    assert len(rows) == te.MAX_ARTICLES
    assert [row.split("\t")[1] for row in rows[:4]] == sections
    assert len({row.split("\t")[0] for row in rows}) == te.MAX_ARTICLES


def test_short_body_is_not_published(tmp_path: Path) -> None:
    root = tmp_path / "editions"
    short = make_article(1, body="短い本文。" * 15)
    assert short.chars < te.MIN_BODY_CHARS
    summary = te.publish_edition(root, [short], NOW)
    assert summary["empty_refused"] is True
    assert not (root / "latest-text.json").exists()


def test_prune_keeps_two_text_editions_and_leaves_others(tmp_path: Path) -> None:
    root = tmp_path / "editions"
    root.mkdir()
    old = root / "text-20260101-0000"
    previous = root / "text-20260102-0000"
    stray = root / "text-notes"
    for folder in (old, previous, stray):
        folder.mkdir()
        (folder / "keep.txt").write_text("x", encoding="utf-8")

    summary = te.publish_edition(root, [make_article(1)], NOW)

    assert not old.exists()
    assert previous.exists()
    assert stray.exists()
    names = sorted(path.name for path in root.iterdir() if path.is_dir())
    assert names.count("text-20261024-1800") == 1
    assert len([name for name in names if name.startswith("text-20")]) == 2
    assert te.verify_edition(edition_dir(root, summary)) == []


def test_one_failed_source_does_not_stop_the_others(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    candidate = make_article(1).candidate

    def boom(fetcher: object) -> list[te.Candidate]:
        raise RuntimeError("blocked")

    monkeypatch.setattr(te, "collect_jiji", boom)
    monkeypatch.setattr(te, "collect_fnn", lambda fetcher: [candidate])
    monkeypatch.setattr(te, "collect_itmedia", lambda fetcher: [])

    candidates, errors = te.collect_all(object())

    assert candidates == [candidate]
    assert errors["時事通信"] == "RuntimeError: blocked"
    assert errors["ITmedia"] == "no candidates"
    assert "FNN" not in errors


def test_all_sources_failing_leaves_latest_untouched(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    root = tmp_path / "editions"
    te.publish_edition(root, [make_article(1)], NOW)
    before = latest_bytes(root)

    monkeypatch.setattr(te, "collect_all", lambda fetcher: ([], {"時事通信": "boom"}))
    assert te.run(root) == 1
    assert latest_bytes(root) == before


def test_lead_stops_at_a_sentence_boundary() -> None:
    paragraphs = ["あ" * 50 + "。" + "い" * 200]
    lead = te.make_lead(paragraphs)
    assert lead == "あ" * 50 + "。"
    assert len(lead) <= te.LEAD_CHARS

    long_single = "う" * 200
    assert te.make_lead([long_single]) == "う" * te.LEAD_CHARS


def test_verify_edition_reports_a_broken_bundle(tmp_path: Path) -> None:
    root = tmp_path / "editions"
    summary = te.publish_edition(root, [make_article(1)], NOW)
    folder = edition_dir(root, summary)
    (folder / "articles" / "jiji-001.txt").unlink()

    problems = te.verify_edition(folder)
    assert any("missing articles/jiji-001.txt" in problem for problem in problems)


JIJI_ARTICLE_HTML = """
<div class="ArticleText clearfix">
    <div class="ArticleFigureWrapper">
        <figure>
            <figcaption><p>高市首相の投稿のワードクラウド（テスト分析）</p></figcaption>
        </figure>
    </div>
    <p>
    　【ワシントン時事】最初の段落です。同じ段落の二つ目の文です。
</p><p class="ArticleTextTab"><a href="/jc/article?k=1">関連記事の見出し</a></p><p>
    　司法省は新人のサリバン氏について「テスト」と主張した。ニューヨーク・タイムズ紙は「テスト」と指摘した。</p><p>
    　三つ目の段落です。
    <img src="/news2/kiji_photos/square/dummy/dummy2.png" alt=""></p>
</div>
<aside class="ArticleBottomLink"><p>関連記事</p></aside>
"""

FNN_ARTICLE_HTML = """
<div class="article-body">
<p>10月23日夜、テスト記事の最初の段落です。</p>
<p>漁業者<br>
「何も被害なく通り過ぎてくれればいいですけど」</p>
<h2>台風30号は26日に接近する見込み</h2>
<p>【24日午後6時の実況】<br>
種別：台風<br>
中心気圧：985 hPa</p>
<p><br>
■台風31号の今後の予報</p>
<figure><figcaption>台風30号の進路予想図</figcaption></figure>
</div>
<div class="article-related-contents"><p>関連記事の見出し</p></div>
"""


def test_jiji_paragraphs_survive_and_sidebar_is_not_a_paragraph() -> None:
    paragraphs = te.extract_jiji(JIJI_ARTICLE_HTML)

    assert paragraphs == [
        "【ワシントン時事】最初の段落です。同じ段落の二つ目の文です。",
        "司法省は新人のサリバン氏について「テスト」と主張した。ニューヨーク・タイムズ紙は「テスト」と指摘した。",
        "三つ目の段落です。",
    ]


def test_fnn_line_breaks_become_paragraphs_without_headings_or_related() -> None:
    paragraphs = te.extract_fnn(FNN_ARTICLE_HTML)

    assert paragraphs == [
        "10月23日夜、テスト記事の最初の段落です。",
        "漁業者",
        "「何も被害なく通り過ぎてくれればいいですけど」",
        "【24日午後6時の実況】",
        "種別：台風",
        "中心気圧：985 hPa",
        "■台風31号の今後の予報",
    ]
    assert "台風30号は26日に接近する見込み" not in paragraphs
    assert "台風30号の進路予想図" not in paragraphs
    assert "関連記事の見出し" not in paragraphs


class FakeResponse:
    def __init__(
        self,
        content: bytes,
        content_type: str = "text/html",
        apparent_encoding: str | None = None,
    ) -> None:
        self.headers = {"Content-Type": content_type}
        self.content = content
        self.apparent_encoding = apparent_encoding


SHIFT_JIS_PAGE = (
    '<html><head><meta charset="shift_jis"></head>'
    "<body>日本語のニュースです。文字化けしていません。</body></html>"
)


def test_response_text_reads_a_shift_jis_meta_page_despite_iso_8859_1_guess() -> None:
    response = FakeResponse(SHIFT_JIS_PAGE.encode("cp932"), "text/html", "ISO-8859-1")

    assert te.response_text(response) == SHIFT_JIS_PAGE
    assert "日本語のニュースです。文字化けしていません。" in te.response_text(response)


def test_response_text_reads_a_shift_jis_http_equiv_meta_page() -> None:
    html = (
        '<html><head><meta http-equiv="Content-Type" '
        'content="text/html; charset=Shift_JIS"></head>'
        "<body>日本語の本文です。</body></html>"
    )
    response = FakeResponse(html.encode("cp932"), "text/html", "ISO-8859-1")

    assert te.response_text(response) == html


def test_response_text_reads_an_xml_encoding_declaration() -> None:
    feed = (
        '<?xml version="1.0" encoding="shift_jis"?>'
        "<rss><channel><title>日本語の見出し①</title></channel></rss>"
    )
    response = FakeResponse(feed.encode("cp932"), "application/rss+xml", "ISO-8859-1")

    assert te.response_text(response) == feed


def test_response_text_prefers_the_content_type_charset_over_markup() -> None:
    html = (
        '<html><head><meta charset="shift_jis"></head>'
        "<body>ヘッダの指定が最優先です。</body></html>"
    )
    response = FakeResponse(html.encode("utf-8"), "text/html; charset=UTF-8", "ISO-8859-1")

    assert te.response_text(response) == html


def test_response_text_falls_back_to_apparent_encoding_then_utf_8() -> None:
    body = "日本語の本文です。"
    euc = FakeResponse(body.encode("euc_jp"), "text/html", "EUC-JP")
    assert te.response_text(euc) == body

    plain = FakeResponse(body.encode("utf-8"), "text/html", None)
    assert te.response_text(plain) == body


@pytest.mark.parametrize("name", ["shift_jis", "Shift-JIS", "sjis", "windows-31j", "x-sjis"])
def test_response_text_decodes_shift_jis_names_as_cp932(name: str) -> None:
    body = "①テスト"
    response = FakeResponse(
        body.encode("cp932"), f"text/html; charset={name}", "ISO-8859-1"
    )

    assert te.response_text(response) == body


def test_each_section_lead_survives_the_order() -> None:
    sections = ["政治", "経済", "国際", "社会", "スポーツ", "科学", "テクノロジー", "文化"]
    articles = [
        make_article(index, section=section, score=800 - index * 10)
        for index, section in enumerate(sections)
    ]
    articles.append(make_article(99, section="政治", source="FNN", key="fnn", score=10))

    ordered = te.order_articles(articles)

    for index, section in enumerate(sections):
        assert articles[index] in ordered, section
    assert len({article.candidate.url for article in ordered}) == len(ordered)


def test_technology_floor_keeps_five_even_when_their_scores_lose() -> None:
    articles = [
        make_article(100 + index, section="政治", score=1000 - index) for index in range(20)
    ]
    tech = [
        make_article(
            200 + index,
            section="テクノロジー",
            source="ITmedia",
            key="itm",
            score=10 - index,
        )
        for index in range(5)
    ]

    ordered = te.order_articles(articles + tech, limit=12)

    kept_tech = [article for article in ordered if article.candidate.section == "テクノロジー"]
    assert len(kept_tech) >= te.TECH_MIN
    assert {article.candidate.url for article in kept_tech} == {
        article.candidate.url for article in tech
    }
    assert len(ordered) == 12


def test_technology_keeps_every_candidate_when_there_are_fewer_than_five() -> None:
    articles = [
        make_article(100 + index, section="政治", score=1000 - index) for index in range(6)
    ]
    tech = [
        make_article(
            200 + index,
            section="テクノロジー",
            source="ITmedia",
            key="itm",
            score=1 - index,
        )
        for index in range(2)
    ]

    ordered = te.order_articles(articles + tech, limit=6)

    assert {
        article.candidate.url
        for article in ordered
        if article.candidate.section == "テクノロジー"
    } == {article.candidate.url for article in tech}


def test_culture_section_is_capped_at_one() -> None:
    culture = [
        make_article(index, section="文化", source="ナタリー", key="nat", score=1000 - index)
        for index in range(6)
    ]
    others = [
        make_article(100 + index, section=section, score=10 - index)
        for index, section in enumerate(["政治", "経済", "科学"])
    ]

    ordered = te.order_articles(culture + others)

    kept_culture = [article for article in ordered if article.candidate.section == "文化"]
    assert len(kept_culture) == 1
    assert kept_culture[0] is culture[0]


def test_order_honours_the_limit_without_duplicates() -> None:
    sections = ["政治", "経済", "国際", "社会", "スポーツ", "文化", "科学"]
    articles = [
        make_article(index, section=sections[index % len(sections)], score=1000 - index)
        for index in range(40)
    ]

    ordered = te.order_articles(articles, limit=10)

    assert len(ordered) == 10
    assert len({article.candidate.url for article in ordered}) == 10


def test_shopping_titles_are_detected_without_false_positives() -> None:
    shopping = [
        "本日みつけたお買い得品",
        "タイムセール開催中",
        "冬のセール開始",
        "特価で登場",
        "最安値で販売",
    ]
    for title in shopping:
        assert te.is_shopping_title(title) is True, title

    for title in ("セールスフォースが新AI", "Amazonで大規模障害"):
        assert te.is_shopping_title(title) is False, title


ITMEDIA_RSS = """<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0"><channel><title>ITmedia</title>
<item><title>通常のニュース記事</title><link>https://www.itmedia.co.jp/news/articles/2610/24/news001.html</link><pubDate>Fri, 24 Oct 2026 09:00:00 +0900</pubDate></item>
<item><title>AIの記事</title><link>https://www.itmedia.co.jp/ait/articles/2610/24/news002.html</link><pubDate>Fri, 24 Oct 2026 09:00:00 +0900</pubDate></item>
<item><title>AI+の記事</title><link>https://www.itmedia.co.jp/aiplus/articles/2610/24/news003.html</link><pubDate>Fri, 24 Oct 2026 09:00:00 +0900</pubDate></item>
<item><title>二つ目の通常記事</title><link>https://www.itmedia.co.jp/news/articles/2610/24/news004.html</link><pubDate>Fri, 24 Oct 2026 10:00:00 +0900</pubDate></item>
</channel></rss>
"""


class FakeFetcher:
    def __init__(self, text: str) -> None:
        self.text = text
        self.calls: list[tuple[str, str]] = []

    def get(self, url: str, source: str) -> str:
        self.calls.append((url, source))
        return self.text


def test_itmedia_feed_skips_ait_and_aiplus_urls() -> None:
    fetcher = FakeFetcher(ITMEDIA_RSS)

    candidates = te.collect_itmedia(fetcher)  # type: ignore[arg-type]

    assert [candidate.url for candidate in candidates] == [
        "https://www.itmedia.co.jp/news/articles/2610/24/news001.html",
        "https://www.itmedia.co.jp/news/articles/2610/24/news004.html",
    ]
    assert all(candidate.source == "ITmedia" for candidate in candidates)
    assert all(candidate.key == "itm" for candidate in candidates)
    assert all(candidate.section == "テクノロジー" for candidate in candidates)
    assert fetcher.calls == [(te.ITMEDIA_FEED, "ITmedia")]


NAZOLOGY_ARTICLE_HTML = """
<article>
    <div class="post-lead__contents"><p>リードの段落です。</p></div>
    <div class="post-body__contents">
        <p>本文の最初の段落です。</p>
        <p>本文の二つ目の段落です。</p>
        <p>関連記事</p>
        <p>停止マーカーより後の段落です。</p>
    </div>
    <div class="post-related"><p>外側の関連記事です。</p></div>
</article>
<footer><p>フッターの段落です。</p></footer>
"""


def test_nazology_keeps_body_paragraphs_and_drops_the_related_section() -> None:
    paragraphs = te.extract_nazology(NAZOLOGY_ARTICLE_HTML)

    assert paragraphs == [
        "リードの段落です。",
        "本文の最初の段落です。",
        "本文の二つ目の段落です。",
    ]
    assert "停止マーカーより後の段落です。" not in paragraphs
    assert "外側の関連記事です。" not in paragraphs
    assert "フッターの段落です。" not in paragraphs


NATALIE_ARTICLE_HTML = """
<div class="NA_article_body">
    <p>本文の最初の段落です。</p>
    <p>［高画質で見る］</p>
    <p>本文の二つ目の段落です。［高画質で見る］</p>
    <p>この記事の画像（全3件）</p>
    <p>停止マーカーより後の段落です。</p>
</div>
<div class="NA_article_body"><p>二つ目の本文ブロックです。</p></div>
<div class="NA_article_related"><p>関連する特集・インタビュー</p></div>
"""


def test_natalie_keeps_body_paragraphs_and_stops_at_the_gallery_link() -> None:
    paragraphs = te.extract_natalie(NATALIE_ARTICLE_HTML)

    assert paragraphs == ["本文の最初の段落です。", "本文の二つ目の段落です。"]
    assert "停止マーカーより後の段落です。" not in paragraphs
    assert "二つ目の本文ブロックです。" not in paragraphs


PCWATCH_ARTICLE_HTML = """
<div class="mainContents">
    <h1>記事の見出し</h1>
    <p>本文の最初の段落です。</p>
    <p>本文の二つ目の段落です。</p>
</div>
<div class="relatedContents">
    <h2>関連記事</h2>
    <p>関連記事の見出しです。</p>
</div>
"""


def test_pcwatch_keeps_only_the_main_content_paragraphs() -> None:
    paragraphs = te.extract_pcwatch(PCWATCH_ARTICLE_HTML)

    assert paragraphs == ["本文の最初の段落です。", "本文の二つ目の段落です。"]
    assert "関連記事の見出しです。" not in paragraphs
    assert "記事の見出し" not in paragraphs


@pytest.mark.parametrize(
    ("title", "source", "expected"),
    [
        ("タイトル - PC Watch", "PC Watch", "タイトル"),
        ("タイトル｜PC Watch", "PC Watch", "タイトル"),
        ("タイトル－PC Watch", "PC Watch", "タイトル"),
        ("タイトル — PC Watch", "PC Watch", "タイトル"),
        ("AI - その先へ", "PC Watch", "AI - その先へ"),
        ("PC Watch", "PC Watch", "PC Watch"),
        (" - PC Watch", "PC Watch", " - PC Watch"),
    ],
)
def test_only_a_trailing_source_name_is_removed_from_the_title(
    title: str, source: str, expected: str
) -> None:
    assert te.strip_source_suffix(title, source) == expected


PCWATCH_RSS = """<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0"><channel><title>PC Watch</title>
<item><title>見出しA - PC Watch</title><link>https://pc.watch.impress.co.jp/docs/news/1.html</link><pubDate>Fri, 24 Oct 2026 09:00:00 +0900</pubDate></item>
<item><title>見出しB｜PC Watch</title><link>https://pc.watch.impress.co.jp/docs/news/2.html</link><pubDate>Fri, 24 Oct 2026 10:00:00 +0900</pubDate></item>
</channel></rss>
"""


def test_pcwatch_feed_titles_lose_the_source_suffix() -> None:
    candidates = te.collect_pcwatch(FakeFetcher(PCWATCH_RSS))  # type: ignore[arg-type]

    assert [candidate.title for candidate in candidates] == ["見出しA", "見出しB"]


class RoutingFetcher:
    def __init__(self, pages: dict[str, str]) -> None:
        self.pages = pages
        self.calls: list[tuple[str, str]] = []

    def get(self, url: str, source: str) -> str:
        self.calls.append((url, source))
        return self.pages.get(url, "")


def test_a_pcwatch_article_keeps_the_stripped_title_after_the_page_is_fetched() -> None:
    article_url = "https://pc.watch.impress.co.jp/docs/news/3.html"
    rss = f"""<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0"><channel><title>PC Watch</title>
<item><title>ページ経由の見出し - PC Watch</title><link>{article_url}</link><pubDate>Fri, 24 Oct 2026 09:00:00 +0900</pubDate></item>
</channel></rss>
"""
    html = f'<div class="mainContents"><p>{BODY}</p></div>'
    fetcher = RoutingFetcher({te.PCWATCH_FEED: rss, article_url: html})

    candidates = te.collect_pcwatch(fetcher)  # type: ignore[arg-type]
    articles, _stats = te.harvest(fetcher, candidates, set(), NOW)  # type: ignore[arg-type]

    assert [article.candidate.title for article in articles] == ["ページ経由の見出し"]


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__]))
