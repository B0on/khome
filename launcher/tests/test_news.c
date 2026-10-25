#include "suites.h"

#include <stdlib.h>
#include <string.h>

#include "layout.h"
#include "state.h"
#include "test_harness.h"

/* The NEWS screen: Newspaper on home enters it, the left and right edges page
 * through the fetched paper, a pull from the top opens its menu, and a tap on
 * a story block opens an article. */

#define MAX_TAPS 8

/* The menu's position line sits between the rule under the buttons and the
 * grab handle, so this band contains no other ink. */
#define MENU_LABEL_TOP 180
#define MENU_LABEL_BOTTOM 250

static KhomeMachine at_state(KhomeState state)
{
    KhomeMachine m;

    m.state = state;
    m.asleep = 0;
    m.confirm = KHOME_CONFIRM_NONE;
    return m;
}

static KhomeMachine after_step(KhomeStep step)
{
    KhomeMachine m;

    m.state = step.state;
    m.asleep = step.asleep;
    m.confirm = step.confirm;
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

static void expect_asleep(const char *label, KhomeStep got, int asleep)
{
    KH_CHECK_MSG(got.asleep == asleep, "%s: expected asleep %d, got %d", label,
                 asleep, got.asleep);
}

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

static void test_home_newspaper_opens_news(void)
{
    static const KhomeAction open[] = { KHOME_ACT_DRAW_NEWS,
                                        KHOME_ACT_NEWS_FETCH };

    expect_step("HOME + tap-newspaper",
                khome_step(at_state(KHOME_STATE_HOME),
                           KHOME_EV_TAP_NEWSPAPER),
                KHOME_STATE_NEWS, open, 2);
}

static void test_news_navigation_events(void)
{
    static const KhomeAction next[] = { KHOME_ACT_NEWS_NEXT };
    static const KhomeAction prev[] = { KHOME_ACT_NEWS_PREV };
    static const KhomeAction home[] = { KHOME_ACT_DRAW_HOME };
    static const KhomeAction fetched[] = { KHOME_ACT_DRAW_NEWS };

    expect_step("NEWS + news-next",
                khome_step(at_state(KHOME_STATE_NEWS), KHOME_EV_NEWS_NEXT),
                KHOME_STATE_NEWS, next, 1);
    expect_step("NEWS + news-prev",
                khome_step(at_state(KHOME_STATE_NEWS), KHOME_EV_NEWS_PREV),
                KHOME_STATE_NEWS, prev, 1);
    expect_step("NEWS + news-home",
                khome_step(at_state(KHOME_STATE_NEWS), KHOME_EV_NEWS_HOME),
                KHOME_STATE_HOME, home, 1);
    expect_step("NEWS + news-fetched",
                khome_step(at_state(KHOME_STATE_NEWS), KHOME_EV_NEWS_FETCHED),
                KHOME_STATE_NEWS, fetched, 1);
}

/* Swipe-derived page turns belong to NEWS alone. */
static void test_news_navigation_is_inert_outside_news(void)
{
    static const KhomeEvent events[] = { KHOME_EV_NEWS_PREV,
                                         KHOME_EV_NEWS_NEXT };
    static const KhomeState states[] = {
        KHOME_STATE_HOME,       KHOME_STATE_BROWSER,     KHOME_STATE_SHEET,
        KHOME_STATE_EDIT,       KHOME_STATE_KOREADER,    KHOME_STATE_NEWS_MENU,
        KHOME_STATE_SETTINGS,   KHOME_STATE_SETTINGS_CONFIRM,
        KHOME_STATE_HOME_CONFIRM
    };
    char label[96];
    size_t i;
    size_t j;

    for (i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        for (j = 0; j < sizeof(events) / sizeof(events[0]); j++) {
            snprintf(label, sizeof(label), "%s + %s",
                     khome_state_name(states[i]),
                     khome_event_name(events[j]));
            expect_step(label,
                        khome_step(at_state(states[i]), events[j]),
                        states[i], NULL, 0);
        }
    }
}

/* A fetch that finishes while the menu is up repaints the menu, not the
 * page underneath it. */
static void test_menu_fetched_redraws_the_menu(void)
{
    static const KhomeAction menu[] = { KHOME_ACT_DRAW_NEWS_MENU };

    expect_step("NEWS_MENU + news-fetched",
                khome_step(at_state(KHOME_STATE_NEWS_MENU),
                           KHOME_EV_NEWS_FETCHED),
                KHOME_STATE_NEWS_MENU, menu, 1);
}

/* The masthead and page number belong to the page bitmap, so a minute tick
 * must not repaint anything on NEWS. */
static void test_news_minute_does_not_redraw_the_clock(void)
{
    expect_step("NEWS + minute",
                khome_step(at_state(KHOME_STATE_NEWS), KHOME_EV_MINUTE),
                KHOME_STATE_NEWS, NULL, 0);
}

/* A pull from the top of the paper opens the menu.  NEWS already owns the
 * touchscreen and draws its own page, so the menu needs neither a grab nor a
 * sheet. */
static void test_news_pull_opens_the_menu_alone(void)
{
    static const KhomeAction menu[] = { KHOME_ACT_DRAW_NEWS_MENU };

    expect_step("NEWS + sheet-pull",
                khome_step(at_state(KHOME_STATE_NEWS), KHOME_EV_SHEET_PULL),
                KHOME_STATE_NEWS_MENU, menu, 1);
}

/* The browser keeps its own pull-down. */
static void test_browser_pull_still_opens_the_sheet(void)
{
    static const KhomeAction sheet[] = { KHOME_ACT_GRAB,
                                         KHOME_ACT_DRAW_SHEET };

    expect_step("BROWSER + sheet-pull",
                khome_step(at_state(KHOME_STATE_BROWSER),
                           KHOME_EV_SHEET_PULL),
                KHOME_STATE_SHEET, sheet, 2);
}

static void test_menu_home_goes_home(void)
{
    static const KhomeAction home[] = { KHOME_ACT_DRAW_HOME };

    expect_step("NEWS_MENU + news-menu-home",
                khome_step(at_state(KHOME_STATE_NEWS_MENU),
                           KHOME_EV_NEWS_MENU_HOME),
                KHOME_STATE_HOME, home, 1);
}

static void test_menu_front_returns_to_the_front_page(void)
{
    static const KhomeAction front[] = { KHOME_ACT_NEWS_FRONT };

    expect_step("NEWS_MENU + news-menu-front",
                khome_step(at_state(KHOME_STATE_NEWS_MENU),
                           KHOME_EV_NEWS_MENU_FRONT),
                KHOME_STATE_NEWS, front, 1);
}

static void test_menu_refresh_starts_from_the_menu(void)
{
    static const KhomeAction refresh[] = { KHOME_ACT_NEWS_MENU_REFRESH };

    expect_step("NEWS_MENU + news-menu-refresh",
                khome_step(at_state(KHOME_STATE_NEWS_MENU),
                           KHOME_EV_NEWS_MENU_REFRESH),
                KHOME_STATE_NEWS_MENU, refresh, 1);
}

static void test_menu_back_returns_to_the_paper(void)
{
    static const KhomeAction back[] = { KHOME_ACT_NEWS_BACK };

    expect_step("NEWS_MENU + news-menu-back",
                khome_step(at_state(KHOME_STATE_NEWS_MENU),
                           KHOME_EV_NEWS_MENU_BACK),
                KHOME_STATE_NEWS, back, 1);
}

static void test_menu_dismiss_returns_to_the_paper(void)
{
    static const KhomeAction back[] = { KHOME_ACT_DRAW_NEWS };

    expect_step("NEWS_MENU + sheet-dismiss",
                khome_step(at_state(KHOME_STATE_NEWS_MENU),
                           KHOME_EV_SHEET_DISMISS),
                KHOME_STATE_NEWS, back, 1);
}

/* The menu events belong to NEWS_MENU alone.  sheet-dismiss is the exception
 * for SHEET, which has its own dismiss behaviour. */
static void test_menu_events_are_inert_outside_the_menu(void)
{
    static const KhomeEvent menu_events[] = {
        KHOME_EV_NEWS_MENU_HOME, KHOME_EV_NEWS_MENU_FRONT,
        KHOME_EV_NEWS_MENU_REFRESH, KHOME_EV_NEWS_MENU_BACK
    };
    static const KhomeState states[] = { KHOME_STATE_HOME, KHOME_STATE_NEWS,
                                         KHOME_STATE_BROWSER,
                                         KHOME_STATE_SHEET,
                                         KHOME_STATE_SETTINGS };
    static const KhomeState dismiss_states[] = { KHOME_STATE_HOME,
                                                 KHOME_STATE_NEWS,
                                                 KHOME_STATE_BROWSER,
                                                 KHOME_STATE_SETTINGS };
    char label[96];
    size_t i;
    size_t j;

    for (i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        for (j = 0; j < sizeof(menu_events) / sizeof(menu_events[0]); j++) {
            snprintf(label, sizeof(label), "%s + %s",
                     khome_state_name(states[i]),
                     khome_event_name(menu_events[j]));
            expect_step(label,
                        khome_step(at_state(states[i]), menu_events[j]),
                        states[i], NULL, 0);
        }
    }
    for (i = 0; i < sizeof(dismiss_states) / sizeof(dismiss_states[0]); i++) {
        snprintf(label, sizeof(label), "%s + sheet-dismiss",
                 khome_state_name(dismiss_states[i]));
        expect_step(label,
                    khome_step(at_state(dismiss_states[i]),
                               KHOME_EV_SHEET_DISMISS),
                    dismiss_states[i], NULL, 0);
    }
}

/* Going under the saver drops the menu to NEWS, and the wake path that
 * follows repaints the paper. */
static void test_sleep_and_wake_return_to_news(void)
{
    static const KhomeAction schedule[] = { KHOME_ACT_SCHEDULE_REDRAW };
    static const KhomeAction redraw[] = { KHOME_ACT_DRAW_NEWS };
    KhomeStep step;

    step = khome_step(at_state(KHOME_STATE_NEWS_MENU),
                      KHOME_EV_POWER_GOING_TO_SS);
    expect_step("NEWS_MENU + goingToScreenSaver", step, KHOME_STATE_NEWS, NULL,
                0);
    expect_asleep("NEWS_MENU + goingToScreenSaver", step, 1);

    step = khome_step(after_step(step), KHOME_EV_POWER_OUT_OF_SS);
    expect_step("NEWS + outOfScreenSaver", step, KHOME_STATE_NEWS, schedule,
                1);
    expect_asleep("NEWS + outOfScreenSaver", step, 0);

    step = khome_step(after_step(step), KHOME_EV_DEFER_REDRAW);
    expect_step("NEWS + defer-redraw", step, KHOME_STATE_NEWS, redraw, 1);
}

/* A minute tick and a status change repaint only the menu's status strip, so
 * the page under the panel is left alone. */
static void test_menu_clock_updates_redraw_only_the_status(void)
{
    static const KhomeAction status[] = { KHOME_ACT_DRAW_NEWS_MENU_STATUS };

    expect_step("NEWS_MENU + minute",
                khome_step(at_state(KHOME_STATE_NEWS_MENU), KHOME_EV_MINUTE),
                KHOME_STATE_NEWS_MENU, status, 1);
    expect_step("NEWS_MENU + status-changed",
                khome_step(at_state(KHOME_STATE_NEWS_MENU),
                           KHOME_EV_STATUS_CHANGED),
                KHOME_STATE_NEWS_MENU, status, 1);
}

/* The invisible home corner is gone: home is a menu button now.  The left
 * 30% goes back and the rest goes forward unless a link wins the tap. */
static void test_news_tap_corners(void)
{
    TapRect taps[MAX_TAPS];
    const TapRect *prev;
    const TapRect *next;
    int count;

    count = layout_news_taps(taps, MAX_TAPS);
    KH_CHECK_MSG(count == 2, "NEWS: expected 2 tap rectangles, got %d",
                 count);
    if (count < 2) {
        return;
    }
    prev = find_tap(taps, count, "news_prev");
    next = find_tap(taps, count, "news_next");
    KH_CHECK_MSG(find_tap(taps, count, "news_home") == NULL,
                 "NEWS: news_home must not exist");
    KH_CHECK_MSG(prev != NULL, "NEWS: missing news_prev");
    KH_CHECK_MSG(next != NULL, "NEWS: missing news_next");
    if (prev == NULL || next == NULL) {
        return;
    }

    expect_tap_action("NEWS top-left corner", taps, count, 10, 10,
                      "news_prev");
    expect_tap_action("NEWS left edge below top", taps, count, 10, 1000,
                      "news_prev");
    expect_tap_action("NEWS left edge", taps, count, 150, 300, "news_prev");
    expect_tap_action("NEWS right edge", taps, count, 1000, 1000,
                      "news_next");
    expect_tap_action("NEWS right edge near the top", taps, count, 1000, 10,
                      "news_next");
}

/* The back band is 30% of the 1072 design width and the rest turns forward.
 * Links are tested before these rectangles in main, so an article can still
 * own a tap inside the back band. */
static void test_news_tap_bands_cover_thirty_percent(void)
{
    TapRect taps[MAX_TAPS];
    const TapRect *prev;
    const TapRect *next;
    int count;

    count = layout_news_taps(taps, MAX_TAPS);
    KH_CHECK_MSG(count == 2, "NEWS: expected 2 tap rectangles, got %d",
                 count);
    if (count != 2) {
        return;
    }
    prev = find_tap(taps, count, "news_prev");
    next = find_tap(taps, count, "news_next");
    if (prev == NULL || next == NULL) {
        KH_CHECK_MSG(0, "NEWS: missing news_prev or news_next");
        return;
    }
    KH_CHECK_MSG(prev->x == 0 && prev->y == 0 && prev->h == 1448,
                 "NEWS back band: expected x=0 y=0 h=1448, got x=%d y=%d "
                 "h=%d",
                 prev->x, prev->y, prev->h);
    KH_CHECK_MSG(next->y == 0 && next->h == 1448 && next->x + next->w == 1072,
                 "NEWS forward band: expected y=0 h=1448 and x+w=1072, got "
                 "x=%d y=%d w=%d h=%d",
                 next->x, next->y, next->w, next->h);
    KH_CHECK_MSG(prev->w >= 318 && prev->w <= 326,
                 "NEWS back band: expected about 30%% of 1072 (322 px), got "
                 "%d px",
                 prev->w);
    KH_CHECK_MSG(next->x == prev->x + prev->w,
                 "NEWS forward band: expected x=%d, got %d",
                 prev->x + prev->w, next->x);
    expect_tap_action("NEWS back band", taps, count, 180, 700, "news_prev");
    expect_tap_action("NEWS forward band", taps, count, 536, 512,
                      "news_next");
    expect_tap_action("NEWS right edge", taps, count, 890, 700, "news_next");
}

/* The dividing line is design x = 322: x < 322 is the previous-page band and
 * x = 322 is the first pixel of the next-page band. */
static void test_news_tap_bands_split_at_322(void)
{
    TapRect taps[MAX_TAPS];
    const TapRect *prev;
    const TapRect *next;
    int count;

    count = layout_news_taps(taps, MAX_TAPS);
    if (count != 2) {
        KH_CHECK_MSG(0, "NEWS: expected 2 tap rectangles, got %d", count);
        return;
    }
    prev = find_tap(taps, count, "news_prev");
    next = find_tap(taps, count, "news_next");
    if (prev == NULL || next == NULL) {
        KH_CHECK_MSG(0, "NEWS: missing news_prev or news_next");
        return;
    }
    KH_CHECK_MSG(prev->w == 322,
                 "NEWS back band: expected width 322, got %d", prev->w);
    expect_tap_action("NEWS x=321", taps, count, 321, 700, "news_prev");
    expect_tap_action("NEWS x=322", taps, count, 322, 700, "news_next");
}

/* A page shows Home, Front page and Refresh.  Back to paper belongs to an
 * article only. */
static void test_news_menu_taps_on_a_page(void)
{
    TapRect taps[MAX_TAPS];
    NewsMenuModel m;
    int count;

    memset(&m, 0, sizeof(m));
    m.position = 3;
    m.count = 7;
    count = layout_news_menu_taps(&m, taps, MAX_TAPS);
    KH_CHECK_MSG(count == 5,
                 "NEWS menu page: expected 5 rectangles (3 buttons, body, "
                 "grab), got %d",
                 count);
    if (count != 5) {
        return;
    }
    KH_CHECK_MSG(find_tap(taps, count, "news_menu_back") == NULL,
                 "NEWS menu page: Back to paper must not exist on a page");
    expect_tap_action("NEWS menu Home", taps, count, 100, 100,
                      "news_menu_home");
    expect_tap_action("NEWS menu Front page", taps, count, 320, 100,
                      "news_menu_front");
    expect_tap_action("NEWS menu Refresh", taps, count, 560, 100,
                      "news_menu_refresh");
    expect_tap_action("NEWS menu body", taps, count, 400, 200,
                      "news_menu_body");
    expect_tap_action("NEWS menu grab strip", taps, count, 400, 260,
                      "news_menu_dismiss");
}

static void test_news_menu_taps_on_an_article(void)
{
    TapRect taps[MAX_TAPS];
    NewsMenuModel m;
    int count;

    memset(&m, 0, sizeof(m));
    m.article = 1;
    m.position = 2;
    m.count = 4;
    count = layout_news_menu_taps(&m, taps, MAX_TAPS);
    KH_CHECK_MSG(count == 6,
                 "NEWS menu article: expected 6 rectangles (4 buttons, body, "
                 "grab), got %d",
                 count);
    if (count != 6) {
        return;
    }
    expect_tap_action("NEWS menu article Back to paper", taps, count, 900, 100,
                      "news_menu_back");
    expect_tap_action("NEWS menu article Refresh", taps, count, 560, 100,
                      "news_menu_refresh");
}

/* Pixel tests for the menu label.  The label is the only ink between the rule
 * under the buttons and the grab handle, so its ink bounding box can be
 * compared with an independently drawn copy of the expected string. */

static Fonts g_fonts;
static int g_fonts_loaded;

static int fonts_ready(void)
{
    const char *env = getenv("KHOME_FONTS");

    if (!g_fonts_loaded && env != NULL && env[0] != 0) {
        g_fonts_loaded = fonts_load(&g_fonts, env);
    }
    if (!g_fonts_loaded) {
        g_fonts_loaded = fonts_load(&g_fonts, "../design/system/fonts");
    }
    if (!g_fonts_loaded) {
        g_fonts_loaded = fonts_load(&g_fonts, "design/system/fonts");
    }
    KH_CHECK_MSG(g_fonts_loaded,
                 "fonts_load failed (run from launcher/ or set KHOME_FONTS)");
    return g_fonts_loaded;
}

static int ink_bbox(const Canvas *c, int x0, int y0, int x1, int y1, int *bx,
                    int *by, int *bw, int *bh)
{
    int min_x = c->w;
    int min_y = c->h;
    int max_x = -1;
    int max_y = -1;
    int x;
    int y;

    if (x1 > c->w) {
        x1 = c->w;
    }
    if (y1 > c->h) {
        y1 = c->h;
    }
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            if (c->pixels[(size_t)y * (size_t)c->stride + x] != 255) {
                if (x < min_x) {
                    min_x = x;
                }
                if (x > max_x) {
                    max_x = x;
                }
                if (y < min_y) {
                    min_y = y;
                }
                if (y > max_y) {
                    max_y = y;
                }
            }
        }
    }
    if (max_x < min_x) {
        return 0;
    }
    *bx = min_x;
    *by = min_y;
    *bw = max_x - min_x + 1;
    *bh = max_y - min_y + 1;
    return 1;
}

static void expect_menu_label(const char *label, int article, int position,
                              int count, const char *expected)
{
    Canvas menu;
    Canvas want;
    Icons icons;
    NewsMenuModel m;
    const Font *font;
    int got[4];
    int exp[4];
    int x;
    int y;

    if (!fonts_ready()) {
        return;
    }
    memset(&m, 0, sizeof(m));
    memset(&icons, 0, sizeof(icons));
    m.hour = 9;
    m.minute = 41;
    m.battery_level = 87;
    m.article = article;
    m.position = position;
    m.count = count;

    KH_CHECK(canvas_init(&menu, 1072, 1448));
    canvas_clear(&menu, 255);
    draw_news_menu(&menu, &g_fonts, &icons, &m, 1.0f);
    if (!ink_bbox(&menu, 0, MENU_LABEL_TOP, 1072, MENU_LABEL_BOTTOM, &got[0],
                  &got[1], &got[2], &got[3])) {
        KH_CHECK_MSG(0, "%s: no position text between the rule and the grab",
                     label);
        canvas_free(&menu);
        return;
    }

    KH_CHECK(canvas_init(&want, 600, 100));
    canvas_clear(&want, 255);
    font = fonts_get(&g_fonts, FONT_SANS_M);
    text_draw(&want, font, 28.0f, 2.0f, 10.0f, 60.0f, expected, 0, TEXT_LEFT,
              0.0f);
    if (!ink_bbox(&want, 0, 0, 600, 100, &exp[0], &exp[1], &exp[2],
                  &exp[3])) {
        KH_CHECK_MSG(0, "%s: could not draw the expected label \"%s\"", label,
                     expected);
        canvas_free(&menu);
        canvas_free(&want);
        return;
    }

    KH_CHECK_MSG(got[2] == exp[2] && got[3] == exp[3],
                 "%s: expected \"%s\" (ink %dx%d), got ink %dx%d", label,
                 expected, exp[2], exp[3], got[2], got[3]);
    if (got[2] == exp[2] && got[3] == exp[3]) {
        for (y = 0; y < exp[3]; y++) {
            for (x = 0; x < exp[2]; x++) {
                uint8_t a = menu.pixels[(size_t)(got[1] + y) *
                                            (size_t)menu.stride +
                                        got[0] + x];
                uint8_t b = want.pixels[(size_t)(exp[1] + y) *
                                            (size_t)want.stride +
                                        exp[0] + x];
                if (a != b) {
                    KH_CHECK_MSG(0,
                                 "%s: expected \"%s\" but pixel (%d, %d) of "
                                 "the label differs: expected %d, got %d",
                                 label, expected, x, y, b, a);
                    canvas_free(&menu);
                    canvas_free(&want);
                    return;
                }
            }
        }
        KH_CHECK(1);
    }
    canvas_free(&menu);
    canvas_free(&want);
}

static void test_news_menu_position_on_a_page(void)
{
    expect_menu_label("NEWS menu page label", 0, 3, 7, "Page 3 / 7");
}

static void test_news_menu_position_in_an_article(void)
{
    expect_menu_label("NEWS menu article label", 1, 2, 4, "Article 2 / 4");
}

void kh_run_news_tests(void)
{
    printf("\n[news]\n");
    kh_run_test("news.home_newspaper_opens_news",
                test_home_newspaper_opens_news);
    kh_run_test("news.navigation_events", test_news_navigation_events);
    kh_run_test("news.navigation_events_are_inert_outside_news",
                test_news_navigation_is_inert_outside_news);
    kh_run_test("news.minute_does_not_redraw_the_clock",
                test_news_minute_does_not_redraw_the_clock);
    kh_run_test("news.tap_corners", test_news_tap_corners);
    kh_run_test("news.pull_opens_the_menu_alone",
                test_news_pull_opens_the_menu_alone);
    kh_run_test("news.browser_pull_still_opens_the_sheet",
                test_browser_pull_still_opens_the_sheet);
    kh_run_test("news.menu_home_goes_home", test_menu_home_goes_home);
    kh_run_test("news.menu_front_returns_to_the_front_page",
                test_menu_front_returns_to_the_front_page);
    kh_run_test("news.menu_refresh_starts_from_the_menu",
                test_menu_refresh_starts_from_the_menu);
    kh_run_test("news.menu_back_returns_to_the_paper",
                test_menu_back_returns_to_the_paper);
    kh_run_test("news.menu_dismiss_returns_to_the_paper",
                test_menu_dismiss_returns_to_the_paper);
    kh_run_test("news.menu_fetched_redraws_the_menu",
                test_menu_fetched_redraws_the_menu);
    kh_run_test("news.menu_events_are_inert_outside_the_menu",
                test_menu_events_are_inert_outside_the_menu);
    kh_run_test("news.sleep_and_wake_return_to_news",
                test_sleep_and_wake_return_to_news);
    kh_run_test("news.menu_clock_updates_redraw_only_the_status",
                test_menu_clock_updates_redraw_only_the_status);
    kh_run_test("news.tap_bands_cover_thirty_percent",
                test_news_tap_bands_cover_thirty_percent);
    kh_run_test("news.tap_bands_split_at_322",
                test_news_tap_bands_split_at_322);
    kh_run_test("news.menu_taps_on_a_page", test_news_menu_taps_on_a_page);
    kh_run_test("news.menu_taps_on_an_article",
                test_news_menu_taps_on_an_article);
    kh_run_test("news.menu_position_on_a_page",
                test_news_menu_position_on_a_page);
    kh_run_test("news.menu_position_in_an_article",
                test_news_menu_position_in_an_article);
}
