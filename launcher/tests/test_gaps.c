#include "suites.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "canvas.h"
#include "fixtures.h"
#include "layout.h"
#include "state.h"
#include "tabs.h"
#include "test_harness.h"
#include "text.h"

/* Behaviour of the home, tab sheet, settings and image-scaling features that
 * arrived after the first release.  Tap dispatch mirrors main.c: the first
 * rectangle that contains the design point wins. */

#define DESIGN_W 1072
#define DESIGN_H 1448
#define MAX_TAPS 32
#define PAPER 255
#define INK 0
#define GRAY1 85

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

static const TapRect *tap_at(const TapRect *taps, int count, int x, int y)
{
    int i;

    for (i = 0; i < count; i++) {
        if (x >= taps[i].x && x < taps[i].x + taps[i].w &&
            y >= taps[i].y && y < taps[i].y + taps[i].h) {
            return &taps[i];
        }
    }
    return NULL;
}

static void expect_tap_action(const char *label, const TapRect *taps,
                              int count, int x, int y, const char *expected)
{
    const TapRect *tap = tap_at(taps, count, x, y);

    KH_CHECK_MSG(tap != NULL && strcmp(tap->action, expected) == 0,
                 "%s: tap at (%d, %d): expected \"%s\", got \"%s\"", label, x,
                 y, expected, tap != NULL ? tap->action : "no rectangle");
}

static KhomeMachine at_state(KhomeState state)
{
    KhomeMachine m;

    m.state = state;
    m.asleep = 0;
    m.confirm = KHOME_CONFIRM_NONE;
    return m;
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

static void home_with_favorites(HomeModel *m, int count)
{
    if (count == 0) {
        fixture_home_empty(m);
        return;
    }
    fixture_home_many(m);
    m->favorite_count = count;
}

/* The empty home is one target: the plus, the copy under it and the add row
 * all open the browser's add flow. */
static void test_home_empty_block_opens_add(void)
{
    HomeModel m;
    TapRect taps[MAX_TAPS];
    const TapRect *block;
    int count;

    fixture_home_empty(&m);
    count = layout_home_taps(&m, taps, MAX_TAPS);
    block = find_tap(taps, count, "open_browser_add");
    KH_CHECK_MSG(block != NULL,
                 "empty home: missing the open_browser_add block");
    if (block == NULL) {
        return;
    }
    KH_CHECK_MSG(block->x >= 56 && block->x <= 72,
                 "empty block x: expected about 64, got %d", block->x);
    KH_CHECK_MSG(block->y >= 880 && block->y <= 896,
                 "empty block y: expected about 888, got %d", block->y);
    KH_CHECK_MSG(block->w >= 936 && block->w <= 952,
                 "empty block width: expected about 944, got %d", block->w);
    KH_CHECK_MSG(block->h >= 416 && block->h <= 432,
                 "empty block height: expected about 424, got %d", block->h);
    KH_CHECK_MSG(block->x <= 488 && block->x + block->w >= 584 &&
                     block->y <= 928 && block->y + block->h >= 1024,
                 "empty block (%d, %d, %d, %d) must contain the plus frame "
                 "(488, 928, 96, 96)",
                 block->x, block->y, block->w, block->h);

    expect_tap_action("empty home plus centre", taps, count, 536, 976,
                      "open_browser_add");
    expect_tap_action("empty home copy", taps, count, 536, 1072,
                      "open_browser_add");
    expect_tap_action("empty home add row", taps, count, 536, 1220,
                      "open_browser_add");
}

/* Six favorites fit one page; seven need the pager. */
static void test_home_page_taps_start_at_seven(void)
{
    HomeModel m;
    TapRect taps[MAX_TAPS];
    int count;
    int n;

    for (n = 0; n <= 6; n++) {
        home_with_favorites(&m, n);
        count = layout_home_taps(&m, taps, MAX_TAPS);
        KH_CHECK_MSG(find_tap(taps, count, "page_prev") == NULL,
                     "%d favorites: page_prev must not exist", n);
        KH_CHECK_MSG(find_tap(taps, count, "page_next") == NULL,
                     "%d favorites: page_next must not exist", n);
    }
    for (n = 7; n <= 9; n++) {
        const TapRect *prev;
        const TapRect *next;
        char label[64];

        home_with_favorites(&m, n);
        count = layout_home_taps(&m, taps, MAX_TAPS);
        prev = find_tap(taps, count, "page_prev");
        next = find_tap(taps, count, "page_next");
        KH_CHECK_MSG(prev != NULL, "%d favorites: missing page_prev", n);
        KH_CHECK_MSG(next != NULL, "%d favorites: missing page_next", n);
        if (prev != NULL) {
            snprintf(label, sizeof(label), "%d favorites page-prev", n);
            expect_tap_action(label, taps, count, prev->x + prev->w / 2,
                              prev->y + prev->h / 2, "page_prev");
        }
        if (next != NULL) {
            snprintf(label, sizeof(label), "%d favorites page-next", n);
            expect_tap_action(label, taps, count, next->x + next->w / 2,
                              next->y + next->h / 2, "page_next");
        }
    }
}

static void test_home_page_events(void)
{
    static const KhomeAction prev[] = { KHOME_ACT_PAGE_PREV };
    static const KhomeAction next[] = { KHOME_ACT_PAGE_NEXT };

    expect_step("HOME + page-prev",
                khome_step(at_state(KHOME_STATE_HOME), KHOME_EV_PAGE_PREV),
                KHOME_STATE_HOME, prev, 1);
    expect_step("HOME + page-next",
                khome_step(at_state(KHOME_STATE_HOME), KHOME_EV_PAGE_NEXT),
                KHOME_STATE_HOME, next, 1);
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

static int new_canvas(Canvas *canvas)
{
    if (!canvas_init(canvas, DESIGN_W, DESIGN_H)) {
        KH_CHECK_MSG(0, "cannot allocate a %dx%d canvas", DESIGN_W, DESIGN_H);
        return 0;
    }
    canvas_clear(canvas, PAPER);
    return 1;
}

/* Draws a line the way layout.c's dtext does: centred or right-aligned in a
 * design box, with the trailing letter-spacing counted in the line width. */
static void draw_text_line(Canvas *canvas, FontRole role, float size, float ls,
                           float x, float baseline, float boxw,
                           const char *text, uint8_t color, TextAlign align)
{
    const Font *font = fonts_get(&g_fonts, role);
    float width;
    float start;

    if (font == NULL) {
        return;
    }
    width = text_width(font, size, ls, text);
    if (text[0] != 0) {
        width += ls;
    }
    if (align == TEXT_LEFT) {
        start = x;
    } else if (align == TEXT_CENTER) {
        start = x + (boxw - width) * 0.5f;
    } else {
        start = x + boxw - width;
    }
    text_draw(canvas, font, size, ls, start, baseline, text, color, TEXT_LEFT,
              0.0f);
}

static int region_diff(const Canvas *a, const Canvas *b, int x0, int y0,
                       int x1, int y1)
{
    int x;
    int y;
    int n = 0;

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            if (a->pixels[(size_t)y * (size_t)a->stride + x] !=
                b->pixels[(size_t)y * (size_t)b->stride + x]) {
                n++;
            }
        }
    }
    return n;
}

/* The footer count is a user-visible string with a singular form. */
static void test_home_footer_count_wording(void)
{
    static const struct {
        int count;
        const char *text;
    } cases[] = {
        { 0, "0 favorites" },
        { 1, "1 favorite" },
        { 2, "2 favorites" },
        { 6, "6 favorites" }
    };
    size_t i;

    if (!load_fonts()) {
        fonts_ready();
        return;
    }
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        HomeModel m;
        Canvas actual;
        Canvas expected;
        int diff;

        home_with_favorites(&m, cases[i].count);
        if (!new_canvas(&actual)) {
            return;
        }
        if (!new_canvas(&expected)) {
            canvas_free(&actual);
            return;
        }
        draw_home(&actual, &g_fonts, &g_icons, &m, 1.0f);
        draw_text_line(&expected, FONT_SANS_M, 24, 2, 808, 1400, 200,
                       cases[i].text, GRAY1, TEXT_RIGHT);
        diff = region_diff(&actual, &expected, 780, 1370, 1010, 1412);
        KH_CHECK_MSG(diff == 0,
                     "home with %d favorites: footer must read \"%s\"; "
                     "%d pixels differ",
                     cases[i].count, cases[i].text, diff);
        canvas_free(&actual);
        canvas_free(&expected);
    }
}

/* The failed-launch notice sits on the footer's own line, replacing whatever
 * the footer would otherwise show for the current favorite count.  Everything
 * above the footer rule is unchanged. */
#define FOOTER_BAND_TOP 1360
#define FOOTER_BAND_BOTTOM 1416

static void check_home_notice(const char *label, const HomeModel *base,
                              const char *line)
{
    HomeModel m = *base;
    Canvas plain;
    Canvas noticed;
    Canvas expected;
    int diff;

    if (!new_canvas(&plain)) {
        return;
    }
    if (!new_canvas(&noticed)) {
        canvas_free(&plain);
        return;
    }
    if (!new_canvas(&expected)) {
        canvas_free(&plain);
        canvas_free(&noticed);
        return;
    }

    m.notice[0] = 0;
    draw_home(&plain, &g_fonts, &g_icons, &m, 1.0f);
    snprintf(m.notice, sizeof(m.notice), "%s", line);
    draw_home(&noticed, &g_fonts, &g_icons, &m, 1.0f);
    draw_text_line(&expected, FONT_SANS_M, 24, 2, 64, 1400, 944, line, INK,
                   TEXT_RIGHT);

    diff = region_diff(&noticed, &expected, 200, FOOTER_BAND_TOP, DESIGN_W,
                       FOOTER_BAND_BOTTOM);
    KH_CHECK_MSG(diff == 0,
                 "%s notice: the footer must draw \"%s\"; %d pixels differ",
                 label, line, diff);
    diff = region_diff(&plain, &expected, 200, FOOTER_BAND_TOP, DESIGN_W,
                       FOOTER_BAND_BOTTOM);
    KH_CHECK_MSG(diff > 0,
                 "%s without a notice: the footer must not draw \"%s\"", label,
                 line);
    diff = region_diff(&plain, &noticed, 0, FOOTER_BAND_TOP, DESIGN_W,
                       FOOTER_BAND_BOTTOM);
    KH_CHECK_MSG(diff > 0,
                 "%s notice: the footer count or pager must be replaced",
                 label);
    diff = region_diff(&plain, &noticed, 0, 0, DESIGN_W, FOOTER_BAND_TOP) +
           region_diff(&plain, &noticed, 0, FOOTER_BAND_BOTTOM, DESIGN_W,
                       DESIGN_H);
    KH_CHECK_MSG(diff == 0,
                 "%s notice: %d pixels changed outside the footer", label,
                 diff);

    canvas_free(&plain);
    canvas_free(&noticed);
    canvas_free(&expected);
}

static void test_home_notice_replaces_count(void)
{
    HomeModel m;

    if (!load_fonts()) {
        fonts_ready();
        return;
    }
    fixture_home(&m);
    check_home_notice("home with 3 favorites", &m, "Browser launch cancelled");
}

static void test_home_many_notice_replaces_pager(void)
{
    HomeModel m;

    if (!load_fonts()) {
        fonts_ready();
        return;
    }
    fixture_home_many_page(&m, 0);
    check_home_notice("home with 9 favorites", &m, "Browser did not start");
}

static void test_home_empty_notice_replaces_zero_count(void)
{
    HomeModel m;

    if (!load_fonts()) {
        fonts_ready();
        return;
    }
    fixture_home_empty(&m);
    check_home_notice("empty home", &m, "Browser launch cancelled");
}

static void test_sheet_top_row_five_buttons(void)
{
    static const char *const actions[5] = {
        "sheet_back", "sheet_home", "sheet_new", "sheet_favorite",
        "sheet_reload"
    };
    TabSheetModel m;
    TapRect taps[MAX_TAPS];
    const TapRect *row[5];
    int count;
    int i;
    int j;

    fixture_tab_sheet_3(&m);
    count = layout_tab_sheet_taps(&m, taps, MAX_TAPS);
    for (i = 0; i < 5; i++) {
        row[i] = find_tap(taps, count, actions[i]);
        KH_CHECK_MSG(row[i] != NULL, "3 tabs: missing the %s button",
                     actions[i]);
    }
    for (i = 0; i < 5; i++) {
        if (row[i] == NULL) {
            continue;
        }
        KH_CHECK_MSG(row[i]->h >= 120,
                     "%s height: expected at least 120 design px, got %d",
                     actions[i], row[i]->h);
        KH_CHECK_MSG(row[i]->y >= 0 && row[i]->y + row[i]->h <= DESIGN_H &&
                         row[i]->x >= 0 &&
                         row[i]->x + row[i]->w <= DESIGN_W,
                     "%s (%d, %d, %d, %d) is outside the design canvas",
                     actions[i], row[i]->x, row[i]->y, row[i]->w, row[i]->h);
        KH_CHECK_MSG(row[i]->y < 200,
                     "%s y=%d: expected the top row", actions[i],
                     row[i]->y);
    }
    for (i = 0; i < 5; i++) {
        for (j = i + 1; j < 5; j++) {
            int apart;

            if (row[i] == NULL || row[j] == NULL) {
                continue;
            }
            apart = row[i]->x + row[i]->w <= row[j]->x ||
                    row[j]->x + row[j]->w <= row[i]->x ||
                    row[i]->y + row[i]->h <= row[j]->y ||
                    row[j]->y + row[j]->h <= row[i]->y;
            KH_CHECK_MSG(apart, "%s and %s overlap: (%d, %d, %d, %d) vs "
                         "(%d, %d, %d, %d)",
                         actions[i], actions[j], row[i]->x, row[i]->y,
                         row[i]->w, row[i]->h, row[j]->x, row[j]->y,
                         row[j]->w, row[j]->h);
        }
    }
}

static void check_close_rects(const char *label, const TabSheetModel *m)
{
    TapRect taps[MAX_TAPS];
    int count = layout_tab_sheet_taps(m, taps, MAX_TAPS);
    int closes = 0;
    int i;

    for (i = 0; i < count; i++) {
        if (strcmp(taps[i].action, "sheet_close") != 0) {
            continue;
        }
        closes++;
        KH_CHECK_MSG(taps[i].w >= 96,
                     "%s: sheet_close for tab %d is %d design px wide, "
                     "expected at least 96", label, taps[i].index, taps[i].w);
        KH_CHECK_MSG(taps[i].index >= 0 && taps[i].index < m->tabs.count,
                     "%s: sheet_close index %d outside 0..%d", label,
                     taps[i].index, m->tabs.count - 1);
    }
    KH_CHECK_MSG(closes == m->tabs.count,
                 "%s: expected %d close rectangles, got %d", label,
                 m->tabs.count, closes);
}

static void test_sheet_tab_close_rects(void)
{
    TabSheetModel m;

    fixture_tab_sheet_3(&m);
    check_close_rects("3 tabs", &m);
    fixture_tab_sheet_8(&m);
    check_close_rects("8 tabs", &m);
}

static void test_sheet_actions_without_tabs(void)
{
    TabSheetModel m;
    TapRect taps[MAX_TAPS];
    int count;

    memset(&m, 0, sizeof(m));
    tabs_init(&m.tabs);
    m.hour = 10;
    m.minute = 42;
    m.battery_level = 91;
    count = layout_tab_sheet_taps(&m, taps, MAX_TAPS);
    KH_CHECK_MSG(find_tap(taps, count, "sheet_favorite") == NULL,
                 "no tabs: Favorite must not be tappable");
    KH_CHECK_MSG(find_tap(taps, count, "sheet_reload") == NULL,
                 "no tabs: Reload must not be tappable");
    KH_CHECK_MSG(find_tap(taps, count, "sheet_tab") == NULL,
                 "no tabs: there must be no tab row");
    KH_CHECK_MSG(find_tap(taps, count, "sheet_close") == NULL,
                 "no tabs: there must be no close rectangle");
    KH_CHECK_MSG(find_tap(taps, count, "sheet_back") != NULL,
                 "no tabs: Back must stay tappable");
    KH_CHECK_MSG(find_tap(taps, count, "sheet_new") != NULL,
                 "no tabs: New tab must stay tappable");
}

/* The new-tab versus current-tab mode is KhomeEditMode, which main.c holds in
 * a static (main.c:112) outside this harness; khome_step has no field for it,
 * so both transitions look identical here. */
static void test_sheet_new_opens_editor(void)
{
    static const KhomeAction draw_edit[] = { KHOME_ACT_DRAW_EDIT };

    expect_step("SHEET + sheet-new",
                khome_step(at_state(KHOME_STATE_SHEET), KHOME_EV_SHEET_NEW),
                KHOME_STATE_EDIT, draw_edit, 1);
    expect_step("SHEET + sheet-edit",
                khome_step(at_state(KHOME_STATE_SHEET), KHOME_EV_SHEET_EDIT),
                KHOME_STATE_EDIT, draw_edit, 1);
}

static void test_sheet_clock_and_reload(void)
{
    static const KhomeAction status[] = { KHOME_ACT_DRAW_SHEET_STATUS };
    static const KhomeAction reload[] = { KHOME_ACT_SHEET_RELOAD,
                                          KHOME_ACT_UNGRAB,
                                          KHOME_ACT_HIDE_SHEET };

    expect_step("SHEET + minute",
                khome_step(at_state(KHOME_STATE_SHEET), KHOME_EV_MINUTE),
                KHOME_STATE_SHEET, status, 1);
    expect_step("SHEET + status-changed",
                khome_step(at_state(KHOME_STATE_SHEET),
                           KHOME_EV_STATUS_CHANGED),
                KHOME_STATE_SHEET, status, 1);
    expect_step("SHEET + sheet-reload",
                khome_step(at_state(KHOME_STATE_SHEET), KHOME_EV_SHEET_RELOAD),
                KHOME_STATE_BROWSER, reload, 3);
}

static void test_sheet_notice_draws_one_line(void)
{
    static const char *const line = "Saved to favorites";
    TabSheetModel m;
    Canvas plain;
    Canvas noticed;
    Canvas expected;
    int diff;

    if (!load_fonts()) {
        fonts_ready();
        return;
    }
    fixture_tab_sheet_3(&m);
    m.notice[0] = 0;
    if (!new_canvas(&plain)) {
        return;
    }
    if (!new_canvas(&noticed)) {
        canvas_free(&plain);
        return;
    }
    if (!new_canvas(&expected)) {
        canvas_free(&plain);
        canvas_free(&noticed);
        return;
    }
    draw_tab_sheet(&plain, &g_fonts, &g_icons, &m, 1.0f);
    snprintf(m.notice, sizeof(m.notice), "%s", line);
    draw_tab_sheet(&noticed, &g_fonts, &g_icons, &m, 1.0f);
    draw_text_line(&expected, FONT_SANS_M, 24, 2, 64, 204, 0, line, INK,
                   TEXT_LEFT);

    diff = region_diff(&noticed, &expected, 64, 176, 520, 211);
    KH_CHECK_MSG(diff == 0,
                 "sheet notice: the heading must draw \"%s\"; %d pixels "
                 "differ", line, diff);
    diff = region_diff(&plain, &expected, 64, 176, 520, 211);
    KH_CHECK_MSG(diff > 0,
                 "empty sheet notice: the heading slot must not draw \"%s\"",
                 line);
    diff = region_diff(&plain, &noticed, 0, 0, DESIGN_W, 176) +
           region_diff(&plain, &noticed, 0, 211, DESIGN_W, DESIGN_H);
    KH_CHECK_MSG(diff == 0,
                 "sheet notice: %d pixels changed outside the one heading "
                 "line", diff);

    canvas_free(&plain);
    canvas_free(&noticed);
    canvas_free(&expected);
}

static void test_settings_update_event(void)
{
    static const KhomeAction fetch[] = { KHOME_ACT_NEWS_FETCH };

    expect_step("SETTINGS + settings-update",
                khome_step(at_state(KHOME_STATE_SETTINGS),
                           KHOME_EV_SETTINGS_UPDATE),
                KHOME_STATE_SETTINGS, fetch, 1);
}

static void test_settings_updating_label(void)
{
    static const struct {
        int updating;
        const char *label;
    } cases[] = {
        { 1, "Updating…" },
        { 0, "Update now" }
    };
    size_t i;

    if (!load_fonts()) {
        fonts_ready();
        return;
    }
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        SettingsModel m;
        Canvas actual;
        Canvas expected;
        int diff;

        if (cases[i].updating) {
            fixture_settings_updating(&m);
        } else {
            fixture_settings(&m, 1);
        }
        if (!new_canvas(&actual)) {
            return;
        }
        if (!new_canvas(&expected)) {
            canvas_free(&actual);
            return;
        }
        draw_settings(&actual, &g_fonts, &g_icons, &m, 1.0f);
        draw_text_line(&expected, FONT_SANS_M, 28, 2, 808, 952, 200,
                       cases[i].label, INK, TEXT_CENTER);
        diff = region_diff(&actual, &expected, 815, 922, 1001, 962);
        KH_CHECK_MSG(diff == 0,
                     "settings updating=%d: the update button must read "
                     "\"%s\"; %d pixels differ",
                     cases[i].updating, cases[i].label, diff);
        canvas_free(&actual);
        canvas_free(&expected);
    }
}

static Bitmap make_bitmap(int w, int h, uint8_t value)
{
    Bitmap b;
    size_t n = (size_t)w * (size_t)h;

    b.w = w;
    b.h = h;
    b.pixels = (uint8_t *)malloc(n);
    if (b.pixels != NULL) {
        memset(b.pixels, value, n);
    }
    return b;
}

static void test_blit_scaled_uniform(void)
{
    Bitmap src = make_bitmap(200, 300, 123);
    Canvas canvas;
    int bad_x = -1;
    int bad_y = -1;
    int bad_value = -1;
    int x;
    int y;

    KH_CHECK_MSG(src.pixels != NULL, "allocate a 200x300 source bitmap");
    if (src.pixels == NULL) {
        return;
    }
    if (!canvas_init(&canvas, 64, 80)) {
        KH_CHECK_MSG(0, "cannot allocate the destination canvas");
        free(src.pixels);
        return;
    }
    canvas_clear(&canvas, 0);
    canvas_blit_scaled(&canvas, 0, 0, 64, 80, &src);
    for (y = 0; y < canvas.h && bad_x < 0; y++) {
        for (x = 0; x < canvas.w; x++) {
            uint8_t got = canvas.pixels[(size_t)y * (size_t)canvas.stride + x];
            if (got != 123) {
                bad_x = x;
                bad_y = y;
                bad_value = got;
                break;
            }
        }
    }
    KH_CHECK_MSG(bad_x < 0,
                 "uniform 123 gray: destination (%d, %d) expected 123, got %d",
                 bad_x, bad_y, bad_value);
    canvas_free(&canvas);
    free(src.pixels);
}

static void test_blit_scaled_averages_the_boundary(void)
{
    Bitmap src = make_bitmap(100, 10, 255);
    Canvas canvas;
    int bad_x = -1;
    int bad_y = -1;
    int bad_value = -1;
    int x;
    int y;

    KH_CHECK_MSG(src.pixels != NULL, "allocate a 100x10 source bitmap");
    if (src.pixels == NULL) {
        return;
    }
    for (y = 0; y < src.h; y++) {
        for (x = 0; x < 50; x++) {
            src.pixels[(size_t)y * (size_t)src.w + x] = 0;
        }
    }
    if (!canvas_init(&canvas, 33, 10)) {
        KH_CHECK_MSG(0, "cannot allocate the destination canvas");
        free(src.pixels);
        return;
    }
    canvas_clear(&canvas, 200);
    canvas_blit_scaled(&canvas, 0, 0, 33, 10, &src);
    for (y = 0; y < canvas.h && bad_x < 0; y++) {
        for (x = 0; x < canvas.w; x++) {
            uint8_t got = canvas.pixels[(size_t)y * (size_t)canvas.stride + x];
            int ok;

            if (x < 16) {
                ok = got == 0;
            } else if (x > 16) {
                ok = got == 255;
            } else {
                ok = got > 0 && got < 255;
            }
            if (!ok) {
                bad_x = x;
                bad_y = y;
                bad_value = got;
                break;
            }
        }
    }
    KH_CHECK_MSG(bad_x < 0,
                 "black left / white right: (%d, %d) expected %s, got %d",
                 bad_x, bad_y,
                 bad_x < 0 ? "the area average" : (bad_x < 16 ? "black" :
                 bad_x > 16 ? "white" : "an intermediate value"),
                 bad_value);
    canvas_free(&canvas);
    free(src.pixels);
}

static void test_blit_scaled_full_page_size(void)
{
    Bitmap src = make_bitmap(1072, 1448, 77);
    Canvas canvas;
    int bad_x = -1;
    int bad_y = -1;
    int bad_value = -1;
    int x;
    int y;

    KH_CHECK_MSG(src.pixels != NULL, "allocate a 1072x1448 source bitmap");
    if (src.pixels == NULL) {
        return;
    }
    if (!canvas_init(&canvas, 758, 1024)) {
        KH_CHECK_MSG(0, "cannot allocate the destination canvas");
        free(src.pixels);
        return;
    }
    canvas_clear(&canvas, 0);
    canvas_blit_scaled(&canvas, 0, 0, 758, 1024, &src);
    KH_CHECK_MSG(canvas.w == 758 && canvas.h == 1024,
                 "1072x1448 -> 758x1024: canvas is %dx%d", canvas.w,
                 canvas.h);
    for (y = 0; y < canvas.h && bad_x < 0; y++) {
        for (x = 0; x < canvas.w; x++) {
            uint8_t got = canvas.pixels[(size_t)y * (size_t)canvas.stride + x];
            if (got != 77) {
                bad_x = x;
                bad_y = y;
                bad_value = got;
                break;
            }
        }
    }
    KH_CHECK_MSG(bad_x < 0,
                 "full page 77 gray: destination (%d, %d) expected 77, got %d",
                 bad_x, bad_y, bad_value);
    canvas_free(&canvas);
    free(src.pixels);
}

/* Checks that pixels inside the destination box are ink and every other pixel
 * keeps the cleared value. */
static void check_blit_clip(const char *label, Canvas *canvas, int x0, int y0,
                            int x1, int y1, uint8_t inside, uint8_t outside)
{
    int bad_x = -1;
    int bad_y = -1;
    int bad_value = -1;
    int bad_expected = -1;
    int x;
    int y;

    for (y = 0; y < canvas->h && bad_x < 0; y++) {
        for (x = 0; x < canvas->w; x++) {
            uint8_t got =
                canvas->pixels[(size_t)y * (size_t)canvas->stride + x];
            uint8_t expected =
                (x >= x0 && x < x1 && y >= y0 && y < y1) ? inside : outside;
            if (got != expected) {
                bad_x = x;
                bad_y = y;
                bad_value = got;
                bad_expected = expected;
                break;
            }
        }
    }
    KH_CHECK_MSG(bad_x < 0,
                 "%s: (%d, %d) expected %d, got %d", label, bad_x, bad_y,
                 bad_expected, bad_value);
}

static void test_blit_scaled_never_writes_outside(void)
{
    Bitmap src = make_bitmap(10, 10, 0);
    Canvas canvas;

    KH_CHECK_MSG(src.pixels != NULL, "allocate a 10x10 source bitmap");
    if (src.pixels == NULL) {
        return;
    }
    if (!canvas_init(&canvas, 80, 60)) {
        KH_CHECK_MSG(0, "cannot allocate the destination canvas");
        free(src.pixels);
        return;
    }

    canvas_clear(&canvas, 255);
    canvas_blit_scaled(&canvas, 10, 10, 50, 40, &src);
    check_blit_clip("box inside the canvas", &canvas, 10, 10, 60, 50, 0, 255);

    canvas_clear(&canvas, 255);
    canvas_blit_scaled(&canvas, -10, -5, 50, 40, &src);
    check_blit_clip("negative origin", &canvas, 0, 0, 40, 35, 0, 255);

    canvas_clear(&canvas, 255);
    canvas_blit_scaled(&canvas, 70, 50, 50, 40, &src);
    check_blit_clip("past the right and bottom edges", &canvas, 70, 50, 80,
                    60, 0, 255);

    canvas_clear(&canvas, 255);
    canvas_blit_scaled(&canvas, 0, 0, 0, 10, &src);
    canvas_blit_scaled(&canvas, 0, 0, 10, 0, &src);
    canvas_blit_scaled(&canvas, 0, 0, -5, 10, &src);
    check_blit_clip("degenerate sizes", &canvas, 0, 0, 0, 0, 0, 255);

    canvas_free(&canvas);
    free(src.pixels);
}

void kh_run_gaps_tests(void)
{
    printf("\n[gaps]\n");
    kh_run_test("gaps.home_empty_block_opens_add",
                test_home_empty_block_opens_add);
    kh_run_test("gaps.home_page_taps_start_at_seven",
                test_home_page_taps_start_at_seven);
    kh_run_test("gaps.home_page_events", test_home_page_events);
    kh_run_test("gaps.home_footer_count_wording",
                test_home_footer_count_wording);
    kh_run_test("gaps.home_notice_replaces_count",
                test_home_notice_replaces_count);
    kh_run_test("gaps.home_many_notice_replaces_pager",
                test_home_many_notice_replaces_pager);
    kh_run_test("gaps.home_empty_notice_replaces_zero_count",
                test_home_empty_notice_replaces_zero_count);
    kh_run_test("gaps.sheet_top_row_five_buttons",
                test_sheet_top_row_five_buttons);
    kh_run_test("gaps.sheet_tab_close_rects", test_sheet_tab_close_rects);
    kh_run_test("gaps.sheet_actions_without_tabs",
                test_sheet_actions_without_tabs);
    kh_run_test("gaps.sheet_new_opens_editor", test_sheet_new_opens_editor);
    kh_run_test("gaps.sheet_clock_and_reload", test_sheet_clock_and_reload);
    kh_run_test("gaps.sheet_notice_draws_one_line",
                test_sheet_notice_draws_one_line);
    kh_run_test("gaps.settings_update_event", test_settings_update_event);
    kh_run_test("gaps.settings_updating_label", test_settings_updating_label);
    kh_run_test("gaps.blit_scaled_uniform", test_blit_scaled_uniform);
    kh_run_test("gaps.blit_scaled_averages_the_boundary",
                test_blit_scaled_averages_the_boundary);
    kh_run_test("gaps.blit_scaled_full_page_size",
                test_blit_scaled_full_page_size);
    kh_run_test("gaps.blit_scaled_never_writes_outside",
                test_blit_scaled_never_writes_outside);
}
