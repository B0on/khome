#include "suites.h"

#include <stdlib.h>
#include <string.h>

#include "kindletimes.h"
#include "test_harness.h"
#include "text.h"

/* Kindle Times paper and article layout.  These tests drive the public
 * kindletimes.h API with the text-edition fixture in tests/fixtures/news-text
 * and assert what a reader sees: edition stamps, page ranges, uninterrupted
 * headlines, and a next-article box only on the last page. */

static Fonts g_fonts;
static int g_fonts_loaded;

static const char *const k_fixture_dirs[] = {
    "tests/fixtures/news-text",
    "../tests/fixtures/news-text"
};

static int load_fonts(void)
{
    const char *env = getenv("KHOME_FONTS");
    const char *candidates[4];
    int count = 0;
    int i;

    if (env != NULL && env[0] != 0) {
        candidates[count++] = env;
    }
    candidates[count++] = "../design/system/fonts";
    candidates[count++] = "design/system/fonts";
    candidates[count] = NULL;
    for (i = 0; i < count; i++) {
        if (fonts_load(&g_fonts, candidates[i])) {
            return 1;
        }
    }
    return 0;
}

static int fonts_ready(void)
{
    KH_CHECK_MSG(g_fonts_loaded,
                 "fonts_load failed (run from launcher/ or set KHOME_FONTS)");
    return g_fonts_loaded;
}

static int load_paper(KtPaper *paper)
{
    size_t i;

    if (!fonts_ready()) {
        return 0;
    }
    for (i = 0; i < sizeof(k_fixture_dirs) / sizeof(k_fixture_dirs[0]); i++) {
        if (kt_paper_load(paper, k_fixture_dirs[i])) {
            kt_paper_build(paper, &g_fonts);
            return 1;
        }
    }
    KH_CHECK_MSG(0, "could not load the text-edition fixture from %s",
                 k_fixture_dirs[0]);
    return 0;
}

static int ends_with_ellipsis(const char *s)
{
    size_t n = strlen(s);

    return n >= 3 && memcmp(s + n - 3, "\xE2\x80\xA6", 3) == 0;
}

static int contains_ellipsis(const char *s)
{
    return strstr(s, "\xE2\x80\xA6") != NULL;
}

static int lines_cover(const char *text, const TextLine *lines, int count)
{
    size_t off = 0;
    int i;

    for (i = 0; i < count; i++) {
        if (lines[i].start != off) {
            return 0;
        }
        off += lines[i].length;
    }
    return off == strlen(text);
}

static void check_page_coverage(const char *label, const KtArticle *a)
{
    int expected = 0;
    int i;

    KH_CHECK_MSG(a->page_count >= 1, "%s: expected at least one page, got %d",
                 label, a->page_count);
    for (i = 0; i < a->page_count; i++) {
        KH_CHECK_MSG(a->pages[i].first == expected,
                     "%s: page %d starts at line %d, expected %d", label, i,
                     a->pages[i].first, expected);
        KH_CHECK_MSG(a->pages[i].count >= 1,
                     "%s: page %d holds %d lines, expected at least 1", label,
                     i, a->pages[i].count);
        expected += a->pages[i].count;
    }
    KH_CHECK_MSG(expected == a->line_count,
                 "%s: pages hold %d lines, body has %d", label, expected,
                 a->line_count);
}

static int line_starts_paragraph(const KtArticle *a, int line)
{
    size_t start;

    if (line < 0 || line >= a->line_count) {
        return 0;
    }
    start = a->lines[line].start;
    return start == 0 || a->body[start - 1] == '\n';
}

static int next_box_height(KtArticle *a, int page)
{
    int first = -1;
    int last = -1;
    int y;

    a->page = page;
    for (y = 0; y < 1024; y++) {
        if (kt_article_next_at(a, 100, y)) {
            if (first < 0) {
                first = y;
            }
            last = y;
        }
    }
    return first < 0 ? 0 : last - first + 1;
}

static void test_edition_id_parses_fields(void)
{
    KtEdition e;

    memset(&e, 0, sizeof(e));
    if (!kt_edition_id_parse("20261001-0900", &e)) {
        KH_CHECK_MSG(0, "rejected a valid edition id \"20261001-0900\"");
        return;
    }
    KH_CHECK_MSG(strcmp(e.id, "20261001-0900") == 0,
                 "id: expected \"20261001-0900\", got \"%s\"", e.id);
    KH_CHECK_MSG(e.year == 2026 && e.month == 10 && e.day == 1,
                 "date: expected 2026-10-01, got %04d-%02d-%02d", e.year,
                 e.month, e.day);
    KH_CHECK_MSG(e.hour == 9 && e.minute == 0,
                 "time: expected 09:00, got %02d:%02d", e.hour, e.minute);
    KH_CHECK_MSG(e.morning == 1, "09:00 should be the morning edition");
    KH_CHECK_MSG(e.issue == 1,
                 "2026-10-01 is the first issue, got %d", e.issue);

    memset(&e, 0, sizeof(e));
    if (!kt_edition_id_parse("20261002-1200", &e)) {
        KH_CHECK_MSG(0, "rejected a valid edition id \"20261002-1200\"");
        return;
    }
    KH_CHECK_MSG(e.morning == 0, "12:00 should be the evening edition");
    KH_CHECK_MSG(e.issue == 2,
                 "2026-10-02 is the second issue, got %d", e.issue);

    memset(&e, 0, sizeof(e));
    if (!kt_edition_id_parse("20261001-0000", &e)) {
        KH_CHECK_MSG(0, "rejected a valid edition id \"20261001-0000\"");
        return;
    }
    KH_CHECK_MSG(e.morning == 1, "00:00 should be the morning edition");
}

static void test_edition_id_rejects_broken_stamps(void)
{
    static const char *const broken[] = {
        "",
        "20261001",
        "20261001-090",
        "20261001-09000",
        "2026100-10900",
        "20261001_0900",
        "20261O01-0900",
        "2026O101-0900",
        "20261301-0900",
        "20260001-0900",
        "20261000-0900",
        "20261032-0900",
        "20261001-2400",
        "20261001-0960"
    };
    size_t i;

    for (i = 0; i < sizeof(broken) / sizeof(broken[0]); i++) {
        KtEdition e;

        KH_CHECK_MSG(!kt_edition_id_parse(broken[i], &e),
                     "accepted the broken edition id \"%s\"", broken[i]);
    }
}

static void test_issue_number_counts_from_october_first(void)
{
    KH_CHECK_MSG(kt_issue_number(2026, 10, 1) == 1,
                 "2026-10-01: expected issue 1, got %d",
                 kt_issue_number(2026, 10, 1));
    KH_CHECK_MSG(kt_issue_number(2026, 10, 2) == 2,
                 "2026-10-02: expected issue 2, got %d",
                 kt_issue_number(2026, 10, 2));
    KH_CHECK_MSG(kt_issue_number(2026, 10, 31) == 31,
                 "2026-10-31: expected issue 31, got %d",
                 kt_issue_number(2026, 10, 31));
    KH_CHECK_MSG(kt_issue_number(2027, 1, 1) == 93,
                 "2027-01-01: expected issue 93, got %d",
                 kt_issue_number(2027, 1, 1));
}

static void test_paginate_fills_pages_and_covers_lines(void)
{
    static const int counts[] = { 13, 17, 17, 17, 17, 17, 2 };
    KtArticlePage pages[KT_ARTICLE_PAGE_MAX];
    int n;
    int i;

    n = kt_article_paginate(100, 13, 11, 17, 15, pages,
                            KT_ARTICLE_PAGE_MAX);
    KH_CHECK_MSG(n == 7, "100 lines: expected 7 pages, got %d", n);
    if (n != 7) {
        return;
    }
    for (i = 0; i < n; i++) {
        KH_CHECK_MSG(pages[i].count == counts[i],
                     "100 lines: page %d holds %d lines, expected %d", i,
                     pages[i].count, counts[i]);
    }
    KH_CHECK_MSG(pages[0].first == 0, "page 0: expected first line 0, got %d",
                 pages[0].first);
    for (i = 1; i < n; i++) {
        KH_CHECK_MSG(pages[i].first == pages[i - 1].first +
                                     pages[i - 1].count,
                     "page %d: first line %d does not follow page %d", i,
                     pages[i].first, i - 1);
    }
}

static void test_paginate_never_leaves_an_empty_last_page(void)
{
    int line_count;

    for (line_count = 1; line_count <= 200; line_count++) {
        KtArticlePage pages[KT_ARTICLE_PAGE_MAX];
        int first_cap = 13;
        int first_last = 11;
        int fill = 17;
        int last_fill = 15;
        int n = kt_article_paginate(line_count, first_cap, first_last, fill,
                                    last_fill, pages, KT_ARTICLE_PAGE_MAX);
        int expected = 0;
        int i;

        KH_CHECK_MSG(n >= 1, "%d lines: expected at least one page, got %d",
                     line_count, n);
        for (i = 0; i < n; i++) {
            int cap = i == 0 ? first_cap : fill;

            KH_CHECK_MSG(pages[i].count >= 1,
                         "%d lines: page %d is empty", line_count, i);
            KH_CHECK_MSG(pages[i].first == expected,
                         "%d lines: page %d starts at %d, expected %d",
                         line_count, i, pages[i].first, expected);
            expected += pages[i].count;
            if (i == n - 1) {
                int last_cap = n == 1 ? first_last : last_fill;

                KH_CHECK_MSG(pages[i].count <= last_cap,
                             "%d lines: last page holds %d lines, over its "
                             "%d-line reserve", line_count, pages[i].count,
                             last_cap);
            } else {
                int remaining = line_count - expected;

                KH_CHECK_MSG(pages[i].count <= cap,
                             "%d lines: page %d holds %d lines, over its "
                             "%d-line capacity", line_count, i,
                             pages[i].count, cap);
                if (pages[i].count < cap) {
                    KH_CHECK_MSG(remaining <= last_fill,
                                 "%d lines: page %d stops at %d of %d lines "
                                 "while %d lines remain for more than the "
                                 "last page", line_count, i, pages[i].count,
                                 cap, remaining);
                }
            }
        }
        KH_CHECK_MSG(expected == line_count,
                     "%d lines: pages hold %d lines", line_count, expected);
    }
}

static void test_page_count_matches_pagination(void)
{
    int n;
    int lines;

    KH_CHECK_MSG(kt_article_page_count(13, 13, 17) == 1,
                 "13 lines at 13/17: expected 1 page, got %d",
                 kt_article_page_count(13, 13, 17));
    KH_CHECK_MSG(kt_article_page_count(14, 13, 17) == 2,
                 "14 lines at 13/17: expected 2 pages, got %d",
                 kt_article_page_count(14, 13, 17));
    KH_CHECK_MSG(kt_article_page_count(100, 13, 17) == 7,
                 "100 lines at 13/17: expected 7 pages, got %d",
                 kt_article_page_count(100, 13, 17));

    n = 0;
    for (lines = 0; lines <= 300; lines++) {
        int got = kt_article_page_count(lines, 13, 17);

        KH_CHECK_MSG(got >= n,
                     "%d lines: page count %d dropped below %d for one fewer "
                     "line", lines, got, n);
        n = got;
    }
}

static void test_article_pages_flow_through_paragraphs(void)
{
    KtPaper paper;
    KtArticle article;
    int i;
    int continued = 0;

    if (!load_paper(&paper)) {
        return;
    }
    memset(&article, 0, sizeof(article));
    if (!kt_article_open(&article, &paper, &g_fonts, 0, 30)) {
        KH_CHECK_MSG(0, "could not open the fixture article");
        return;
    }
    KH_CHECK_MSG(article.page_count >= 3,
                 "fixture article: expected at least 3 pages at 30 px, got %d",
                 article.page_count);
    check_page_coverage("fixture article at 30 px", &article);
    KH_CHECK_MSG(article.pages[0].count < article.line_count,
                 "fixture article: page 0 holds all %d lines, so the body "
                 "never flows to page 2", article.line_count);
    for (i = 1; i < article.page_count; i++) {
        if (!line_starts_paragraph(&article, article.pages[i].first)) {
            continued = 1;
        }
    }
    KH_CHECK_MSG(continued,
                 "fixture article: every page starts a new paragraph, so no "
                 "paragraph flows across a page turn");
    kt_article_close(&article);
}

static void test_article_page_count_grows_with_text_size(void)
{
    static const int sizes[] = { 26, 30, 36 };
    KtPaper paper;
    KtArticle article;
    int pages[3];
    int i;

    if (!load_paper(&paper)) {
        return;
    }
    memset(&article, 0, sizeof(article));
    if (!kt_article_open(&article, &paper, &g_fonts, 0, sizes[0])) {
        KH_CHECK_MSG(0, "could not open the fixture article at 26 px");
        return;
    }
    for (i = 0; i < 3; i++) {
        char label[64];

        if (i > 0 && !kt_article_set_size(&article, &paper, &g_fonts,
                                          sizes[i])) {
            KH_CHECK_MSG(0, "could not set the article to %d px", sizes[i]);
            kt_article_close(&article);
            return;
        }
        KH_CHECK_MSG(article.size == sizes[i],
                     "expected size %d px, got %d", sizes[i], article.size);
        pages[i] = article.page_count;
        snprintf(label, sizeof(label), "fixture article at %d px", sizes[i]);
        check_page_coverage(label, &article);
    }
    KH_CHECK_MSG(pages[0] <= pages[1],
                 "26 px takes %d pages, 30 px takes %d: bigger type cannot "
                 "need fewer pages", pages[0], pages[1]);
    KH_CHECK_MSG(pages[1] <= pages[2],
                 "30 px takes %d pages, 36 px takes %d: bigger type cannot "
                 "need fewer pages", pages[1], pages[2]);
    kt_article_close(&article);
}

static void test_article_headline_is_complete(void)
{
    static const int sizes[] = { 26, 30, 36 };
    KtPaper paper;
    KtArticle article;
    int i;

    if (!load_paper(&paper)) {
        return;
    }
    for (i = 0; i < 3; i++) {
        memset(&article, 0, sizeof(article));
        if (!kt_article_open(&article, &paper, &g_fonts, 0, sizes[i])) {
            KH_CHECK_MSG(0, "could not open the fixture article at %d px",
                         sizes[i]);
            continue;
        }
        KH_CHECK_MSG(strcmp(article.headline, paper.stories[0].title) == 0,
                     "%d px: headline lost the full title: \"%s\"", sizes[i],
                     article.headline);
        KH_CHECK_MSG(!contains_ellipsis(article.head),
                     "%d px: article headline is shortened with an ellipsis: "
                     "\"%s\"", sizes[i], article.head);
        KH_CHECK_MSG(article.head_count >= 2,
                     "%d px: headline fits on %d line(s), so this check "
                     "cannot see a line cap", sizes[i], article.head_count);
        KH_CHECK_MSG(lines_cover(article.head, article.head_lines,
                                 article.head_count),
                     "%d px: the wrapped headline does not cover the whole "
                     "\"%s\"", sizes[i], article.head);
        kt_article_close(&article);
    }
}

static void test_article_last_page_reserves_next_box(void)
{
    KtPaper paper;
    KtArticle article;
    int last;
    int i;

    if (!load_paper(&paper)) {
        return;
    }
    memset(&article, 0, sizeof(article));
    if (!kt_article_open(&article, &paper, &g_fonts, 0, 30)) {
        KH_CHECK_MSG(0, "could not open the fixture article");
        return;
    }
    KH_CHECK_MSG(article.page_count >= 2,
                 "fixture article: expected at least 2 pages, got %d",
                 article.page_count);
    if (article.page_count < 2) {
        kt_article_close(&article);
        return;
    }
    last = article.page_count - 1;
    for (i = 0; i < last; i++) {
        int height = next_box_height(&article, i);

        KH_CHECK_MSG(height == 0,
                     "page %d is not the last page but has a %d px "
                     "next-article box", i, height);
    }
    KH_CHECK_MSG(next_box_height(&article, last) == 70,
                 "last page next-article box is %d px, expected 70",
                 next_box_height(&article, last));
    kt_article_close(&article);
}

static void test_paper_top_headline_stops_at_three_lines(void)
{
    KtPaper paper;
    KtStoryText *top;
    size_t length;

    if (!load_paper(&paper)) {
        return;
    }
    top = &paper.text[0];
    KH_CHECK_MSG(top->head_count == 3,
                 "top headline: expected 3 lines, got %d", top->head_count);
    KH_CHECK_MSG(ends_with_ellipsis(top->head),
                 "top headline: last line does not end with an ellipsis: "
                 "\"%s\"", top->head);
    length = strlen(top->head);
    KH_CHECK_MSG(top->head_lines[2].start + top->head_lines[2].length ==
                     length,
                 "top headline: line 3 does not reach the end of the drawn "
                 "text (%zu bytes)", length);
    KH_CHECK_MSG(strlen(paper.stories[0].title) > length,
                 "top headline: the fixture title is not longer than the "
                 "three drawn lines");
}

static void test_paper_plan_keeps_story_order(void)
{
    KtFit fit[7];
    KtPaperLayout layout;
    int seen[7] = { 0 };
    int expected_item = 0;
    int i;
    int p;

    memset(fit, 0, sizeof(fit));
    for (i = 0; i < 7; i++) {
        int height = i == 0 ? 300 : 120;

        fit[i].top_height = height;
        fit[i].brief_height = height;
        fit[i].head_count = 1;
        fit[i].lead_count = 1;
    }
    if (!kt_paper_plan(fit, 7, &layout)) {
        KH_CHECK_MSG(0, "kt_paper_plan returned 0 for 7 stories");
        return;
    }
    KH_CHECK_MSG(layout.item_count == 7,
                 "expected 7 placed stories, got %d", layout.item_count);
    KH_CHECK_MSG(layout.page_count >= 2,
                 "7 stories should need more than one page, got %d",
                 layout.page_count);
    for (i = 0; i < layout.item_count; i++) {
        int story = layout.items[i].story;

        KH_CHECK_MSG(story >= 0 && story < 7,
                     "item %d names story %d", i, story);
        if (story >= 0 && story < 7) {
            KH_CHECK_MSG(seen[story] == 0,
                         "story %d is placed twice", story);
            seen[story]++;
        }
        if (i == 0) {
            KH_CHECK_MSG(layout.items[i].role == KT_ROLE_TOP,
                         "item 0: expected the top story, got role %d",
                         layout.items[i].role);
        } else {
            KH_CHECK_MSG(layout.items[i].role == KT_ROLE_BRIEF,
                         "item %d: expected a brief, got role %d", i,
                         layout.items[i].role);
        }
    }
    for (i = 0; i < 7; i++) {
        KH_CHECK_MSG(seen[i] == 1, "story %d was placed %d times", i,
                     seen[i]);
    }

    for (p = 0; p < layout.page_count; p++) {
        int first = layout.pages[p].first;
        int end = first + layout.pages[p].count;

        KH_CHECK_MSG(layout.pages[p].count >= 1,
                     "page %d is empty", p);
        KH_CHECK_MSG(first == expected_item,
                     "page %d starts at item %d, expected %d", p, first,
                     expected_item);
        for (i = first; i < end && i < layout.item_count; i++) {
            KH_CHECK_MSG(layout.story_page[layout.items[i].story] == p,
                         "page %d: story %d maps to page %d", p,
                         layout.items[i].story,
                         layout.story_page[layout.items[i].story]);
            if (i > first && i > 0) {
                KH_CHECK_MSG(layout.items[i - 1].content_y +
                                     layout.items[i - 1].content_h <=
                                 layout.items[i].content_y,
                             "page %d: item %d overlaps item %d", p, i - 1,
                             i);
            }
        }
        expected_item = end;
    }
    KH_CHECK_MSG(expected_item == layout.item_count,
                 "pages hold %d items, layout has %d", expected_item,
                 layout.item_count);

    KH_CHECK_MSG(layout.index_top >
                     layout.items[layout.pages[0].first +
                                 layout.pages[0].count - 1].rule_y,
                 "the index box starts at %d, on or above the last page-1 "
                 "story rule at %d", layout.index_top,
                 layout.items[layout.pages[0].first +
                              layout.pages[0].count - 1].rule_y);
    KH_CHECK_MSG(layout.index_count >= 1,
                 "expected the index to list a story from page 2 or later");
    for (i = 0; i < layout.index_count; i++) {
        KH_CHECK_MSG(layout.index_story[i] > 0 &&
                         layout.story_page[layout.index_story[i]] > 0,
                     "index row %d lists story %d from page %d", i,
                     layout.index_story[i],
                     layout.story_page[layout.index_story[i]]);
        if (i > 0) {
            KH_CHECK_MSG(layout.index_story[i - 1] < layout.index_story[i],
                         "index rows are out of order at %d", i);
        }
    }
    KH_CHECK_MSG(layout.index_count <= KT_INDEX_MAX,
                 "index has %d rows, over its %d-row limit",
                 layout.index_count, KT_INDEX_MAX);
}

static void test_paper_plan_spills_to_the_next_page(void)
{
    KtFit fit[4];
    KtPaperLayout layout;
    int i;

    memset(fit, 0, sizeof(fit));
    for (i = 0; i < 4; i++) {
        int height = i == 0 ? 300 : 500;

        fit[i].top_height = height;
        fit[i].brief_height = height;
    }
    if (!kt_paper_plan(fit, 4, &layout)) {
        KH_CHECK_MSG(0, "kt_paper_plan returned 0 for 4 tall stories");
        return;
    }
    KH_CHECK_MSG(layout.page_count == 4,
                 "4 tall stories: expected one page each (4), got %d",
                 layout.page_count);
    for (i = 0; i < 4; i++) {
        KH_CHECK_MSG(layout.story_page[i] == i,
                     "story %d: expected page %d, got %d", i, i,
                     layout.story_page[i]);
    }
    for (i = 0; i < layout.page_count; i++) {
        KH_CHECK_MSG(layout.pages[i].count == 1,
                     "page %d: expected 1 story, got %d", i,
                     layout.pages[i].count);
    }
}

static void test_paper_plan_single_story_has_no_index(void)
{
    KtFit fit[1];
    KtPaperLayout layout;

    memset(fit, 0, sizeof(fit));
    fit[0].top_height = 300;
    fit[0].brief_height = 300;
    if (!kt_paper_plan(fit, 1, &layout)) {
        KH_CHECK_MSG(0, "kt_paper_plan returned 0 for one story");
        return;
    }
    KH_CHECK_MSG(layout.page_count == 1,
                 "one story: expected 1 page, got %d", layout.page_count);
    KH_CHECK_MSG(layout.item_count == 1,
                 "one story: expected 1 item, got %d", layout.item_count);
    KH_CHECK_MSG(layout.story_page[0] == 0,
                 "one story: expected page 0, got %d", layout.story_page[0]);
    KH_CHECK_MSG(layout.index_count == 0,
                 "one story: expected an empty index, got %d rows",
                 layout.index_count);
}

void kh_run_kindletimes_tests(void)
{
    printf("\n[kindletimes]\n");
    g_fonts_loaded = load_fonts();
    kh_run_test("kindletimes.edition_id_parses_fields",
                test_edition_id_parses_fields);
    kh_run_test("kindletimes.edition_id_rejects_broken_stamps",
                test_edition_id_rejects_broken_stamps);
    kh_run_test("kindletimes.issue_number_counts_from_october_first",
                test_issue_number_counts_from_october_first);
    kh_run_test("kindletimes.paginate_fills_pages_and_covers_lines",
                test_paginate_fills_pages_and_covers_lines);
    kh_run_test("kindletimes.paginate_never_leaves_an_empty_last_page",
                test_paginate_never_leaves_an_empty_last_page);
    kh_run_test("kindletimes.page_count_matches_pagination",
                test_page_count_matches_pagination);
    kh_run_test("kindletimes.article_pages_flow_through_paragraphs",
                test_article_pages_flow_through_paragraphs);
    kh_run_test("kindletimes.article_page_count_grows_with_text_size",
                test_article_page_count_grows_with_text_size);
    kh_run_test("kindletimes.article_headline_is_complete",
                test_article_headline_is_complete);
    kh_run_test("kindletimes.article_last_page_reserves_next_box",
                test_article_last_page_reserves_next_box);
    kh_run_test("kindletimes.paper_top_headline_stops_at_three_lines",
                test_paper_top_headline_stops_at_three_lines);
    kh_run_test("kindletimes.paper_plan_keeps_story_order",
                test_paper_plan_keeps_story_order);
    kh_run_test("kindletimes.paper_plan_spills_to_the_next_page",
                test_paper_plan_spills_to_the_next_page);
    kh_run_test("kindletimes.paper_plan_single_story_has_no_index",
                test_paper_plan_single_story_has_no_index);
    if (g_fonts_loaded) {
        fonts_free(&g_fonts);
    }
}
