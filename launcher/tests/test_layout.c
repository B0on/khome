#include "suites.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "fixtures.h"
#include "layout.h"
#include "test_harness.h"

/* Tap rectangles are design units (1072 x 1448), copied from SPEC.md section 4.
 * The device conversion used by the renderer is round(design * 758 / 1072);
 * every edge is rounded independently. */

#define DESIGN_W 1072
#define DESIGN_H 1448
#define DEVICE_W 758
#define DEVICE_H 1024
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

static void expect_tap(const TapRect *taps, int count, const char *action,
                       int x, int y, int w, int h)
{
    const TapRect *tap = find_tap(taps, count, action);
    if (tap == NULL) {
        KH_CHECK_MSG(0, "missing tap action \"%s\"", action);
        return;
    }
    KH_CHECK_MSG(tap->x == x && tap->y == y && tap->w == w && tap->h == h,
                 "tap \"%s\": expected (%d, %d, %d, %d), got "
                 "(%d, %d, %d, %d)",
                 action, x, y, w, h, tap->x, tap->y, tap->w, tap->h);
}

static void expect_app_taps(const TapRect *taps, int count)
{
    expect_tap(taps, count, "app_newspaper", 120, 624, 208, 224);
    expect_tap(taps, count, "app_manga", 328, 624, 208, 224);
    expect_tap(taps, count, "app_browser", 536, 624, 208, 224);
    expect_tap(taps, count, "app_settings", 744, 624, 208, 224);
    expect_tap(taps, count, "browser_orientation", 536, 848, 208, 40);
}

static void test_clock_rect(void)
{
    int x = -1;
    int y = -1;
    int w = -1;
    int h = -1;
    layout_clock_rect(&x, &y, &w, &h);
    KH_CHECK_MSG(x == 48 && y == 192 && w == 976 && h == 336,
                 "SPEC 5 clock rect: expected (48, 192, 976, 336), got "
                 "(%d, %d, %d, %d)", x, y, w, h);
    /* Contains baseline 432 and 512, excludes the rules at y=176 and y=536. */
    KH_CHECK_MSG(y >= 180 && y + h <= 536,
                 "clock rect (y=%d, h=%d) must sit between the masthead rule "
                 "at 176 and the clock rule at 536", y, h);
}

static void test_home_taps_three(void)
{
    HomeModel m;
    TapRect taps[MAX_TAPS];
    int count;

    fixture_home(&m);
    count = layout_home_taps(&m, taps, MAX_TAPS);
    KH_CHECK_MSG(count == 9, "home with 3 favorites: expected 9 taps, got %d",
                 count);
    expect_app_taps(taps, count);
    expect_tap(taps, count, "fav_open_01", 64, 928, 288, 184);
    expect_tap(taps, count, "fav_open_02", 392, 928, 288, 184);
    expect_tap(taps, count, "fav_open_03", 720, 928, 288, 184);
    expect_tap(taps, count, "open_browser_add", 64, 1128, 944, 184);
}

static void test_home_taps_empty(void)
{
    HomeModel m;
    TapRect taps[MAX_TAPS];
    int count;

    fixture_home_empty(&m);
    count = layout_home_taps(&m, taps, MAX_TAPS);
    KH_CHECK_MSG(count == 6, "home with 0 favorites: expected 6 taps, got %d",
                 count);
    expect_app_taps(taps, count);
    expect_tap(taps, count, "open_browser_add", 64, 888, 944, 424);
}

static void test_home_taps_many_page1(void)
{
    HomeModel m;
    TapRect taps[MAX_TAPS];
    int count;

    fixture_home_many_page(&m, 0);
    count = layout_home_taps(&m, taps, MAX_TAPS);
    KH_CHECK_MSG(count == 13,
                 "home with 9 favorites, page 1: expected 13 taps, got %d",
                 count);
    expect_app_taps(taps, count);
    expect_tap(taps, count, "fav_open_01", 64, 928, 288, 184);
    expect_tap(taps, count, "fav_open_02", 392, 928, 288, 184);
    expect_tap(taps, count, "fav_open_03", 720, 928, 288, 184);
    expect_tap(taps, count, "fav_open_04", 64, 1128, 288, 184);
    expect_tap(taps, count, "fav_open_05", 392, 1128, 288, 184);
    expect_tap(taps, count, "fav_open_06", 720, 1128, 288, 184);
    expect_tap(taps, count, "page_prev", 200, 1328, 120, 120);
    expect_tap(taps, count, "page_next", 888, 1328, 120, 120);
}

static void test_home_taps_many_page2(void)
{
    HomeModel m;
    TapRect taps[MAX_TAPS];
    int count;

    fixture_home_many_page(&m, 1);
    count = layout_home_taps(&m, taps, MAX_TAPS);
    KH_CHECK_MSG(count == 10,
                 "home with 9 favorites, page 2: expected 10 taps, got %d",
                 count);
    expect_app_taps(taps, count);
    /* Page 2 of 9 favorites shows items 07-09 on the first tile row. */
    expect_tap(taps, count, "fav_open_07", 64, 928, 288, 184);
    expect_tap(taps, count, "fav_open_08", 392, 928, 288, 184);
    expect_tap(taps, count, "fav_open_09", 720, 928, 288, 184);
    expect_tap(taps, count, "page_prev", 200, 1328, 120, 120);
    expect_tap(taps, count, "page_next", 888, 1328, 120, 120);
}

static void test_settings_taps(void)
{
    TapRect taps[MAX_TAPS];
    int count = layout_settings_taps(taps, MAX_TAPS);
    int i;
    int j;

    KH_CHECK_MSG(count == 8, "settings: expected 8 taps, got %d", count);
    expect_tap(taps, count, "back_home", 48, 184, 224, 120);
    expect_tap(taps, count, "wifi_toggle", 888, 384, 120, 120);
    expect_tap(taps, count, "charge_limit_toggle", 888, 1008, 120, 120);
    expect_tap(taps, count, "brightness_down", 744, 632, 120, 120);
    expect_tap(taps, count, "brightness_up", 888, 632, 120, 120);
    expect_tap(taps, count, "news_update", 808, 880, 200, 120);
    expect_tap(taps, count, "reboot", 64, 1136, 944, 96);
    expect_tap(taps, count, "stock_ui", 64, 1232, 944, 96);
    for (i = 0; i < count; i++) {
        for (j = i + 1; j < count; j++) {
            int apart = taps[i].x + taps[i].w <= taps[j].x ||
                        taps[j].x + taps[j].w <= taps[i].x ||
                        taps[i].y + taps[i].h <= taps[j].y ||
                        taps[j].y + taps[j].h <= taps[i].y;
            KH_CHECK_MSG(apart,
                         "settings taps \"%s\" and \"%s\" overlap: "
                         "(%d, %d, %d, %d) vs (%d, %d, %d, %d)",
                         taps[i].action, taps[j].action, taps[i].x, taps[i].y,
                         taps[i].w, taps[i].h, taps[j].x, taps[j].y,
                         taps[j].w, taps[j].h);
        }
    }
}

static void check_device_taps(const char *screen, const TapRect *taps,
                              int count)
{
    const double scale = 758.0 / 1072.0;
    int x0[MAX_TAPS];
    int y0[MAX_TAPS];
    int x1[MAX_TAPS];
    int y1[MAX_TAPS];
    int i;
    int j;

    KH_CHECK_MSG(count <= MAX_TAPS, "%s: too many taps (%d)", screen, count);
    if (count > MAX_TAPS) {
        return;
    }
    for (i = 0; i < count; i++) {
        x0[i] = (int)lround((double)taps[i].x * scale);
        y0[i] = (int)lround((double)taps[i].y * scale);
        x1[i] = (int)lround((double)(taps[i].x + taps[i].w) * scale);
        y1[i] = (int)lround((double)(taps[i].y + taps[i].h) * scale);
        KH_CHECK_MSG(x0[i] >= 0 && y0[i] >= 0 && x1[i] <= DEVICE_W &&
                     y1[i] <= DEVICE_H && x0[i] < x1[i] && y0[i] < y1[i],
                     "%s tap \"%s\": device rect (%d, %d, %d, %d) is outside "
                     "%dx%d", screen, taps[i].action, x0[i], y0[i],
                     x1[i] - x0[i], y1[i] - y0[i], DEVICE_W, DEVICE_H);
    }
    for (i = 0; i < count; i++) {
        for (j = 0; j < i; j++) {
            int overlap = !(x1[i] <= x0[j] || x1[j] <= x0[i] ||
                            y1[i] <= y0[j] || y1[j] <= y0[i]);
            KH_CHECK_MSG(!overlap,
                         "%s: device taps \"%s\" and \"%s\" overlap: "
                         "(%d,%d)-(%d,%d) vs (%d,%d)-(%d,%d)",
                         screen, taps[i].action, taps[j].action, x0[i], y0[i],
                         x1[i], y1[i], x0[j], y0[j], x1[j], y1[j]);
        }
    }
}

static void test_device_taps_fit_and_do_not_overlap(void)
{
    HomeModel home3;
    HomeModel home0;
    HomeModel many1;
    HomeModel many2;
    TapRect taps[MAX_TAPS];
    int count;

    fixture_home(&home3);
    fixture_home_empty(&home0);
    fixture_home_many_page(&many1, 0);
    fixture_home_many_page(&many2, 1);

    count = layout_home_taps(&home3, taps, MAX_TAPS);
    check_device_taps("home", taps, count);
    count = layout_home_taps(&home0, taps, MAX_TAPS);
    check_device_taps("home-empty", taps, count);
    count = layout_home_taps(&many1, taps, MAX_TAPS);
    check_device_taps("home-many page1", taps, count);
    count = layout_home_taps(&many2, taps, MAX_TAPS);
    check_device_taps("home-many page2", taps, count);

    count = layout_settings_taps(taps, MAX_TAPS);
    check_device_taps("settings", taps, count);

    count = layout_confirm_taps(KHOME_CONFIRM_REBOOT, taps, MAX_TAPS);
    check_device_taps("confirm-reboot", taps, count);
    count = layout_confirm_taps(KHOME_CONFIRM_STOCK, taps, MAX_TAPS);
    check_device_taps("confirm-stock", taps, count);
}

#define SHEET_TAP_CAP 64

static void sheet_model_with_tabs(TabSheetModel *m, int count)
{
    int i;

    memset(m, 0, sizeof(*m));
    m->battery_level = -1;
    m->tabs.count = count;
    m->tabs.active = count > 0 ? 0 : -1;
    for (i = 0; i < count; i++) {
        snprintf(m->tabs.tabs[i].url, sizeof(m->tabs.tabs[i].url),
                 "https://example.com/%d", i);
    }
}

/* Mirrors main.c's on_sheet_tap: the first rectangle that contains the point
 * wins, so a control listed after sheet_body can never be reached. */
static void expect_sheet_hit(const TapRect *taps, int count, int x, int y,
                             const char *action, int index)
{
    int i;

    for (i = 0; i < count; i++) {
        const TapRect *r = &taps[i];
        if (x < r->x || x >= r->x + r->w || y < r->y || y >= r->y + r->h) {
            continue;
        }
        KH_CHECK_MSG(strcmp(r->action, action) == 0,
                     "sheet tap (%d, %d): expected %s, got %s", x, y, action,
                     r->action);
        if (strcmp(r->action, action) == 0 && index >= 0) {
            KH_CHECK_MSG(r->index == index,
                         "sheet tap (%d, %d) %s: expected index %d, got %d", x,
                         y, action, index, r->index);
        }
        return;
    }
    KH_CHECK_MSG(0, "sheet tap (%d, %d): expected %s, hit no rectangle", x, y,
                 action);
}

static void expect_sheet_miss(const TapRect *taps, int count, int x, int y)
{
    int i;

    for (i = 0; i < count; i++) {
        const TapRect *r = &taps[i];
        if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h) {
            KH_CHECK_MSG(0, "sheet tap (%d, %d): expected no rectangle, hit %s",
                         x, y, r->action);
            return;
        }
    }
}

static void test_sheet_taps_keep_body_behind_controls(void)
{
    TabSheetModel m;
    TapRect taps[SHEET_TAP_CAP];
    int count;
    int height;

    fixture_tab_sheet_3(&m);
    height = layout_tab_sheet_height(&m);
    count = layout_tab_sheet_taps(&m, taps, SHEET_TAP_CAP);

    expect_sheet_hit(taps, count, 500, 200, "sheet_edit_url", -1);
    expect_sheet_hit(taps, count, 248, 100, "sheet_body", -1);
    expect_sheet_hit(taps, count, 440, 100, "sheet_body", -1);
    expect_sheet_hit(taps, count, 100, 100, "sheet_back", -1);
    expect_sheet_hit(taps, count, 500, 300, "sheet_tab", 0);
    expect_sheet_hit(taps, count, 970, 300, "sheet_close", 0);
    expect_sheet_hit(taps, count, 500, height - 16, "sheet_dismiss", -1);
    expect_sheet_miss(taps, count, 500, height);
    expect_sheet_miss(taps, count, 500, height + 100);
}

static void test_sheet_taps_cover_wide_sheet_margins(void)
{
    TabSheetModel m;
    TapRect taps[SHEET_TAP_CAP];
    int count;
    int height;

    fixture_tab_sheet_8(&m);
    height = layout_tab_sheet_height(&m);
    count = layout_tab_sheet_taps(&m, taps, SHEET_TAP_CAP);

    expect_sheet_hit(taps, count, 20, 700, "sheet_body", -1);
    expect_sheet_hit(taps, count, 1040, 700, "sheet_body", -1);
    expect_sheet_hit(taps, count, 500, 200, "sheet_edit_url", -1);
    expect_sheet_hit(taps, count, 500, height - 16, "sheet_dismiss", -1);
    expect_sheet_miss(taps, count, 500, height);
}

static void test_sheet_taps_empty_sheet(void)
{
    TabSheetModel m;
    TapRect taps[SHEET_TAP_CAP];
    int count;
    int height;

    sheet_model_with_tabs(&m, 0);
    height = layout_tab_sheet_height(&m);
    count = layout_tab_sheet_taps(&m, taps, SHEET_TAP_CAP);

    expect_sheet_hit(taps, count, 500, 200, "sheet_edit_url", -1);
    expect_sheet_hit(taps, count, 500, height - 16, "sheet_dismiss", -1);
    expect_sheet_miss(taps, count, 500, height);
}

static void test_sheet_taps_fit_caller_buffer(void)
{
    TabSheetModel m;
    TapRect taps[SHEET_TAP_CAP];
    TapRect capped[24];
    int n;

    for (n = 0; n <= KHOME_MAX_TABS; n++) {
        int full;
        int got;

        sheet_model_with_tabs(&m, n);
        full = layout_tab_sheet_taps(&m, taps, SHEET_TAP_CAP);
        KH_CHECK_MSG(full <= 24,
                     "sheet with %d tabs: %d rectangles exceed main.c's "
                     "rects[24]", n, full);
        got = layout_tab_sheet_taps(&m, capped, 24);
        KH_CHECK_MSG(got == full,
                     "sheet with %d tabs: 24 slots hold %d of %d rectangles", n,
                     got, full);
    }
}

static void test_sheet_edit_url_box_is_tappable(void)
{
    TabSheetModel m;
    TapRect taps[SHEET_TAP_CAP];
    int count;

    fixture_tab_sheet_3(&m);
    count = layout_tab_sheet_taps(&m, taps, SHEET_TAP_CAP);

    expect_sheet_hit(taps, count, 536, 216, "sheet_edit_url", -1);
    expect_sheet_hit(taps, count, 64, 184, "sheet_edit_url", -1);
    expect_sheet_hit(taps, count, 1007, 239, "sheet_edit_url", -1);
    expect_sheet_hit(taps, count, 1008, 216, "sheet_body", -1);
}

static void test_sheet_edit_url_heading_is_tappable(void)
{
    TabSheetModel m;
    TapRect taps[SHEET_TAP_CAP];
    int count;

    fixture_tab_sheet_3(&m);
    count = layout_tab_sheet_taps(&m, taps, SHEET_TAP_CAP);

    /* "Current tab" is drawn at baseline 184 with 24 px glyphs, so its
     * face sits around y=163..185 on the design canvas. */
    expect_sheet_hit(taps, count, 200, 176, "sheet_edit_url", -1);
}

static void test_sheet_edit_url_precedes_body(void)
{
    TabSheetModel m;
    TapRect taps[SHEET_TAP_CAP];
    const TapRect *edit;
    const TapRect *body;
    int count;
    int n;

    for (n = 0; n <= KHOME_MAX_TABS; n++) {
        sheet_model_with_tabs(&m, n);
        count = layout_tab_sheet_taps(&m, taps, SHEET_TAP_CAP);
        edit = find_tap(taps, count, "sheet_edit_url");
        body = find_tap(taps, count, "sheet_body");
        KH_CHECK_MSG(edit != NULL && body != NULL,
                     "sheet with %d tabs: edit %s, body %s", n,
                     edit != NULL ? "present" : "missing",
                     body != NULL ? "present" : "missing");
        if (edit == NULL || body == NULL) {
            continue;
        }
        KH_CHECK_MSG(edit < body,
                     "sheet with %d tabs: sheet_edit_url must be stacked "
                     "before sheet_body, edit at %d and body at %d", n,
                     (int)(edit - taps), (int)(body - taps));
    }
}

#define EDIT_TAP_CAP 64

static void expect_edit_hit(const TapRect *taps, int count, int x, int y,
                            const char *action)
{
    int i;

    for (i = 0; i < count; i++) {
        const TapRect *r = &taps[i];
        if (x < r->x || x >= r->x + r->w || y < r->y || y >= r->y + r->h) {
            continue;
        }
        KH_CHECK_MSG(strcmp(r->action, action) == 0,
                     "edit tap (%d, %d): expected %s, got %s", x, y, action,
                     r->action);
        return;
    }
    KH_CHECK_MSG(0, "edit tap (%d, %d): expected %s, hit no rectangle", x, y,
                 action);
}

static void expect_edit_miss(const TapRect *taps, int count, int x, int y)
{
    int i;

    for (i = 0; i < count; i++) {
        const TapRect *r = &taps[i];
        if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h) {
            KH_CHECK_MSG(0, "edit tap (%d, %d): expected no rectangle, hit %s",
                         x, y, r->action);
            return;
        }
    }
}

static void test_url_edit_taps_cover_controls(void)
{
    UrlEdit edit;
    TapRect taps[EDIT_TAP_CAP];
    int count;

    urledit_init(&edit, "https://example.com");
    count = layout_url_edit_taps(&edit, taps, EDIT_TAP_CAP);

    KH_CHECK_MSG(count == 50, "letters: expected 50 taps, got %d", count);
    expect_edit_hit(taps, count, 156, 64, "urledit_cancel");
    expect_edit_hit(taps, count, 916, 64, "urledit_open");
    expect_edit_hit(taps, count, 184, 476, "urledit_clear");
    expect_edit_hit(taps, count, 536, 300, "urledit_field");
    expect_edit_miss(taps, count, 536, 700);
}

static void test_url_edit_key_centers_match_their_labels(void)
{
    UrlEdit edit;
    TapRect taps[EDIT_TAP_CAP];
    int count;

    urledit_init(&edit, "https://example.com");
    count = layout_url_edit_taps(&edit, taps, EDIT_TAP_CAP);

    expect_edit_hit(taps, count, 484, 946, "key:5");
    expect_edit_hit(taps, count, 68, 1048, "key:q");
    expect_edit_hit(taps, count, 1004, 1048, "key:p");
    expect_edit_hit(taps, count, 68, 1150, "key:a");
    expect_edit_hit(taps, count, 75, 1252, "key:shift");
    expect_edit_hit(taps, count, 995, 1252, "key:bs");
    expect_edit_hit(taps, count, 240, 1354, "key:/");
    expect_edit_hit(taps, count, 684, 1354, "key:left");
    expect_edit_hit(taps, count, 832, 1354, "key:right");
    expect_edit_hit(taps, count, 980, 1354, "key:.com");
    expect_edit_hit(taps, count, 92, 1354, "key:sym");
}

static void test_url_edit_symbols_swap_the_letter_keys(void)
{
    UrlEdit edit;
    TapRect taps[EDIT_TAP_CAP];
    int count;

    urledit_init(&edit, "https://example.com");
    edit.symbols = 1;
    count = layout_url_edit_taps(&edit, taps, EDIT_TAP_CAP);

    KH_CHECK_MSG(count == 51, "symbols: expected 51 taps, got %d", count);
    expect_edit_hit(taps, count, 68, 1048, "key:_");
    expect_edit_hit(taps, count, 68, 1150, "key:;");
    expect_edit_hit(taps, count, 75, 1252, "key:shift");
    expect_edit_hit(taps, count, 995, 1252, "key:bs");
    expect_edit_hit(taps, count, 92, 1354, "key:sym");
    expect_edit_hit(taps, count, 980, 1354, "key:.com");
    KH_CHECK_MSG(find_tap(taps, count, "key:q") == NULL,
                 "symbols: the q key should be gone");
    KH_CHECK_MSG(find_tap(taps, count, "key:a") == NULL,
                 "symbols: the a key should be gone");
}

static void test_url_edit_key_rects_stay_inside_and_apart(void)
{
    UrlEdit edit;
    TapRect taps[EDIT_TAP_CAP];
    const TapRect *keys[EDIT_TAP_CAP];
    int kx;
    int ky;
    int kw;
    int kh;
    int mode;

    layout_url_edit_keyboard_rect(&kx, &ky, &kw, &kh);
    for (mode = 0; mode < 2; mode++) {
        int count;
        int nkeys = 0;
        int i;
        int j;

        urledit_init(&edit, "https://example.com");
        edit.symbols = mode;
        count = layout_url_edit_taps(&edit, taps, EDIT_TAP_CAP);
        for (i = 0; i < count; i++) {
            if (strncmp(taps[i].action, "key:", 4) == 0) {
                keys[nkeys++] = &taps[i];
            }
        }
        KH_CHECK_MSG(nkeys == (mode ? 47 : 46),
                     "mode %d: expected %d key rectangles, got %d", mode,
                     mode ? 47 : 46, nkeys);
        for (i = 0; i < nkeys; i++) {
            KH_CHECK_MSG(keys[i]->x >= kx && keys[i]->y >= ky &&
                         keys[i]->x + keys[i]->w <= kx + kw &&
                         keys[i]->y + keys[i]->h <= ky + kh,
                         "mode %d: key %s at (%d, %d, %d, %d) is outside the "
                         "keyboard (%d, %d, %d, %d)", mode, keys[i]->action,
                         keys[i]->x, keys[i]->y, keys[i]->w, keys[i]->h, kx,
                         ky, kw, kh);
            for (j = 0; j < i; j++) {
                int apart = keys[i]->x >= keys[j]->x + keys[j]->w ||
                            keys[j]->x >= keys[i]->x + keys[i]->w ||
                            keys[i]->y >= keys[j]->y + keys[j]->h ||
                            keys[j]->y >= keys[i]->y + keys[i]->h;
                KH_CHECK_MSG(apart,
                             "mode %d: key %s and %s overlap: (%d,%d,%d,%d) "
                             "vs (%d,%d,%d,%d)", mode, keys[j]->action,
                             keys[i]->action, keys[j]->x, keys[j]->y,
                             keys[j]->w, keys[j]->h, keys[i]->x, keys[i]->y,
                             keys[i]->w, keys[i]->h);
            }
        }
    }
}

static void test_url_edit_taps_fit_caller_buffer(void)
{
    UrlEdit edit;
    TapRect taps[EDIT_TAP_CAP];
    TapRect capped[64];
    int mode;

    for (mode = 0; mode < 2; mode++) {
        int full;
        int got;

        urledit_init(&edit, "https://example.com");
        edit.symbols = mode;
        full = layout_url_edit_taps(&edit, taps, EDIT_TAP_CAP);
        KH_CHECK_MSG(full <= 64,
                     "mode %d: %d rectangles exceed main.c's rects[64]", mode,
                     full);
        got = layout_url_edit_taps(&edit, capped, 64);
        KH_CHECK_MSG(got == full,
                     "mode %d: 64 slots hold %d of %d rectangles", mode, got,
                     full);
    }
}

static Fonts g_fonts;
static Icons g_icons;
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

static void test_url_edit_cursor_at(void)
{
    UrlEdit edit;
    char long_url[601];
    int fx;
    int fy;
    int fw;
    int fh;
    size_t top;
    size_t above;
    size_t bottom;

    if (!load_fonts()) {
        fonts_ready();
        return;
    }
    layout_url_edit_field_rect(&fx, &fy, &fw, &fh);

    urledit_init(&edit, "");
    KH_CHECK_MSG(layout_url_edit_cursor_at(&g_fonts, &edit, fx + fw / 2,
                                           fy + fh / 2) == 0,
                 "empty edit: expected cursor 0");

    urledit_init(&edit, "abc");
    KH_CHECK_MSG(layout_url_edit_cursor_at(&g_fonts, &edit, fx - 10,
                                           fy + 10) == 0,
                 "tap left of the text: expected cursor 0");
    KH_CHECK_MSG(layout_url_edit_cursor_at(&g_fonts, &edit, fx + fw - 1,
                                           fy + 10) == 3,
                 "tap right of the text: expected cursor 3");
    KH_CHECK_MSG(layout_url_edit_cursor_at(&g_fonts, &edit, fx + fw / 2,
                                           fy + fh + 100) == 3,
                 "tap below a single line: expected cursor 3");
    {
        const Font *font = fonts_get(&g_fonts, FONT_SANS_M);
        float char_w = text_width(font, 28.0f, 1.0f, "a");
        int text_x = fx + 16;
        int mid = text_x + (int)(char_w * 0.5f);

        KH_CHECK_MSG(layout_url_edit_cursor_at(&g_fonts, &edit, mid - 1,
                                               fy + 10) == 0,
                     "tap in the left half of the first glyph: expected "
                     "cursor 0");
        KH_CHECK_MSG(layout_url_edit_cursor_at(&g_fonts, &edit, mid + 1,
                                               fy + 10) == 1,
                     "tap in the right half of the first glyph: expected "
                     "cursor 1");
    }

    memset(long_url, 'a', sizeof(long_url) - 1);
    long_url[sizeof(long_url) - 1] = 0;
    urledit_init(&edit, long_url);
    top = layout_url_edit_cursor_at(&g_fonts, &edit, fx + fw - 1, fy + 10);
    above = layout_url_edit_cursor_at(&g_fonts, &edit, fx + fw - 1, 0);
    bottom = layout_url_edit_cursor_at(&g_fonts, &edit, fx + fw - 1,
                                       fy + fh - 20);
    KH_CHECK_MSG(top > 0 && top < edit.len,
                 "long edit: the top visible line should not be the first "
                 "line, got cursor %d of %d", (int)top, (int)edit.len);
    KH_CHECK_MSG(above == top,
                 "long edit: a tap above the field should map to the top "
                 "visible line, got %d and %d", (int)above, (int)top);
    KH_CHECK_MSG(bottom == edit.len,
                 "long edit: a tap past the last line should map to the end, "
                 "got %d of %d", (int)bottom, (int)edit.len);
}

/* The URL field draws four text lines of 56 design px, the first baseline at
 * field y + 56, and its text starts 16 px inside the field. */
#define EDIT_FIRST_TEXT_Y_OFFSET 16
#define EDIT_LINE_STEP 56

static int render_edit_field(Canvas *canvas, UrlEdit *edit)
{
    if (!canvas_init(canvas, DESIGN_W, DESIGN_H)) {
        KH_CHECK_MSG(0, "cannot allocate a %dx%d canvas", DESIGN_W, DESIGN_H);
        return 0;
    }
    canvas_clear(canvas, 255);
    draw_url_edit_field(canvas, &g_fonts, edit, 1.0f);
    return 1;
}

static int is_ink(const Canvas *canvas, int x, int y)
{
    return canvas->pixels[y * canvas->stride + x] < 128;
}

static int count_ink(const Canvas *canvas, int x0, int y0, int x1, int y1)
{
    int x;
    int y;
    int n = 0;

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            if (is_ink(canvas, x, y)) {
                n++;
            }
        }
    }
    return n;
}

/* A cursor at a wrapped line's start offset belongs to that line, so the bar
 * is drawn at the text margin of the next row, not at the end of the row
 * above.  A tap on the next row's left edge returns that same offset. */
static void test_url_edit_wrapped_line_starts_on_the_next_row(void)
{
    UrlEdit edit;
    Canvas canvas;
    char url[601];
    int fx;
    int fy;
    int fw;
    int fh;
    int text_x;
    int row0_y;
    int row1_y;
    int band_y;
    size_t row0_start;
    size_t row1_start;
    int bar_x;
    int bar_w = 0;

    if (!load_fonts()) {
        fonts_ready();
        return;
    }
    layout_url_edit_field_rect(&fx, &fy, &fw, &fh);
    text_x = fx + EDIT_FIRST_TEXT_Y_OFFSET;
    row0_y = fy + EDIT_FIRST_TEXT_Y_OFFSET + 10;
    row1_y = row0_y + EDIT_LINE_STEP;
    /* Below the second row's baseline, where only the cursor bar reaches. */
    band_y = fy + 56 + EDIT_LINE_STEP + 4;

    memset(url, 'a', sizeof(url) - 1);
    url[sizeof(url) - 1] = 0;
    urledit_init(&edit, url);
    urledit_set_cursor(&edit, 0);
    if (!render_edit_field(&canvas, &edit)) {
        return;
    }
    KH_CHECK_MSG(edit.first_line == 0,
                 "cursor at 0: expected the first page, got %d",
                 edit.first_line);

    row0_start = layout_url_edit_cursor_at(&g_fonts, &edit, fx - 10, row0_y);
    row1_start = layout_url_edit_cursor_at(&g_fonts, &edit, fx - 10, row1_y);
    KH_CHECK_MSG(row0_start == 0,
                 "tap on the first row's left edge: expected offset 0, got %d",
                 (int)row0_start);
    KH_CHECK_MSG(row1_start > 0 && row1_start < edit.len,
                 "tap on the second row's left edge: expected a wrapped offset "
                 "inside the text, got %d of %d", (int)row1_start,
                 (int)edit.len);

    urledit_set_cursor(&edit, row1_start);
    KH_CHECK_MSG(edit.cursor == row1_start,
                 "the row start must be a character boundary, expected %d, "
                 "got %d", (int)row1_start, (int)edit.cursor);
    canvas_clear(&canvas, 255);
    draw_url_edit_field(&canvas, &g_fonts, &edit, 1.0f);

    for (bar_x = text_x; bar_x < text_x + 12; bar_x++) {
        if (is_ink(&canvas, bar_x, band_y)) {
            bar_w++;
        } else {
            break;
        }
    }
    KH_CHECK_MSG(bar_w == 3,
                 "cursor at the second row's start: expected a 3 px bar at "
                 "x=%d, found %d px there", text_x, bar_w);
    if (bar_w == 3) {
        KH_CHECK_MSG(!is_ink(&canvas, text_x + 3, band_y) &&
                         !is_ink(&canvas, text_x + 4, band_y),
                     "cursor at the second row's start: the bar must end at "
                     "x=%d, but ink continues", text_x + 3);
    }
    canvas_free(&canvas);
}

/* Tapping a line that is already on screen must not move the page.  Only a
 * cursor that leaves the four visible rows scrolls, and then by one row. */
static void test_url_edit_keeps_its_page_and_scrolls_by_one_line(void)
{
    UrlEdit edit;
    Canvas canvas;
    char url[601];
    int fx;
    int fy;
    int fw;
    int fh;
    int row0_y;
    int row3_y;
    int last_page;
    int page;
    size_t row0;
    size_t row1;
    size_t row3;
    size_t row3_end;
    size_t s0;
    size_t s3;
    size_t e3;
    size_t top_after;

    if (!load_fonts()) {
        fonts_ready();
        return;
    }
    layout_url_edit_field_rect(&fx, &fy, &fw, &fh);
    row0_y = fy + EDIT_FIRST_TEXT_Y_OFFSET + 10;
    row3_y = row0_y + 3 * EDIT_LINE_STEP;

    memset(url, 'a', sizeof(url) - 1);
    url[sizeof(url) - 1] = 0;
    urledit_init(&edit, url);
    if (!render_edit_field(&canvas, &edit)) {
        return;
    }
    last_page = edit.first_line;
    KH_CHECK_MSG(last_page > 0,
                 "a long URL with the cursor at the end must scroll, got the "
                 "first line %d", last_page);

    row0 = layout_url_edit_cursor_at(&g_fonts, &edit, fx - 10, row0_y);
    row1 = layout_url_edit_cursor_at(&g_fonts, &edit, fx - 10,
                                     row0_y + EDIT_LINE_STEP);
    row3 = layout_url_edit_cursor_at(&g_fonts, &edit, fx - 10, row3_y);
    row3_end = layout_url_edit_cursor_at(&g_fonts, &edit, fx + fw - 1, row3_y);
    KH_CHECK_MSG(row0 > 0 && row1 > row0 && row3 > row1,
                 "expected four visible wrapped lines, got offsets %d, %d, %d",
                 (int)row0, (int)row1, (int)row3);
    KH_CHECK_MSG(row3_end == edit.len,
                 "the page holding the cursor must end with the text, got "
                 "offset %d of %d", (int)row3_end, (int)edit.len);

    urledit_set_cursor(&edit, row0);
    canvas_clear(&canvas, 255);
    draw_url_edit_field(&canvas, &g_fonts, &edit, 1.0f);
    KH_CHECK_MSG(edit.first_line == last_page,
                 "cursor on the top visible line's start: expected the page to "
                 "stay at %d, got %d", last_page, edit.first_line);

    urledit_set_cursor(&edit, row3 + 1);
    canvas_clear(&canvas, 255);
    draw_url_edit_field(&canvas, &g_fonts, &edit, 1.0f);
    KH_CHECK_MSG(edit.first_line == last_page,
                 "cursor inside the last visible line: expected the page to "
                 "stay at %d, got %d", last_page, edit.first_line);

    /* One line above the top is the smallest move that must scroll. */
    urledit_set_cursor(&edit, row0 - 1);
    canvas_clear(&canvas, 255);
    draw_url_edit_field(&canvas, &g_fonts, &edit, 1.0f);
    page = last_page - 1;
    KH_CHECK_MSG(edit.first_line == page,
                 "cursor one line above the page: expected a one-line scroll "
                 "to %d, got %d", page, edit.first_line);

    /* The page now has a line below its bottom row, so the cursor can leave
     * it in the other direction. */
    s0 = layout_url_edit_cursor_at(&g_fonts, &edit, fx - 10, row0_y);
    s3 = layout_url_edit_cursor_at(&g_fonts, &edit, fx - 10, row3_y);
    e3 = layout_url_edit_cursor_at(&g_fonts, &edit, fx + fw - 1, row3_y);
    KH_CHECK_MSG(s0 < row0,
                 "after the scroll the top row must start at a new line, got "
                 "%d against the old %d", (int)s0, (int)row0);
    KH_CHECK_MSG(e3 > s3 && e3 < edit.len,
                 "the bottom row must end before the text does, got %d of %d",
                 (int)e3, (int)edit.len);

    urledit_set_cursor(&edit, s0);
    canvas_clear(&canvas, 255);
    draw_url_edit_field(&canvas, &g_fonts, &edit, 1.0f);
    KH_CHECK_MSG(edit.first_line == page,
                 "cursor on the scrolled page's top line start: expected the "
                 "page to stay at %d, got %d", page, edit.first_line);

    urledit_set_cursor(&edit, s3 + 1);
    canvas_clear(&canvas, 255);
    draw_url_edit_field(&canvas, &g_fonts, &edit, 1.0f);
    KH_CHECK_MSG(edit.first_line == page,
                 "cursor inside the bottom row: expected the page to stay at "
                 "%d, got %d", page, edit.first_line);

    urledit_set_cursor(&edit, e3);
    canvas_clear(&canvas, 255);
    draw_url_edit_field(&canvas, &g_fonts, &edit, 1.0f);
    KH_CHECK_MSG(edit.first_line == page + 1,
                 "cursor one line past the bottom row: expected a one-line "
                 "scroll to %d, got %d", page + 1, edit.first_line);

    top_after = layout_url_edit_cursor_at(&g_fonts, &edit, fx - 10, row0_y);
    KH_CHECK_MSG(top_after == row0,
                 "after scrolling back down the top row must start at %d, got "
                 "%d", (int)row0, (int)top_after);

    urledit_set_cursor(&edit, top_after);
    canvas_clear(&canvas, 255);
    draw_url_edit_field(&canvas, &g_fonts, &edit, 1.0f);
    KH_CHECK_MSG(edit.first_line == page + 1,
                 "cursor on the new page's top line start: expected the page "
                 "to stay at %d, got %d", page + 1, edit.first_line);

    urledit_set_cursor(&edit, s0);
    canvas_clear(&canvas, 255);
    draw_url_edit_field(&canvas, &g_fonts, &edit, 1.0f);
    KH_CHECK_MSG(edit.first_line == page,
                 "cursor one line above the page: expected a one-line scroll "
                 "back to %d, got %d", page, edit.first_line);
    canvas_free(&canvas);
}

/* Every non-ASCII character is shown as one '?', so a buffer full of "é"
 * must render exactly like the same buffer full of "?", and no line may end
 * inside the two bytes of one character. */
static void test_url_edit_draws_one_question_mark_per_non_ascii_character(void)
{
    UrlEdit wide;
    UrlEdit ascii;
    Canvas wide_canvas;
    Canvas ascii_canvas;
    char wide_text[512];
    char ascii_text[512];
    int fx;
    int fy;
    int fw;
    int fh;
    int row;
    size_t n = 0;
    int i;

    if (!load_fonts()) {
        fonts_ready();
        return;
    }
    layout_url_edit_field_rect(&fx, &fy, &fw, &fh);
    for (i = 0; i < 80; i++) {
        wide_text[n] = 'a';
        wide_text[n + 1] = (char)0xC3;
        wide_text[n + 2] = (char)0xA9;
        n += 3;
    }
    wide_text[n] = 0;
    n = 0;
    for (i = 0; i < 80; i++) {
        ascii_text[n++] = 'a';
        ascii_text[n++] = '?';
    }
    ascii_text[n] = 0;

    urledit_init(&wide, wide_text);
    urledit_init(&ascii, ascii_text);
    urledit_set_cursor(&wide, 0);
    urledit_set_cursor(&ascii, 0);
    if (!render_edit_field(&wide_canvas, &wide)) {
        return;
    }
    if (!render_edit_field(&ascii_canvas, &ascii)) {
        canvas_free(&wide_canvas);
        return;
    }
    KH_CHECK_MSG(count_ink(&wide_canvas, fx, fy, fx + fw, fy + 4 * EDIT_LINE_STEP) > 200,
                 "the field must actually draw the URL text");
    KH_CHECK_MSG(memcmp(wide_canvas.pixels, ascii_canvas.pixels,
                        (size_t)DESIGN_W * DESIGN_H) == 0,
                 "a multi-byte character must look exactly like one '?'");

    n = layout_url_edit_cursor_at(&g_fonts, &wide, fx - 10,
                                  fy + EDIT_FIRST_TEXT_Y_OFFSET + 10 +
                                      EDIT_LINE_STEP);
    KH_CHECK_MSG(n > 0 && n < wide.len,
                 "the sample must wrap onto a second line, got offset %d of %d",
                 (int)n, (int)wide.len);
    for (row = 0; row < 4; row++) {
        size_t start = layout_url_edit_cursor_at(
            &g_fonts, &wide, fx - 10,
            fy + EDIT_FIRST_TEXT_Y_OFFSET + 10 + row * EDIT_LINE_STEP);
        size_t end = layout_url_edit_cursor_at(
            &g_fonts, &wide, fx + fw - 1,
            fy + EDIT_FIRST_TEXT_Y_OFFSET + 10 + row * EDIT_LINE_STEP);

        if (start < wide.len) {
            KH_CHECK_MSG(((unsigned char)wide.buf[start] & 0xC0) != 0x80,
                         "row %d starts inside a UTF-8 sequence at %d", row,
                         (int)start);
        }
        if (end < wide.len) {
            KH_CHECK_MSG(((unsigned char)wide.buf[end] & 0xC0) != 0x80,
                         "row %d ends inside a UTF-8 sequence at %d", row,
                         (int)end);
        }
    }
    canvas_free(&wide_canvas);
    canvas_free(&ascii_canvas);
}

/* Charge limit toggle: the on state fills the track and parks the knob on the
 * right; off (0) and unknown (-1) outline the track with the knob on the left.
 * The row is at 912..1008 x 1044..1100, with a 40 px knob on either end. */
static void test_settings_charge_limit_toggle_draws_state(void)
{
    SettingsModel m;
    Canvas on;
    Canvas off;
    Canvas unknown;
    int outside = 0;
    int x;
    int y;

    if (!load_fonts()) {
        fonts_ready();
        return;
    }
    if (!canvas_init(&on, DESIGN_W, DESIGN_H)) {
        KH_CHECK_MSG(0, "cannot allocate a %dx%d canvas", DESIGN_W, DESIGN_H);
        return;
    }
    if (!canvas_init(&off, DESIGN_W, DESIGN_H)) {
        KH_CHECK_MSG(0, "cannot allocate a %dx%d canvas", DESIGN_W, DESIGN_H);
        canvas_free(&on);
        return;
    }
    if (!canvas_init(&unknown, DESIGN_W, DESIGN_H)) {
        KH_CHECK_MSG(0, "cannot allocate a %dx%d canvas", DESIGN_W, DESIGN_H);
        canvas_free(&on);
        canvas_free(&off);
        return;
    }
    fixture_settings(&m, 1);

    m.charge_limit = 1;
    canvas_clear(&on, 255);
    draw_settings(&on, &g_fonts, &g_icons, &m, 1.0f);
    m.charge_limit = 0;
    canvas_clear(&off, 255);
    draw_settings(&off, &g_fonts, &g_icons, &m, 1.0f);
    m.charge_limit = -1;
    canvas_clear(&unknown, 255);
    draw_settings(&unknown, &g_fonts, &g_icons, &m, 1.0f);

    KH_CHECK_MSG(memcmp(off.pixels, unknown.pixels,
                        (size_t)DESIGN_W * DESIGN_H) == 0,
                 "charge_limit 0 and -1 must draw the same off toggle");

    KH_CHECK_MSG(is_ink(&on, 918, 1072) && is_ink(&on, 1002, 1072),
                 "charge_limit 1: expected a filled track, got paper at "
                 "(%d, 1072) and (%d, 1072)", 918, 1002);
    KH_CHECK_MSG(!is_ink(&on, 980, 1072),
                 "charge_limit 1: expected the knob on the right at (980, "
                 "1072)");
    KH_CHECK_MSG(!is_ink(&off, 918, 1072) && !is_ink(&off, 1002, 1072),
                 "charge_limit 0: expected an outlined track with paper at "
                 "(%d, 1072) and (%d, 1072)", 918, 1002);
    KH_CHECK_MSG(is_ink(&off, 940, 1072),
                 "charge_limit 0: expected the knob on the left at (940, "
                 "1072)");
    KH_CHECK_MSG(!is_ink(&unknown, 918, 1072) && !is_ink(&unknown, 1002, 1072),
                 "charge_limit -1: expected an outlined track with paper at "
                 "(%d, 1072) and (%d, 1072)", 918, 1002);
    KH_CHECK_MSG(is_ink(&unknown, 940, 1072),
                 "charge_limit -1: expected the knob on the left at (940, "
                 "1072)");

    for (y = 0; y < DESIGN_H; y++) {
        for (x = 0; x < DESIGN_W; x++) {
            size_t i = (size_t)y * (size_t)DESIGN_W + (size_t)x;

            if (x >= 908 && x <= 1012 && y >= 1040 && y <= 1104) {
                continue;
            }
            if (on.pixels[i] != off.pixels[i]) {
                outside++;
            }
        }
    }
    KH_CHECK_MSG(outside == 0,
                 "charge_limit 1: %d pixels changed outside the toggle",
                 outside);

    canvas_free(&on);
    canvas_free(&off);
    canvas_free(&unknown);
}

static void test_confirm_taps_match_drawn_buttons(void)
{
    static const char *const reboot_actions[3] = { "confirm_reboot_kindle",
                                                   "confirm_reboot_home",
                                                   "confirm_cancel" };
    static const char *const stock_actions[2] = { "confirm_stock_back",
                                                  "confirm_cancel" };
    static const KhomeConfirm dialogs[] = { KHOME_CONFIRM_REBOOT,
                                            KHOME_CONFIRM_STOCK };
    char label[96];
    size_t d;

    if (!load_fonts()) {
        fonts_ready();
        return;
    }
    for (d = 0; d < sizeof(dialogs) / sizeof(dialogs[0]); d++) {
        const char *const *actions =
            dialogs[d] == KHOME_CONFIRM_REBOOT ? reboot_actions : stock_actions;
        int expected = dialogs[d] == KHOME_CONFIRM_REBOOT ? 3 : 2;
        TapRect taps[MAX_TAPS];
        Canvas canvas;
        int count;
        int i;

        snprintf(label, sizeof(label), "confirm %d", (int)dialogs[d]);
        count = layout_confirm_taps(dialogs[d], taps, MAX_TAPS);
        KH_CHECK_MSG(count == expected, "%s: expected %d taps, got %d", label,
                     expected, count);
        if (count != expected) {
            continue;
        }
        for (i = 0; i < count; i++) {
            KH_CHECK_MSG(strcmp(taps[i].action, actions[i]) == 0,
                         "%s: tap %d: expected %s, got %s", label, i,
                         actions[i], taps[i].action);
            KH_CHECK_MSG(taps[i].x >= 0 && taps[i].y >= 0 &&
                             taps[i].x + taps[i].w <= DESIGN_W &&
                             taps[i].y + taps[i].h <= DESIGN_H,
                         "%s: tap %s at (%d, %d, %d, %d) is outside the "
                         "design canvas", label, taps[i].action, taps[i].x,
                         taps[i].y, taps[i].w, taps[i].h);
            if (i > 0) {
                KH_CHECK_MSG(taps[i].y >= taps[i - 1].y + taps[i - 1].h,
                             "%s: tap %s at y=%d overlaps the button above at "
                             "y=%d h=%d", label, taps[i].action, taps[i].y,
                             taps[i - 1].y, taps[i - 1].h);
            }
        }

        if (!canvas_init(&canvas, DESIGN_W, DESIGN_H)) {
            KH_CHECK_MSG(0, "%s: cannot allocate a %dx%d canvas", label,
                         DESIGN_W, DESIGN_H);
            continue;
        }
        canvas_clear(&canvas, 255);
        draw_confirm(&canvas, &g_fonts, dialogs[d], 1.0f);
        for (i = 0; i < count; i++) {
            const TapRect *r = &taps[i];
            int x;
            int y;
            int missing = 0;

            /* draw_confirm outlines each button on the tap rectangle, so every
             * edge pixel must be ink. */
            for (x = r->x; x < r->x + r->w; x++) {
                if (!is_ink(&canvas, x, r->y) ||
                    !is_ink(&canvas, x, r->y + r->h - 1)) {
                    missing++;
                }
            }
            for (y = r->y; y < r->y + r->h; y++) {
                if (!is_ink(&canvas, r->x, y) ||
                    !is_ink(&canvas, r->x + r->w - 1, y)) {
                    missing++;
                }
            }
            KH_CHECK_MSG(missing == 0,
                         "%s: tap %s (%d, %d, %d, %d) misses the drawn button "
                         "border at %d edge pixels", label, r->action, r->x,
                         r->y, r->w, r->h, missing);
            KH_CHECK_MSG(count_ink(&canvas, r->x + 3, r->y + 3,
                                   r->x + r->w - 3, r->y + r->h - 3) > 0,
                         "%s: tap %s covers a button with no label inside it",
                         label, r->action);
        }
        canvas_free(&canvas);
    }
}

static void test_confirm_taps_without_a_dialog(void)
{
    TapRect taps[MAX_TAPS];
    TapRect capped[4];

    KH_CHECK_MSG(layout_confirm_taps(KHOME_CONFIRM_NONE, taps, MAX_TAPS) == 0,
                 "confirm NONE: expected no taps");
    KH_CHECK_MSG(layout_confirm_taps(KHOME_CONFIRM_REBOOT, capped, 2) == 2,
                 "confirm REBOOT with 2 slots: expected 2 taps, got %d",
                 layout_confirm_taps(KHOME_CONFIRM_REBOOT, capped, 2));
    KH_CHECK_MSG(layout_confirm_taps(KHOME_CONFIRM_REBOOT, taps, MAX_TAPS) <= 4,
                 "confirm REBOOT: more than main.c's rects[4] taps");
    KH_CHECK_MSG(layout_confirm_taps(KHOME_CONFIRM_NONE, NULL, 0) == 0,
                 "confirm taps: a null buffer must return zero");
}

void kh_run_layout_tests(void)
{
    printf("\n[layout]\n");
    kh_run_test("layout.clock_rect", test_clock_rect);
    kh_run_test("layout.home_taps_three", test_home_taps_three);
    kh_run_test("layout.home_taps_empty", test_home_taps_empty);
    kh_run_test("layout.home_taps_many_page1", test_home_taps_many_page1);
    kh_run_test("layout.home_taps_many_page2", test_home_taps_many_page2);
    kh_run_test("layout.settings_taps", test_settings_taps);
    kh_run_test("layout.device_taps_fit_and_do_not_overlap",
                test_device_taps_fit_and_do_not_overlap);
    kh_run_test("layout.sheet_taps_keep_body_behind_controls",
                test_sheet_taps_keep_body_behind_controls);
    kh_run_test("layout.sheet_taps_cover_wide_sheet_margins",
                test_sheet_taps_cover_wide_sheet_margins);
    kh_run_test("layout.sheet_taps_empty_sheet", test_sheet_taps_empty_sheet);
    kh_run_test("layout.sheet_taps_fit_caller_buffer",
                test_sheet_taps_fit_caller_buffer);
    kh_run_test("layout.sheet_edit_url_box_is_tappable",
                test_sheet_edit_url_box_is_tappable);
    kh_run_test("layout.sheet_edit_url_heading_is_tappable",
                test_sheet_edit_url_heading_is_tappable);
    kh_run_test("layout.sheet_edit_url_precedes_body",
                test_sheet_edit_url_precedes_body);
    kh_run_test("layout.url_edit_taps_cover_controls",
                test_url_edit_taps_cover_controls);
    kh_run_test("layout.url_edit_key_centers_match_their_labels",
                test_url_edit_key_centers_match_their_labels);
    kh_run_test("layout.url_edit_symbols_swap_the_letter_keys",
                test_url_edit_symbols_swap_the_letter_keys);
    kh_run_test("layout.url_edit_key_rects_stay_inside_and_apart",
                test_url_edit_key_rects_stay_inside_and_apart);
    kh_run_test("layout.url_edit_taps_fit_caller_buffer",
                test_url_edit_taps_fit_caller_buffer);
    kh_run_test("layout.url_edit_cursor_at", test_url_edit_cursor_at);
    kh_run_test("layout.url_edit_wrapped_line_starts_on_the_next_row",
                test_url_edit_wrapped_line_starts_on_the_next_row);
    kh_run_test("layout.url_edit_keeps_its_page_and_scrolls_by_one_line",
                test_url_edit_keeps_its_page_and_scrolls_by_one_line);
    kh_run_test("layout.url_edit_draws_one_question_mark_per_non_ascii",
                test_url_edit_draws_one_question_mark_per_non_ascii_character);
    kh_run_test("layout.settings_charge_limit_toggle_draws_state",
                test_settings_charge_limit_toggle_draws_state);
    kh_run_test("layout.confirm_taps_match_drawn_buttons",
                test_confirm_taps_match_drawn_buttons);
    kh_run_test("layout.confirm_taps_without_a_dialog",
                test_confirm_taps_without_a_dialog);
}
