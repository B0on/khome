#include "suites.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "tabs.h"
#include "test_harness.h"

/* The tab list is a pure value from src/tabs.h: a text form persisted as
 * tabs.txt, a fixed-capacity list in open order, and two string helpers for
 * the sheet rows.  Tests drive the public functions only. */

static void expect_list(const char *label, const TabList *tabs,
                        const char *const *urls, int count, int active)
{
    int i;

    KH_CHECK_MSG(tabs->count == count, "%s: expected %d tabs, got %d", label,
                 count, tabs->count);
    KH_CHECK_MSG(tabs->active == active,
                 "%s: expected active %d, got %d", label, active,
                 tabs->active);
    for (i = 0; i < count && i < tabs->count; i++) {
        KH_CHECK_MSG(strcmp(tabs->tabs[i].url, urls[i]) == 0,
                     "%s: tab %d: expected %s, got %s", label, i, urls[i],
                     tabs->tabs[i].url);
    }
}

static void init_tabs(TabList *tabs, const char *const *urls, int count,
                      int active)
{
    int i;

    tabs_init(tabs);
    for (i = 0; i < count; i++) {
        tabs_open(tabs, urls[i]);
    }
    if (active >= 0) {
        tabs_select(tabs, active);
    }
}

static void test_parse_header_keeps_order_and_active(void)
{
    static const char *const urls[] = { "https://a.example/",
                                        "https://b.example/",
                                        "https://c.example/" };
    TabList tabs;
    int count;

    count = tabs_parse(&tabs, "active=2\nhttps://a.example/\n"
                              "https://b.example/\nhttps://c.example/\n");
    KH_CHECK_MSG(count == 3, "parse with header: expected 3 tabs, got %d",
                 count);
    expect_list("parse with header", &tabs, urls, 3, 2);
}

static void test_parse_without_header_activates_first_url(void)
{
    static const char *const urls[] = { "https://a.example/",
                                        "https://b.example/" };
    TabList tabs;

    tabs_parse(&tabs, "https://a.example/\nhttps://b.example/\n");
    expect_list("parse without header", &tabs, urls, 2, 0);
}

static void test_parse_ignores_blank_lines_and_crlf(void)
{
    static const char *const urls[] = { "https://a.example/",
                                        "https://b.example/" };
    TabList tabs;

    tabs_parse(&tabs, "\r\n\r\nactive=1\r\n\r\nhttps://a.example/\r\n"
                      "https://b.example/\r\n\r\n");
    expect_list("parse with blank lines and CRLF", &tabs, urls, 2, 1);
}

/* A text editor on the Kindle may save tabs.txt with a UTF-8 BOM.  The BOM is
 * not part of the header, so the file reads like the same text without it. */
static void test_parse_skips_a_utf8_bom(void)
{
    static const char *const urls[] = { "https://a.example/",
                                        "https://b.example/" };
    TabList tabs;
    int count;

    count = tabs_parse(&tabs,
                       "\xEF\xBB\xBF"
                       "active=1\nhttps://a.example/\nhttps://b.example/\n");
    KH_CHECK_MSG(count == 2, "parse with a BOM: expected 2 tabs, got %d",
                 count);
    expect_list("parse with a BOM", &tabs, urls, 2, 1);
    KH_CHECK_MSG(strcmp(tabs.tabs[0].url, "\xEF\xBB\xBF"
                                          "active=1") != 0,
                 "the BOM must not turn the header into a URL");
}

static void test_parse_skips_url_at_or_over_length_limit(void)
{
    char text[KHOME_TAB_URL_MAX * 2 + 64];
    char at_limit[KHOME_TAB_URL_MAX];
    char over_limit[KHOME_TAB_URL_MAX + 1];
    static const char *const last = "https://last.example/";
    const char *urls[2];
    TabList tabs;
    size_t n = 0;

    memset(at_limit, 'a', sizeof(at_limit) - 1);
    at_limit[sizeof(at_limit) - 1] = 0;
    memset(over_limit, 'b', sizeof(over_limit) - 1);
    over_limit[sizeof(over_limit) - 1] = 0;

    n += (size_t)snprintf(text + n, sizeof(text) - n, "active=0\n");
    n += (size_t)snprintf(text + n, sizeof(text) - n, "%s\n", at_limit);
    n += (size_t)snprintf(text + n, sizeof(text) - n, "%s\n", over_limit);
    snprintf(text + n, sizeof(text) - n, "%s\n", last);

    KH_CHECK_MSG(tabs_parse(&tabs, text) == 2,
                 "only the 1023-byte URL and the next one must be kept");
    urls[0] = at_limit;
    urls[1] = last;
    expect_list("parse length limit", &tabs, urls, 2, 0);
    KH_CHECK_MSG(strlen(at_limit) == KHOME_TAB_URL_MAX - 1,
                 "the 1023-byte URL must be kept intact");
}

static void test_parse_stops_at_tab_capacity(void)
{
    static const char *const urls[] = {
        "https://0.example/", "https://1.example/", "https://2.example/",
        "https://3.example/", "https://4.example/", "https://5.example/",
        "https://6.example/", "https://7.example/"
    };
    char text[KHOME_MAX_TABS * 32 + 32];
    TabList tabs;
    size_t n = 0;
    int i;

    n += (size_t)snprintf(text + n, sizeof(text) - n, "active=0\n");
    for (i = 0; i <= KHOME_MAX_TABS; i++) {
        n += (size_t)snprintf(text + n, sizeof(text) - n,
                              "https://%d.example/\n", i);
    }
    KH_CHECK_MSG(tabs_parse(&tabs, text) == KHOME_MAX_TABS,
                 "a ninth URL must be ignored");
    expect_list("parse capacity", &tabs, urls, KHOME_MAX_TABS, 0);
}

static void test_format_writes_header_then_urls(void)
{
    static const char *const urls[] = { "https://a.example/",
                                        "https://b.example/" };
    static const char expected[] =
        "active=1\nhttps://a.example/\nhttps://b.example/\n";
    TabList tabs;
    char out[128];

    init_tabs(&tabs, urls, 2, 1);
    KH_CHECK_MSG(tabs_format(&tabs, out, sizeof(out)) ==
                     (int)strlen(expected),
                 "format must return the written length");
    KH_CHECK_MSG(strcmp(out, expected) == 0,
                 "format: expected %s, got %s", expected, out);
}

static void test_format_rejects_short_buffer(void)
{
    static const char *const urls[] = { "https://a.example/",
                                        "https://b.example/" };
    static const char expected[] =
        "active=1\nhttps://a.example/\nhttps://b.example/\n";
    TabList tabs;
    char small[16];
    char exact[128];

    init_tabs(&tabs, urls, 2, 1);
    small[0] = 'x';
    KH_CHECK_MSG(tabs_format(&tabs, small, sizeof(small)) == -1,
                 "a buffer smaller than the text must be rejected");
    KH_CHECK_MSG(small[0] == 0, "a rejected format must leave an empty string");
    KH_CHECK_MSG(tabs_format(&tabs, exact, strlen(expected) + 1) ==
                     (int)strlen(expected),
                 "a buffer that fits the text and the NUL must succeed");
    KH_CHECK_MSG(strcmp(exact, expected) == 0,
                 "format at the exact capacity must write the whole text");
}

static void test_parse_format_parse_roundtrip(void)
{
    TabList first;
    TabList second;
    char text[256];
    char again[256];

    tabs_parse(&first, "\r\nactive=1\r\nhttps://a.example/\r\n\r\n"
                       "https://b.example/\r\nhttps://c.example/\r\n");
    KH_CHECK_MSG(tabs_format(&first, text, sizeof(text)) > 0,
                 "format of a parsed list must fit");
    tabs_parse(&second, text);
    KH_CHECK_MSG(second.count == first.count && second.active == first.active,
                 "roundtrip must keep the count and active index");
    KH_CHECK_MSG(strcmp(second.tabs[0].url, first.tabs[0].url) == 0 &&
                     strcmp(second.tabs[1].url, first.tabs[1].url) == 0 &&
                     strcmp(second.tabs[2].url, first.tabs[2].url) == 0,
                 "roundtrip must keep every URL");
    KH_CHECK_MSG(tabs_format(&second, again, sizeof(again)) > 0 &&
                     strcmp(again, text) == 0,
                 "a second format must be byte-identical");
}

static void test_open_appends_and_activates(void)
{
    static const char *const urls[] = { "https://a.example/",
                                        "https://b.example/",
                                        "https://c.example/" };
    TabList tabs;

    tabs_init(&tabs);
    KH_CHECK_MSG(tabs_open(&tabs, urls[0]) == 0,
                 "the first open must return index 0");
    KH_CHECK_MSG(tabs_open(&tabs, urls[1]) == 1,
                 "the second open must return index 1");
    KH_CHECK_MSG(tabs_open(&tabs, urls[2]) == 2,
                 "the third open must return index 2");
    expect_list("open appends", &tabs, urls, 3, 2);
}

static void test_open_rejects_invalid_urls(void)
{
    static const char *const urls[] = { "https://a.example/" };
    char too_long[KHOME_TAB_URL_MAX + 1];
    TabList tabs;

    init_tabs(&tabs, urls, 1, 0);
    memset(too_long, 'a', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = 0;
    KH_CHECK_MSG(tabs_open(&tabs, NULL) == -1, "NULL must be rejected");
    KH_CHECK_MSG(tabs_open(&tabs, "") == -1, "an empty URL must be rejected");
    KH_CHECK_MSG(tabs_open(&tabs, too_long) == -1,
                 "a 1024-byte URL must be rejected");
    expect_list("open rejects", &tabs, urls, 1, 0);
}

/* main.c's edit_commit tries tabs_set_active_url first and falls back to
 * tabs_open, which is how opening the editor with no tabs turns the typed URL
 * into a new active tab.  main.c has no test seam, so this pins the list
 * contract that fallback relies on. */
static void test_edit_commit_opens_a_first_tab(void)
{
    static const char *const url = "https://example.com/typed";
    TabList tabs;

    tabs_init(&tabs);
    KH_CHECK_MSG(tabs_set_active_url(&tabs, url) == -1,
                 "an empty list has no active tab for set_active_url");
    KH_CHECK_MSG(tabs_open(&tabs, url) == 0,
                 "the fallback must make the URL tab 0");
    KH_CHECK_MSG(tabs.count == 1 && tabs.active == 0,
                 "expected one active tab, got count %d active %d", tabs.count,
                 tabs.active);
    KH_CHECK_MSG(tabs_active_url(&tabs) != NULL &&
                     strcmp(tabs_active_url(&tabs), url) == 0,
                 "the typed URL must be the active tab's URL");
}

static void test_open_when_full_drops_oldest_background_tab(void)
{
    static const char *const opened[] = {
        "https://0.example/", "https://1.example/", "https://2.example/",
        "https://3.example/", "https://4.example/", "https://5.example/",
        "https://6.example/", "https://7.example/", "https://8.example/"
    };
    static const char *const after_middle[] = {
        "https://1.example/", "https://2.example/", "https://3.example/",
        "https://4.example/", "https://5.example/", "https://6.example/",
        "https://7.example/", "https://8.example/"
    };
    static const char *const after_first[] = {
        "https://0.example/", "https://2.example/", "https://3.example/",
        "https://4.example/", "https://5.example/", "https://6.example/",
        "https://7.example/", "https://8.example/"
    };
    TabList tabs;

    init_tabs(&tabs, opened, KHOME_MAX_TABS, 3);
    KH_CHECK_MSG(tabs_open(&tabs, opened[8]) == KHOME_MAX_TABS - 1,
                 "opening into a full list must activate the new last tab");
    expect_list("full list, active 3 drops tab 0", &tabs, after_middle,
                KHOME_MAX_TABS, KHOME_MAX_TABS - 1);

    init_tabs(&tabs, opened, KHOME_MAX_TABS, 0);
    tabs_open(&tabs, opened[8]);
    expect_list("full list, active 0 drops tab 1", &tabs, after_first,
                KHOME_MAX_TABS, KHOME_MAX_TABS - 1);
}

static void test_close_active_activates_replacement(void)
{
    static const char *const urls[] = { "https://a.example/",
                                        "https://b.example/",
                                        "https://c.example/" };
    static const char *const left[] = { "https://a.example/",
                                        "https://c.example/" };
    TabList tabs;

    init_tabs(&tabs, urls, 3, 1);
    KH_CHECK_MSG(tabs_close(&tabs, 1) == 1,
                 "closing the active middle tab must return the new active");
    expect_list("close active middle", &tabs, left, 2, 1);
    KH_CHECK_MSG(strcmp(tabs_active_url(&tabs), "https://c.example/") == 0,
                 "the tab that slid into the slot must be active");
}

static void test_close_active_last_activates_previous(void)
{
    static const char *const urls[] = { "https://a.example/",
                                        "https://b.example/",
                                        "https://c.example/" };
    static const char *const left[] = { "https://a.example/",
                                        "https://b.example/" };
    TabList tabs;

    init_tabs(&tabs, urls, 3, 2);
    KH_CHECK_MSG(tabs_close(&tabs, 2) == 1,
                 "closing the active last tab must return the previous index");
    expect_list("close active last", &tabs, left, 2, 1);
    KH_CHECK_MSG(strcmp(tabs_active_url(&tabs), "https://b.example/") == 0,
                 "the previous tab must be active");
}

static void test_close_background_keeps_active_tab(void)
{
    static const char *const urls[] = { "https://a.example/",
                                        "https://b.example/",
                                        "https://c.example/" };
    static const char *const before[] = { "https://b.example/",
                                          "https://c.example/" };
    static const char *const after[] = { "https://a.example/",
                                         "https://b.example/" };
    TabList tabs;

    init_tabs(&tabs, urls, 3, 2);
    KH_CHECK_MSG(tabs_close(&tabs, 0) == 1,
                 "closing a tab before the active one must shift its index");
    expect_list("close before active", &tabs, before, 2, 1);
    KH_CHECK_MSG(strcmp(tabs_active_url(&tabs), "https://c.example/") == 0,
                 "the same tab must stay active");

    init_tabs(&tabs, urls, 3, 0);
    KH_CHECK_MSG(tabs_close(&tabs, 2) == 0,
                 "closing a tab after the active one must keep its index");
    expect_list("close after active", &tabs, after, 2, 0);
    KH_CHECK_MSG(strcmp(tabs_active_url(&tabs), "https://a.example/") == 0,
                 "the same tab must stay active");
}

static void test_close_last_tab_empties_list(void)
{
    static const char *const urls[] = { "https://a.example/" };
    TabList tabs;

    init_tabs(&tabs, urls, 1, 0);
    KH_CHECK_MSG(tabs_close(&tabs, 0) == -1,
                 "closing the only tab must report no active index");
    KH_CHECK_MSG(tabs.count == 0, "the list must become empty");
    KH_CHECK_MSG(tabs.active == -1, "an empty list must have active -1");
    KH_CHECK_MSG(tabs_active_url(&tabs) == NULL,
                 "an empty list must have no active URL");
}

static void test_close_out_of_range_does_nothing(void)
{
    static const char *const urls[] = { "https://a.example/",
                                        "https://b.example/" };
    TabList tabs;

    init_tabs(&tabs, urls, 2, 0);
    KH_CHECK_MSG(tabs_close(&tabs, -1) == -1, "index -1 must be rejected");
    KH_CHECK_MSG(tabs_close(&tabs, 2) == -1, "index == count must be rejected");
    expect_list("close out of range", &tabs, urls, 2, 0);
}

static void test_select_changes_active(void)
{
    static const char *const urls[] = { "https://a.example/",
                                        "https://b.example/",
                                        "https://c.example/" };
    TabList tabs;

    init_tabs(&tabs, urls, 3, 0);
    KH_CHECK_MSG(tabs_select(&tabs, 2) == 2, "select must return the index");
    KH_CHECK_MSG(tabs.active == 2 && tabs_active_url(&tabs) != NULL &&
                     strcmp(tabs_active_url(&tabs), urls[2]) == 0,
                 "select must change the active URL");
    KH_CHECK_MSG(tabs_select(&tabs, -1) == -1, "index -1 must be rejected");
    KH_CHECK_MSG(tabs_select(&tabs, 3) == -1,
                 "index == count must be rejected");
    KH_CHECK_MSG(tabs.active == 2, "a rejected select must not change active");

    tabs_init(&tabs);
    KH_CHECK_MSG(tabs_select(&tabs, 0) == -1,
                 "an empty list must have no tab to select");
    KH_CHECK_MSG(tabs.active == -1,
                 "a rejected select on an empty list must keep active -1");
}

static void test_set_active_url_replaces_only_the_active_tab(void)
{
    static const char *const urls[] = { "https://a.example/",
                                        "https://b.example/" };
    static const char *const after[] = { "https://z.example/",
                                         "https://b.example/" };
    char too_long[KHOME_TAB_URL_MAX + 1];
    TabList tabs;

    init_tabs(&tabs, urls, 2, 0);
    KH_CHECK_MSG(tabs_set_active_url(&tabs, after[0]) == 0,
                 "setting a valid URL must succeed");
    expect_list("set active URL", &tabs, after, 2, 0);

    memset(too_long, 'a', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = 0;
    KH_CHECK_MSG(tabs_set_active_url(&tabs, too_long) == -1,
                 "a 1024-byte URL must be rejected");
    expect_list("rejected set keeps the list", &tabs, after, 2, 0);

    tabs_init(&tabs);
    KH_CHECK_MSG(tabs_set_active_url(&tabs, "https://a.example/") == -1,
                 "an empty list must have no active tab to change");
}

static void test_active_url_follows_active(void)
{
    static const char *const urls[] = { "https://a.example/",
                                        "https://b.example/" };
    TabList tabs;

    tabs_init(&tabs);
    KH_CHECK_MSG(tabs_active_url(&tabs) == NULL,
                 "an empty list must have no active URL");
    tabs_open(&tabs, urls[0]);
    tabs_open(&tabs, urls[1]);
    tabs_select(&tabs, 0);
    KH_CHECK_MSG(strcmp(tabs_active_url(&tabs), urls[0]) == 0,
                 "active_url must follow the selected tab");
    tabs_select(&tabs, 1);
    KH_CHECK_MSG(strcmp(tabs_active_url(&tabs), urls[1]) == 0,
                 "active_url must follow the selected tab");
}

static void test_url_parts_strips_scheme_userinfo_and_www(void)
{
    char host[KHOME_TAB_HOST_MAX];
    char path[KHOME_TAB_PATH_MAX];

    tabs_url_parts("https://user:pass@www.example.com/news/today?x=1#top",
                   host, sizeof(host), path, sizeof(path));
    KH_CHECK_MSG(strcmp(host, "example.com") == 0,
                 "host: expected example.com, got %s", host);
    KH_CHECK_MSG(strcmp(path, "/news/today") == 0,
                 "path: expected /news/today, got %s", path);
}

static void test_url_parts_defaults_and_plain_host(void)
{
    char host[KHOME_TAB_HOST_MAX];
    char path[KHOME_TAB_PATH_MAX];

    tabs_url_parts("example.com", host, sizeof(host), path, sizeof(path));
    KH_CHECK_MSG(strcmp(host, "example.com") == 0,
                 "no-scheme host: expected example.com, got %s", host);
    KH_CHECK_MSG(strcmp(path, "/") == 0,
                 "a URL without a path must show /, got %s", path);

    tabs_url_parts("https://www.example.com", host, sizeof(host), path,
                   sizeof(path));
    KH_CHECK_MSG(strcmp(host, "example.com") == 0,
                 "host-only URL: expected example.com, got %s", host);
    KH_CHECK_MSG(strcmp(path, "/") == 0,
                 "a URL without a path must show /, got %s", path);

    tabs_url_parts("", host, sizeof(host), path, sizeof(path));
    KH_CHECK_MSG(strcmp(host, "") == 0,
                 "an empty URL must have an empty host, got %s", host);
    KH_CHECK_MSG(strcmp(path, "/") == 0,
                 "an empty URL must show /, got %s", path);
}

static void test_url_parts_drops_query_and_fragment(void)
{
    char host[KHOME_TAB_HOST_MAX];
    char path[KHOME_TAB_PATH_MAX];

    tabs_url_parts("https://www.example.com/a/b?c=d#e", host, sizeof(host),
                   path, sizeof(path));
    KH_CHECK_MSG(strcmp(path, "/a/b") == 0,
                 "the path must stop before ? and #, got %s", path);

    tabs_url_parts("https://example.com/p?q", host, sizeof(host), path,
                   sizeof(path));
    KH_CHECK_MSG(strcmp(path, "/p") == 0,
                 "the path must stop before ?, got %s", path);

    tabs_url_parts("https://example.com/", host, sizeof(host), path,
                   sizeof(path));
    KH_CHECK_MSG(strcmp(path, "/") == 0,
                 "the root path must stay /, got %s", path);
}

static void test_url_parts_shortens_long_path_to_24_chars(void)
{
    char host[KHOME_TAB_HOST_MAX];
    char path[KHOME_TAB_PATH_MAX];

    tabs_url_parts("https://example.com/abcdefghijklmnopqrstuvwxyz0123456789",
                   host, sizeof(host), path, sizeof(path));
    KH_CHECK_MSG(strcmp(path, "/abcdefghijklmnopqrstuvw\xE2\x80\xA6") == 0,
                 "a 37-char path must be 24 chars plus an ellipsis, got %s",
                 path);
    KH_CHECK_MSG(strlen(path) == 24 + 3,
                 "the shortened path must be 24 characters plus U+2026");

    tabs_url_parts("https://example.com/abcdefghijklmnopqrstuvw", host,
                   sizeof(host), path, sizeof(path));
    KH_CHECK_MSG(strcmp(path, "/abcdefghijklmnopqrstuvw") == 0,
                 "a path of exactly 24 characters must not be shortened, got "
                 "%s",
                 path);
}

static void test_url_parts_counts_shortening_in_characters(void)
{
    char url[192];
    char expected[KHOME_TAB_PATH_MAX];
    char host[KHOME_TAB_HOST_MAX];
    char path[KHOME_TAB_PATH_MAX];
    int i;

    strcpy(url, "https://example.com/");
    for (i = 0; i < 25; i++) {
        strcat(url, "\xE3\x81\x82");
    }
    strcpy(expected, "/");
    for (i = 0; i < 23; i++) {
        strcat(expected, "\xE3\x81\x82");
    }
    strcat(expected, "\xE2\x80\xA6");

    tabs_url_parts(url, host, sizeof(host), path, sizeof(path));
    KH_CHECK_MSG(strcmp(path, expected) == 0,
                 "a 25-character multibyte path must be cut at 24 characters "
                 "plus an ellipsis, got %s",
                 path);
}

static void test_encode_keeps_unreserved_characters(void)
{
    static const char unreserved[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        "-_.!~*'()";
    char out[256];

    KH_CHECK_MSG(tabs_encode_component(unreserved, out, sizeof(out)) ==
                     (int)strlen(unreserved),
                 "encode must return the written length");
    KH_CHECK_MSG(strcmp(out, unreserved) == 0,
                 "the unreserved set must stay literal, got %s", out);
}

static void test_encode_percent_encodes_reserved_and_utf8(void)
{
    char out[256];
    int len;

    len = tabs_encode_component("a b&c/d?e#f%g", out, sizeof(out));
    KH_CHECK_MSG(len == (int)strlen("a%20b%26c%2Fd%3Fe%23f%25g"),
                 "encode must return the encoded length");
    KH_CHECK_MSG(strcmp(out, "a%20b%26c%2Fd%3Fe%23f%25g") == 0,
                 "space & / ? # %% must be percent-encoded, got %s", out);

    tabs_encode_component("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", out,
                          sizeof(out));
    KH_CHECK_MSG(strcmp(out, "%E6%97%A5%E6%9C%AC%E8%AA%9E") == 0,
                 "each UTF-8 byte must be an uppercase %%XX, got %s", out);
}

static void test_encode_rejects_short_buffer(void)
{
    char exact[8];
    char small[5];

    KH_CHECK_MSG(tabs_encode_component("", exact, sizeof(exact)) == 0,
                 "an empty string must encode to length 0");
    KH_CHECK_MSG(exact[0] == 0, "an empty string must encode to \"\"");

    KH_CHECK_MSG(tabs_encode_component("a b", exact, sizeof(exact)) == 5,
                 "\"a b\" must encode to five characters");
    KH_CHECK_MSG(strcmp(exact, "a%20b") == 0,
                 "the exact-capacity buffer must hold a%%20b, got %s", exact);

    small[0] = 'x';
    KH_CHECK_MSG(tabs_encode_component("a b", small, sizeof(small)) == -1,
                 "a buffer that cannot hold the result must be rejected");
    KH_CHECK_MSG(small[0] == 0,
                 "a rejected encode must leave an empty string");
}

void kh_run_tabs_tests(void)
{
    printf("\n[tabs]\n");
    kh_run_test("tabs.parse_header_keeps_order_and_active",
                test_parse_header_keeps_order_and_active);
    kh_run_test("tabs.parse_without_header_activates_first_url",
                test_parse_without_header_activates_first_url);
    kh_run_test("tabs.parse_ignores_blank_lines_and_crlf",
                test_parse_ignores_blank_lines_and_crlf);
    kh_run_test("tabs.parse_skips_a_utf8_bom", test_parse_skips_a_utf8_bom);
    kh_run_test("tabs.parse_skips_url_at_or_over_length_limit",
                test_parse_skips_url_at_or_over_length_limit);
    kh_run_test("tabs.parse_stops_at_tab_capacity",
                test_parse_stops_at_tab_capacity);
    kh_run_test("tabs.format_writes_header_then_urls",
                test_format_writes_header_then_urls);
    kh_run_test("tabs.format_rejects_short_buffer",
                test_format_rejects_short_buffer);
    kh_run_test("tabs.parse_format_parse_roundtrip",
                test_parse_format_parse_roundtrip);
    kh_run_test("tabs.open_appends_and_activates",
                test_open_appends_and_activates);
    kh_run_test("tabs.open_rejects_invalid_urls",
                test_open_rejects_invalid_urls);
    kh_run_test("tabs.edit_commit_opens_a_first_tab",
                test_edit_commit_opens_a_first_tab);
    kh_run_test("tabs.open_when_full_drops_oldest_background_tab",
                test_open_when_full_drops_oldest_background_tab);
    kh_run_test("tabs.close_active_activates_replacement",
                test_close_active_activates_replacement);
    kh_run_test("tabs.close_active_last_activates_previous",
                test_close_active_last_activates_previous);
    kh_run_test("tabs.close_background_keeps_active_tab",
                test_close_background_keeps_active_tab);
    kh_run_test("tabs.close_last_tab_empties_list",
                test_close_last_tab_empties_list);
    kh_run_test("tabs.close_out_of_range_does_nothing",
                test_close_out_of_range_does_nothing);
    kh_run_test("tabs.select_changes_active", test_select_changes_active);
    kh_run_test("tabs.set_active_url_replaces_only_the_active_tab",
                test_set_active_url_replaces_only_the_active_tab);
    kh_run_test("tabs.active_url_follows_active",
                test_active_url_follows_active);
    kh_run_test("tabs.url_parts_strips_scheme_userinfo_and_www",
                test_url_parts_strips_scheme_userinfo_and_www);
    kh_run_test("tabs.url_parts_defaults_and_plain_host",
                test_url_parts_defaults_and_plain_host);
    kh_run_test("tabs.url_parts_drops_query_and_fragment",
                test_url_parts_drops_query_and_fragment);
    kh_run_test("tabs.url_parts_shortens_long_path_to_24_chars",
                test_url_parts_shortens_long_path_to_24_chars);
    kh_run_test("tabs.url_parts_counts_shortening_in_characters",
                test_url_parts_counts_shortening_in_characters);
    kh_run_test("tabs.encode_keeps_unreserved_characters",
                test_encode_keeps_unreserved_characters);
    kh_run_test("tabs.encode_percent_encodes_reserved_and_utf8",
                test_encode_percent_encodes_reserved_and_utf8);
    kh_run_test("tabs.encode_rejects_short_buffer",
                test_encode_rejects_short_buffer);
}
