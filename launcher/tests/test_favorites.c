#include "suites.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canvas.h"
#include "fixtures.h"
#include "layout.h"
#include "state.h"
#include "tabs.h"
#include "test_harness.h"

/* Favorites are the browser bookmarks khome owns.  The list is a pure value
 * with a one-line-per-entry text form, the home tiles open an entry by its
 * index, and the tab sheet toggles the active URL.  Tests drive the public
 * functions and the tap geometry only. */

static void expect_favorite(const char *label, const Favorite *fav, int index,
                            const char *url, const char *name)
{
    KH_CHECK_MSG(strcmp(fav->url, url) == 0,
                 "%s: favorite %d url: expected \"%s\", got \"%s\"", label,
                 index, url, fav->url);
    KH_CHECK_MSG(strcmp(fav->name, name) == 0,
                 "%s: favorite %d name: expected \"%s\", got \"%s\"", label,
                 index, name, fav->name);
}

static void test_parse_bom_and_crlf(void)
{
    Favorite favs[KHOME_MAX_FAVORITES];
    int count = favorites_parse(favs, KHOME_MAX_FAVORITES,
                                "\xEF\xBB\xBF"
                                "https://a.example/\tAlpha\r\n"
                                "\r\n"
                                "https://b.example/\tBeta\r\n");

    KH_CHECK_MSG(count == 2, "BOM and CRLF: expected 2 entries, got %d",
                 count);
    if (count == 2) {
        expect_favorite("BOM and CRLF", &favs[0], 0, "https://a.example/",
                        "Alpha");
        expect_favorite("BOM and CRLF", &favs[1], 1, "https://b.example/",
                        "Beta");
    }
}

static void test_parse_skips_malformed_lines(void)
{
    Favorite favs[KHOME_MAX_FAVORITES];
    int count = favorites_parse(favs, KHOME_MAX_FAVORITES,
                                "\tOnly a title\n"
                                "https://a.example/\tAlpha\n"
                                "https://b.example/\tBeta\n");

    KH_CHECK_MSG(count == 2,
                 "lines with an empty URL: expected 2 entries, got %d",
                 count);
    if (count == 2) {
        expect_favorite("malformed lines", &favs[0], 0, "https://a.example/",
                        "Alpha");
        expect_favorite("malformed lines", &favs[1], 1, "https://b.example/",
                        "Beta");
    }
}

static void test_parse_keeps_first_duplicate(void)
{
    Favorite favs[KHOME_MAX_FAVORITES];
    int count = favorites_parse(favs, KHOME_MAX_FAVORITES,
                                "https://a.example/\tFirst\n"
                                "https://b.example/\tBee\n"
                                "https://a.example/\tSecond\n");

    KH_CHECK_MSG(count == 2, "duplicate URL: expected 2 entries, got %d",
                 count);
    if (count == 2) {
        expect_favorite("duplicate URL", &favs[0], 0, "https://a.example/",
                        "First");
        expect_favorite("duplicate URL", &favs[1], 1, "https://b.example/",
                        "Bee");
    }
    KH_CHECK_MSG(favorites_find(favs, count, "https://a.example/") == 0,
                 "duplicate URL: the first line must stay at index 0");
}

static void test_parse_empty_title_uses_host(void)
{
    Favorite favs[KHOME_MAX_FAVORITES];
    int count = favorites_parse(favs, KHOME_MAX_FAVORITES,
                                "https://www.example.com/path?q=1\t\n"
                                "https://plain.example/\t\r\n");

    KH_CHECK_MSG(count == 2, "empty titles: expected 2 entries, got %d",
                 count);
    if (count == 2) {
        expect_favorite("empty title", &favs[0], 0,
                        "https://www.example.com/path?q=1", "example.com");
        expect_favorite("empty title", &favs[1], 1, "https://plain.example/",
                        "plain.example");
    }
}

/* A line without a tab is a bare URL.  The title stays empty, so the name
 * falls back to the URL's host, the same as favorites_add(url, ""). */
static void test_parse_bare_url_uses_host_name(void)
{
    Favorite favs[KHOME_MAX_FAVORITES];
    Favorite added[KHOME_MAX_FAVORITES];
    int added_count = 0;
    int count = favorites_parse(favs, KHOME_MAX_FAVORITES,
                                "https://www.example.com/path?q=1\n"
                                "https://plain.example/\n");

    KH_CHECK_MSG(count == 2, "bare URLs: expected 2 entries, got %d", count);
    if (count == 2) {
        expect_favorite("bare URL", &favs[0], 0,
                        "https://www.example.com/path?q=1", "example.com");
        expect_favorite("bare URL", &favs[1], 1, "https://plain.example/",
                        "plain.example");
    }
    memset(added, 0, sizeof(added));
    (void)favorites_add(added, &added_count, KHOME_MAX_FAVORITES,
                        "https://www.example.com/path?q=1", "");
    KH_CHECK_MSG(count > 0 && added_count == 1 &&
                     strcmp(favs[0].name, added[0].name) == 0,
                 "bare URL name \"%s\" must match favorites_add's \"%s\"",
                 count > 0 ? favs[0].name : "",
                 added_count == 1 ? added[0].name : "");
}

static void test_parse_skips_blank_and_whitespace_lines(void)
{
    Favorite favs[KHOME_MAX_FAVORITES];
    int count = favorites_parse(favs, KHOME_MAX_FAVORITES,
                                "\r\n"
                                "   \n"
                                "\t\n"
                                "\v\f\n"
                                "https://a.example/\n"
                                "https://b.example/\tBeta\n");

    KH_CHECK_MSG(count == 2,
                 "blank and whitespace-only lines: expected 2 entries, got %d",
                 count);
    if (count == 2) {
        expect_favorite("whitespace lines", &favs[0], 0, "https://a.example/",
                        "a.example");
        expect_favorite("whitespace lines", &favs[1], 1, "https://b.example/",
                        "Beta");
    }
}

/* A whitespace-only line can also contain the tab that separates a title, so
 * the blank check has to look at the whole line before splitting it. */
static void test_parse_skips_whitespace_only_line_with_tab(void)
{
    Favorite favs[KHOME_MAX_FAVORITES];
    int count = favorites_parse(favs, KHOME_MAX_FAVORITES,
                                " \t \n"
                                "https://a.example/\n"
                                "https://b.example/\tBeta\n");

    KH_CHECK_MSG(count == 2,
                 "whitespace-only line with a tab: expected 2 entries, got %d",
                 count);
    if (count == 2) {
        expect_favorite("whitespace with tab", &favs[0], 0,
                        "https://a.example/", "a.example");
        expect_favorite("whitespace with tab", &favs[1], 1,
                        "https://b.example/", "Beta");
    }
}

static void test_parse_skips_url_at_or_over_length_limit(void)
{
    char text[KHOME_TAB_URL_MAX * 2 + 64];
    char at_limit[KHOME_TAB_URL_MAX];
    char over_limit[KHOME_TAB_URL_MAX + 1];
    Favorite favs[KHOME_MAX_FAVORITES];
    size_t prefix;
    size_t n = 0;
    int count;

    prefix = strlen("https://long.example/");
    memcpy(at_limit, "https://long.example/", prefix);
    memset(at_limit + prefix, 'a', sizeof(at_limit) - 1 - prefix);
    at_limit[sizeof(at_limit) - 1] = 0;
    prefix = strlen("https://over.example/");
    memcpy(over_limit, "https://over.example/", prefix);
    memset(over_limit + prefix, 'b', sizeof(over_limit) - 1 - prefix);
    over_limit[sizeof(over_limit) - 1] = 0;

    n += (size_t)snprintf(text + n, sizeof(text) - n, "%s\n", at_limit);
    n += (size_t)snprintf(text + n, sizeof(text) - n, "%s\n", over_limit);
    snprintf(text + n, sizeof(text) - n, "%s\n", "https://last.example/");

    count = favorites_parse(favs, KHOME_MAX_FAVORITES, text);
    KH_CHECK_MSG(count == 2,
                 "bare URL length limit: expected 2 entries, got %d", count);
    if (count == 2) {
        expect_favorite("length limit", &favs[0], 0, at_limit,
                        "long.example");
        expect_favorite("length limit", &favs[1], 1, "https://last.example/",
                        "last.example");
    }
}

static void test_parse_bare_url_with_bom_and_crlf(void)
{
    Favorite favs[KHOME_MAX_FAVORITES];
    int count = favorites_parse(favs, KHOME_MAX_FAVORITES,
                                "\xEF\xBB\xBF"
                                "https://a.example/\r\n"
                                "https://www.b.example/path\r\n");

    KH_CHECK_MSG(count == 2,
                 "bare URLs with a BOM and CRLF: expected 2 entries, got %d",
                 count);
    if (count == 2) {
        expect_favorite("bare URL CRLF", &favs[0], 0, "https://a.example/",
                        "a.example");
        expect_favorite("bare URL CRLF", &favs[1], 1,
                        "https://www.b.example/path", "b.example");
    }
}

static void test_parse_keeps_first_bare_duplicate(void)
{
    Favorite favs[KHOME_MAX_FAVORITES];
    int count = favorites_parse(favs, KHOME_MAX_FAVORITES,
                                "https://a.example/\n"
                                "https://b.example/\tBee\n"
                                "https://a.example/\tSecond\n"
                                "https://b.example/\n");

    KH_CHECK_MSG(count == 2, "bare URL duplicate: expected 2 entries, got %d",
                 count);
    if (count == 2) {
        expect_favorite("bare duplicate", &favs[0], 0, "https://a.example/",
                        "a.example");
        expect_favorite("bare duplicate", &favs[1], 1, "https://b.example/",
                        "Bee");
    }
    KH_CHECK_MSG(favorites_find(favs, count, "https://a.example/") == 0,
                 "bare URL duplicate: the first line must stay at index 0");
}

static void test_parse_stops_at_capacity(void)
{
    Favorite favs[KHOME_MAX_FAVORITES];
    char text[KHOME_MAX_FAVORITES * 40 + 80];
    size_t used = 0;
    int i;
    int count;

    for (i = 0; i < KHOME_MAX_FAVORITES + 1; i++) {
        used += (size_t)snprintf(text + used, sizeof(text) - used,
                                 "https://%02d.example/\tFav %02d\n", i, i);
    }
    count = favorites_parse(favs, KHOME_MAX_FAVORITES, text);
    KH_CHECK_MSG(count == KHOME_MAX_FAVORITES,
                 "capacity: expected %d entries, got %d", KHOME_MAX_FAVORITES,
                 count);
    if (count == KHOME_MAX_FAVORITES) {
        expect_favorite("capacity", &favs[KHOME_MAX_FAVORITES - 1],
                        KHOME_MAX_FAVORITES - 1, "https://63.example/",
                        "Fav 63");
    }
    KH_CHECK_MSG(favorites_find(favs, count, "https://64.example/") == -1,
                 "capacity: the 65th URL must be ignored");
    KH_CHECK_MSG(favorites_parse(favs, 2, text) == 2,
                 "capacity: max 2 must read exactly 2 entries");
}

static void test_format_round_trips_parse(void)
{
    Favorite first[KHOME_MAX_FAVORITES];
    Favorite second[KHOME_MAX_FAVORITES];
    char out[1024];
    char tiny[8];
    int count;
    int len;
    int again;
    int i;

    count = favorites_parse(first, KHOME_MAX_FAVORITES,
                            "\xEF\xBB\xBF"
                            "https://a.example/\tAlpha\r\n"
                            "https://www.b.example/path\t\r\n");
    KH_CHECK_MSG(count == 2, "format round trip: expected 2 entries, got %d",
                 count);
    len = favorites_format(first, count, out, sizeof(out));
    KH_CHECK_MSG(len >= 0 && len == (int)strlen(out),
                 "format: expected the length %d, got %d for \"%s\"",
                 (int)strlen(out), len, out);
    KH_CHECK_MSG(strcmp(out, "https://a.example/\tAlpha\n"
                             "https://www.b.example/path\tb.example\n") == 0,
                 "format: expected normalized lines, got \"%s\"", out);

    again = favorites_parse(second, KHOME_MAX_FAVORITES, out);
    KH_CHECK_MSG(again == count,
                 "format round trip: expected %d entries, got %d", count,
                 again);
    for (i = 0; i < count && i < again; i++) {
        expect_favorite("format round trip", &second[i], i, first[i].url,
                        first[i].name);
    }
    KH_CHECK_MSG(favorites_format(first, count, tiny, sizeof(tiny)) == -1 &&
                     tiny[0] == 0,
                 "format: a short buffer must fail and leave an empty string");
}

static void test_format_round_trips_bare_url(void)
{
    Favorite first[KHOME_MAX_FAVORITES];
    Favorite second[KHOME_MAX_FAVORITES];
    char out[1024];
    int count;
    int len;
    int again;
    int i;

    count = favorites_parse(first, KHOME_MAX_FAVORITES,
                            "https://www.example.com/path\n"
                            "https://b.example/\tBeta\n");
    KH_CHECK_MSG(count == 2, "bare URL round trip: expected 2 entries, got %d",
                 count);
    len = favorites_format(first, count, out, sizeof(out));
    KH_CHECK_MSG(len >= 0 && len == (int)strlen(out),
                 "bare URL format: expected the length %d, got %d for \"%s\"",
                 (int)strlen(out), len, out);
    KH_CHECK_MSG(strcmp(out, "https://www.example.com/path\texample.com\n"
                             "https://b.example/\tBeta\n") == 0,
                 "bare URL format: expected the host as the title, got \"%s\"",
                 out);

    again = favorites_parse(second, KHOME_MAX_FAVORITES, out);
    KH_CHECK_MSG(again == count,
                 "bare URL round trip: expected %d entries, got %d", count,
                 again);
    for (i = 0; i < count && i < again; i++) {
        expect_favorite("bare URL round trip", &second[i], i, first[i].url,
                        first[i].name);
    }
    KH_CHECK_MSG(favorites_find(second, again,
                                "https://www.example.com/path") == 0,
                 "bare URL round trip: the URL-only line must survive a save");
}

static void test_find_matches_exact_url(void)
{
    Favorite favs[KHOME_MAX_FAVORITES];
    int count = favorites_parse(favs, KHOME_MAX_FAVORITES,
                                "https://a.example/\tAlpha\n"
                                "https://b.example/\tBeta\n");

    KH_CHECK_MSG(favorites_find(favs, count, "https://a.example/") == 0,
                 "find: https://a.example/ must be at index 0");
    KH_CHECK_MSG(favorites_find(favs, count, "https://b.example/") == 1,
                 "find: https://b.example/ must be at index 1");
    KH_CHECK_MSG(favorites_find(favs, count, "https://c.example/") == -1,
                 "find: a missing URL must return -1");
    KH_CHECK_MSG(favorites_find(favs, count, "https://a.example") == -1,
                 "find: URLs must match exactly, including the trailing slash");
}

static void test_add_appends_and_rejects_duplicates(void)
{
    Favorite favs[KHOME_MAX_FAVORITES];
    int count = 0;
    int index;

    memset(favs, 0, sizeof(favs));
    index = favorites_add(favs, &count, KHOME_MAX_FAVORITES,
                          "https://a.example/", "Alpha");
    KH_CHECK_MSG(index == 0 && count == 1,
                 "add: expected index 0 and count 1, got %d and %d", index,
                 count);
    if (index == 0) {
        expect_favorite("add", &favs[0], 0, "https://a.example/", "Alpha");
    }

    index = favorites_add(favs, &count, KHOME_MAX_FAVORITES,
                          "https://b.example/", NULL);
    KH_CHECK_MSG(index == 1 && count == 2,
                 "add without a title: expected index 1 and count 2, got %d "
                 "and %d",
                 index, count);
    if (index == 1) {
        expect_favorite("add without a title", &favs[1], 1,
                        "https://b.example/", "b.example");
    }

    index = favorites_add(favs, &count, KHOME_MAX_FAVORITES,
                          "https://www.c.example/x", "");
    KH_CHECK_MSG(index == 2 && count == 3,
                 "add with an empty title: expected index 2 and count 3, got "
                 "%d and %d",
                 index, count);
    if (index == 2) {
        expect_favorite("add with an empty title", &favs[2], 2,
                        "https://www.c.example/x", "c.example");
    }

    index = favorites_add(favs, &count, KHOME_MAX_FAVORITES,
                          "https://a.example/", "Again");
    KH_CHECK_MSG(index == -1 && count == 3,
                 "add duplicate: expected -1 and count 3, got %d and %d",
                 index, count);
    expect_favorite("add duplicate", &favs[0], 0, "https://a.example/",
                    "Alpha");
    KH_CHECK_MSG(favorites_add(favs, &count, KHOME_MAX_FAVORITES, "",
                               "Empty") == -1 &&
                     count == 3,
                 "add with an empty URL: expected -1 and an unchanged list");
}

static void test_add_fails_when_full(void)
{
    Favorite favs[KHOME_MAX_FAVORITES];
    char url[64];
    int count = 0;
    int i;

    for (i = 0; i < KHOME_MAX_FAVORITES; i++) {
        int index;

        snprintf(url, sizeof(url), "https://%02d.example/", i);
        index = favorites_add(favs, &count, KHOME_MAX_FAVORITES, url, "Fav");
        KH_CHECK_MSG(index == i, "add %d: expected index %d, got %d", i, i,
                     index);
    }
    KH_CHECK_MSG(count == KHOME_MAX_FAVORITES,
                 "add at capacity: expected count %d, got %d",
                 KHOME_MAX_FAVORITES, count);
    KH_CHECK_MSG(favorites_add(favs, &count, KHOME_MAX_FAVORITES,
                               "https://overflow.example/", "Overflow") == -1 &&
                     count == KHOME_MAX_FAVORITES,
                 "add past capacity: expected -1 and count %d, got %d",
                 KHOME_MAX_FAVORITES, count);
    KH_CHECK_MSG(favorites_find(favs, count, "https://overflow.example/") ==
                     -1,
                 "add past capacity: the extra URL must not be stored");
}

static void test_remove_slides_and_rejects_out_of_range(void)
{
    Favorite favs[KHOME_MAX_FAVORITES];
    int count = favorites_parse(favs, KHOME_MAX_FAVORITES,
                                "https://a.example/\tAlpha\n"
                                "https://b.example/\tBeta\n"
                                "https://c.example/\tGamma\n");

    KH_CHECK_MSG(count == 3, "remove: expected 3 entries, got %d", count);
    KH_CHECK_MSG(favorites_remove(favs, &count, -1) == -1 && count == 3,
                 "remove below range: expected -1 and count 3");
    KH_CHECK_MSG(favorites_remove(favs, &count, 3) == -1 && count == 3,
                 "remove at count: expected -1 and count 3");

    KH_CHECK_MSG(favorites_remove(favs, &count, 1) == 0 && count == 2,
                 "remove middle: expected 0 and count 2, got %d", count);
    if (count == 2) {
        expect_favorite("remove middle", &favs[0], 0, "https://a.example/",
                        "Alpha");
        expect_favorite("remove middle", &favs[1], 1, "https://c.example/",
                        "Gamma");
    }
    KH_CHECK_MSG(favorites_remove(favs, &count, 1) == 0 && count == 1,
                 "remove last: expected 0 and count 1");
    KH_CHECK_MSG(favorites_remove(favs, &count, 0) == 0 && count == 0,
                 "remove the only entry: expected 0 and count 0");
    KH_CHECK_MSG(favorites_remove(favs, &count, 0) == -1 && count == 0,
                 "remove from an empty list: expected -1 and count 0");
}

static KhomeMachine at_state(KhomeState state)
{
    KhomeMachine machine;

    machine.state = state;
    machine.asleep = 0;
    machine.confirm = KHOME_CONFIRM_NONE;
    return machine;
}

static void expect_step(const char *label, KhomeStep got, KhomeState state,
                        const KhomeAction *actions, int action_count)
{
    int i;

    KH_CHECK_MSG(got.state == state, "%s: expected state %s, got %s", label,
                 khome_state_name(state), khome_state_name(got.state));
    KH_CHECK_MSG(got.action_count == action_count,
                 "%s: expected %d actions, got %d", label, action_count,
                 got.action_count);
    for (i = 0; i < action_count && i < got.action_count; i++) {
        KH_CHECK_MSG(got.actions[i] == actions[i],
                     "%s: action %d: expected %s, got %s", label, i,
                     khome_action_name(actions[i]),
                     khome_action_name(got.actions[i]));
    }
}

static void test_home_favorite_opens_browser(void)
{
    static const KhomeAction actions[] = { KHOME_ACT_UNGRAB,
                                           KHOME_ACT_FAVORITE_OPEN };

    expect_step("HOME + tap-favorite",
                khome_step(at_state(KHOME_STATE_HOME), KHOME_EV_TAP_FAVORITE),
                KHOME_STATE_BROWSER, actions, 2);
}

static void test_sheet_favorite_stays_in_sheet(void)
{
    static const KhomeAction actions[] = { KHOME_ACT_SHEET_FAVORITE };

    expect_step("SHEET + sheet-favorite",
                khome_step(at_state(KHOME_STATE_SHEET),
                           KHOME_EV_SHEET_FAVORITE),
                KHOME_STATE_SHEET, actions, 1);
}

static void test_favorite_events_are_inert_elsewhere(void)
{
    static const KhomeState tap_states[] = {
        KHOME_STATE_BROWSER,      KHOME_STATE_SHEET,
        KHOME_STATE_EDIT,         KHOME_STATE_KOREADER,
        KHOME_STATE_SETTINGS,     KHOME_STATE_SETTINGS_CONFIRM
    };
    static const KhomeState sheet_states[] = {
        KHOME_STATE_HOME,         KHOME_STATE_BROWSER,
        KHOME_STATE_EDIT,         KHOME_STATE_KOREADER,
        KHOME_STATE_SETTINGS,     KHOME_STATE_SETTINGS_CONFIRM
    };
    char label[96];
    int i;

    for (i = 0; i < (int)(sizeof(tap_states) / sizeof(tap_states[0])); i++) {
        KhomeStep step =
            khome_step(at_state(tap_states[i]), KHOME_EV_TAP_FAVORITE);

        snprintf(label, sizeof(label), "%s + tap-favorite",
                 khome_state_name(tap_states[i]));
        expect_step(label, step, tap_states[i], NULL, 0);
    }
    for (i = 0; i < (int)(sizeof(sheet_states) / sizeof(sheet_states[0]));
         i++) {
        KhomeStep step =
            khome_step(at_state(sheet_states[i]), KHOME_EV_SHEET_FAVORITE);

        snprintf(label, sizeof(label), "%s + sheet-favorite",
                 khome_state_name(sheet_states[i]));
        expect_step(label, step, sheet_states[i], NULL, 0);
    }
}

#define DESIGN_W 1072
#define DESIGN_H 1448
#define MAX_TAPS 32

static const TapRect *find_tap(const TapRect *taps, int count,
                               const char *action)
{
    int i;

    for (i = 0; i < count; i++) {
        if (strcmp(taps[i].action, action) == 0) {
            return &taps[i];
        }
    }
    return NULL;
}

static void sheet_model_with_tabs(TabSheetModel *m, int count)
{
    char url[64];
    int i;

    memset(m, 0, sizeof(*m));
    m->hour = 10;
    m->minute = 42;
    m->battery_level = 91;
    for (i = 0; i < count; i++) {
        snprintf(url, sizeof(url), "https://%d.example/", i);
        (void)tabs_open(&m->tabs, url);
    }
}

static int collect_favorite_taps(const TapRect *taps, int count,
                                 const TapRect **out, int max)
{
    int n = 0;
    int i;

    for (i = 0; i < count && n < max; i++) {
        if (strncmp(taps[i].action, "fav_open_", 9) == 0) {
            out[n++] = &taps[i];
        }
    }
    return n;
}

static void expect_favorite_tiles(const char *label, const HomeModel *m,
                                  int start, int expected)
{
    TapRect taps[MAX_TAPS];
    const TapRect *tiles[KHOME_MAX_FAVORITES];
    int count = layout_home_taps(m, taps, MAX_TAPS);
    int n = collect_favorite_taps(taps, count, tiles, KHOME_MAX_FAVORITES);
    int i;

    KH_CHECK_MSG(n == expected, "%s: expected %d favorite tiles, got %d",
                 label, expected, n);
    for (i = 0; i < n && i < expected; i++) {
        KH_CHECK_MSG(tiles[i]->index == start + i,
                     "%s: tile %d index: expected %d, got %d", label, i,
                     start + i, tiles[i]->index);
    }
}

static void test_home_tiles_map_to_favorite_indices(void)
{
    HomeModel m;

    fixture_home(&m);
    expect_favorite_tiles("home with 3 favorites", &m, 0, 3);

    fixture_home_many_page(&m, 1);
    expect_favorite_tiles("home page 2 of 9 favorites", &m, 6, 3);
}

static void test_sheet_top_row_has_four_buttons(void)
{
    static const char *const actions[4] = { "sheet_back", "sheet_home",
                                            "sheet_new",
                                            "sheet_favorite" };
    TabSheetModel m;
    TapRect taps[MAX_TAPS];
    const TapRect *row[4];
    int count;
    int i;

    sheet_model_with_tabs(&m, 3);
    count = layout_tab_sheet_taps(&m, taps, MAX_TAPS);
    for (i = 0; i < 4; i++) {
        row[i] = find_tap(taps, count, actions[i]);
        KH_CHECK_MSG(row[i] != NULL, "3 tabs: missing the %s button",
                     actions[i]);
        if (row[i] == NULL) {
            return;
        }
    }
    for (i = 0; i < 4; i++) {
        KH_CHECK_MSG(row[i]->y == row[0]->y && row[i]->h == row[0]->h,
                     "%s at (%d, %d, %d, %d) must share the top row with %s "
                     "at (%d, %d, %d, %d)",
                     actions[i], row[i]->x, row[i]->y, row[i]->w,
                     row[i]->h, actions[0], row[0]->x, row[0]->y, row[0]->w,
                     row[0]->h);
        KH_CHECK_MSG(row[i]->w == row[0]->w,
                     "%s: expected the same width as %s, got %d and %d",
                     actions[i], actions[0], row[i]->w, row[0]->w);
        KH_CHECK_MSG(row[i]->x + row[i]->w <= DESIGN_W,
                     "%s: right edge %d is outside the design canvas",
                     actions[i], row[i]->x + row[i]->w);
        if (i > 0) {
            KH_CHECK_MSG(row[i]->x >= row[i - 1]->x + row[i - 1]->w,
                         "%s at x=%d overlaps %s ending at x=%d", actions[i],
                         row[i]->x, actions[i - 1],
                         row[i - 1]->x + row[i - 1]->w);
        }
    }
}

static void test_sheet_favorite_only_with_tabs(void)
{
    TabSheetModel m;
    TapRect taps[MAX_TAPS];
    int n;
    int count;

    for (n = 0; n <= KHOME_MAX_TABS; n++) {
        const TapRect *fav;

        sheet_model_with_tabs(&m, n);
        count = layout_tab_sheet_taps(&m, taps, MAX_TAPS);
        fav = find_tap(taps, count, "sheet_favorite");
        if (n == 0) {
            KH_CHECK_MSG(fav == NULL,
                         "no tabs: sheet_favorite must not be tappable");
            continue;
        }
        KH_CHECK_MSG(fav != NULL, "%d tabs: missing sheet_favorite", n);
        if (fav != NULL) {
            KH_CHECK_MSG(fav->x >= 0 && fav->y >= 0 &&
                             fav->x + fav->w <= DESIGN_W &&
                             fav->y + fav->h <= DESIGN_H,
                         "%d tabs: sheet_favorite (%d, %d, %d, %d) is "
                         "outside the design canvas",
                         n, fav->x, fav->y, fav->w, fav->h);
        }
    }
}

static Fonts g_fonts;
static int g_fonts_loaded;

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
            g_fonts_loaded = 1;
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

static int pixel_is_text(const Canvas *canvas, int x, int y, int inverted)
{
    uint8_t value = canvas->pixels[y * canvas->stride + x];

    return inverted ? value >= 128 : value < 128;
}

/* Counts the pixels in the button's interior that differ from the same text
 * drawn by dtext: a paper button with "Favorite", or an inverted button with
 * "Favorited".  Zero means the label and the inverted state both match. */
static int label_mismatches(const Canvas *canvas, const TapRect *button,
                            const char *text, int inverted)
{
    Canvas reference;
    float baseline = (float)(button->h / 2 + 12);
    int x;
    int y;
    int mismatches = 0;

    if (!canvas_init(&reference, button->w, button->h)) {
        return -1;
    }
    canvas_clear(&reference, inverted ? 0 : 255);
    text_draw(&reference, fonts_get(&g_fonts, FONT_SANS_M), 32.0f, 2.0f, 0.0f,
              baseline, text, inverted ? 255 : 0, TEXT_CENTER,
              (float)button->w);
    for (y = 4; y < button->h - 4; y++) {
        for (x = 4; x < button->w - 4; x++) {
            int want = pixel_is_text(&reference, x, y, inverted);
            int got =
                pixel_is_text(canvas, button->x + x, button->y + y, inverted);

            if (want != got) {
                mismatches++;
            }
        }
    }
    canvas_free(&reference);
    return mismatches;
}

static void test_sheet_favorite_button_shows_state(void)
{
    TabSheetModel m;
    TapRect taps[MAX_TAPS];
    const TapRect *fav;
    Canvas canvas;
    int count;
    int mismatches;

    if (!load_fonts()) {
        fonts_ready();
        return;
    }
    sheet_model_with_tabs(&m, 3);
    count = layout_tab_sheet_taps(&m, taps, MAX_TAPS);
    fav = find_tap(taps, count, "sheet_favorite");
    KH_CHECK_MSG(fav != NULL, "3 tabs: missing sheet_favorite");
    if (fav == NULL) {
        return;
    }

    m.favorite = 0;
    if (!canvas_init(&canvas, DESIGN_W, DESIGN_H)) {
        KH_CHECK_MSG(0, "cannot allocate a %dx%d canvas", DESIGN_W, DESIGN_H);
        return;
    }
    canvas_clear(&canvas, 255);
    draw_tab_sheet(&canvas, &g_fonts, NULL, &m, 1.0f);
    mismatches = label_mismatches(&canvas, fav, "Favorite", 0);
    KH_CHECK_MSG(mismatches == 0,
                 "favorite = 0: expected the paper button labelled "
                 "\"Favorite\", %d pixels differ",
                 mismatches);
    canvas_free(&canvas);

    m.favorite = 1;
    if (!canvas_init(&canvas, DESIGN_W, DESIGN_H)) {
        KH_CHECK_MSG(0, "cannot allocate a %dx%d canvas", DESIGN_W, DESIGN_H);
        return;
    }
    canvas_clear(&canvas, 255);
    draw_tab_sheet(&canvas, &g_fonts, NULL, &m, 1.0f);
    mismatches = label_mismatches(&canvas, fav, "Favorited", 1);
    KH_CHECK_MSG(mismatches == 0,
                 "favorite = 1: expected the inverted button labelled "
                 "\"Favorited\", %d pixels differ",
                 mismatches);
    canvas_free(&canvas);
}

void kh_run_favorites_tests(void)
{
    printf("\n[favorites]\n");
    kh_run_test("favorites.parse_bom_and_crlf", test_parse_bom_and_crlf);
    kh_run_test("favorites.parse_skips_malformed_lines",
                test_parse_skips_malformed_lines);
    kh_run_test("favorites.parse_keeps_first_duplicate",
                test_parse_keeps_first_duplicate);
    kh_run_test("favorites.parse_empty_title_uses_host",
                test_parse_empty_title_uses_host);
    kh_run_test("favorites.parse_stops_at_capacity",
                test_parse_stops_at_capacity);
    kh_run_test("favorites.parse_bare_url_uses_host_name",
                test_parse_bare_url_uses_host_name);
    kh_run_test("favorites.parse_skips_blank_and_whitespace_lines",
                test_parse_skips_blank_and_whitespace_lines);
    kh_run_test("favorites.parse_skips_whitespace_only_line_with_tab",
                test_parse_skips_whitespace_only_line_with_tab);
    kh_run_test("favorites.parse_skips_url_at_or_over_length_limit",
                test_parse_skips_url_at_or_over_length_limit);
    kh_run_test("favorites.parse_bare_url_with_bom_and_crlf",
                test_parse_bare_url_with_bom_and_crlf);
    kh_run_test("favorites.parse_keeps_first_bare_duplicate",
                test_parse_keeps_first_bare_duplicate);
    kh_run_test("favorites.format_round_trips_parse",
                test_format_round_trips_parse);
    kh_run_test("favorites.format_round_trips_bare_url",
                test_format_round_trips_bare_url);
    kh_run_test("favorites.find_matches_exact_url",
                test_find_matches_exact_url);
    kh_run_test("favorites.add_appends_and_rejects_duplicates",
                test_add_appends_and_rejects_duplicates);
    kh_run_test("favorites.add_fails_when_full", test_add_fails_when_full);
    kh_run_test("favorites.remove_slides_and_rejects_out_of_range",
                test_remove_slides_and_rejects_out_of_range);
    printf("\n[favorites.state]\n");
    kh_run_test("favorites.state_home_favorite_opens_browser",
                test_home_favorite_opens_browser);
    kh_run_test("favorites.state_sheet_favorite_stays_in_sheet",
                test_sheet_favorite_stays_in_sheet);
    kh_run_test("favorites.state_favorite_events_are_inert_elsewhere",
                test_favorite_events_are_inert_elsewhere);
    printf("\n[favorites.layout]\n");
    kh_run_test("favorites.layout_home_tiles_map_to_favorite_indices",
                test_home_tiles_map_to_favorite_indices);
    kh_run_test("favorites.layout_sheet_top_row_has_four_buttons",
                test_sheet_top_row_has_four_buttons);
    kh_run_test("favorites.layout_sheet_favorite_only_with_tabs",
                test_sheet_favorite_only_with_tabs);
    kh_run_test("favorites.layout_sheet_favorite_button_shows_state",
                test_sheet_favorite_button_shows_state);
}
