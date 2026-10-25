#include "suites.h"

#include <stdio.h>
#include <string.h>

#include "fixtures.h"
#include "layout.h"
#include "state.h"
#include "tabs.h"
#include "test_harness.h"

/* Holding a favorite tile asks before removing it.  The state table owns the
 * dialog, the layout owns its two buttons, and favorites_remove owns the list;
 * together they are what the user sees. */

#define MAX_TAPS 24
#define DESIGN_W 1072
#define DESIGN_H 1448

static KhomeMachine at_state(KhomeState state)
{
    KhomeMachine m;

    m.state = state;
    m.asleep = 0;
    m.confirm = KHOME_CONFIRM_NONE;
    return m;
}

static KhomeMachine at_home_confirm(void)
{
    KhomeMachine m = at_state(KHOME_STATE_HOME_CONFIRM);

    m.confirm = KHOME_CONFIRM_REMOVE_FAVORITE;
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

static void test_long_press_favorite_asks_first(void)
{
    static const KhomeAction draw[] = { KHOME_ACT_DRAW_CONFIRM };
    KhomeStep step =
        khome_step(at_state(KHOME_STATE_HOME), KHOME_EV_LONG_FAVORITE);

    expect_step("HOME + long-favorite", step, KHOME_STATE_HOME_CONFIRM, draw,
                1);
    KH_CHECK_MSG(step.confirm == KHOME_CONFIRM_REMOVE_FAVORITE,
                 "HOME + long-favorite: expected the remove dialog, got "
                 "confirm %d",
                 (int)step.confirm);
}

static void test_remove_confirm_cancel_returns_home(void)
{
    static const KhomeAction draw_home[] = { KHOME_ACT_DRAW_HOME };
    KhomeStep step =
        khome_step(at_home_confirm(), KHOME_EV_CONFIRM_CANCEL);

    expect_step("HOME_CONFIRM + confirm-cancel", step, KHOME_STATE_HOME,
                draw_home, 1);
    KH_CHECK_MSG(step.confirm == KHOME_CONFIRM_NONE,
                 "HOME_CONFIRM + confirm-cancel: expected no dialog, got %d",
                 (int)step.confirm);
}

static void test_remove_confirm_remove_returns_home(void)
{
    static const KhomeAction remove[] = { KHOME_ACT_REMOVE_FAVORITE,
                                          KHOME_ACT_DRAW_HOME };
    KhomeStep step =
        khome_step(at_home_confirm(), KHOME_EV_CONFIRM_REMOVE_FAVORITE);

    expect_step("HOME_CONFIRM + confirm-remove-favorite", step,
                KHOME_STATE_HOME, remove, 2);
    KH_CHECK_MSG(step.confirm == KHOME_CONFIRM_NONE,
                 "HOME_CONFIRM + confirm-remove-favorite: expected no dialog, "
                 "got %d",
                 (int)step.confirm);
}

static void test_long_press_outside_home_is_inert(void)
{
    static const struct {
        KhomeState state;
        KhomeConfirm confirm;
    } cases[] = {
        { KHOME_STATE_BROWSER, KHOME_CONFIRM_NONE },
        { KHOME_STATE_SHEET, KHOME_CONFIRM_NONE },
        { KHOME_STATE_EDIT, KHOME_CONFIRM_NONE },
        { KHOME_STATE_KOREADER, KHOME_CONFIRM_NONE },
        { KHOME_STATE_NEWS, KHOME_CONFIRM_NONE },
        { KHOME_STATE_SETTINGS, KHOME_CONFIRM_NONE },
        { KHOME_STATE_SETTINGS_CONFIRM, KHOME_CONFIRM_REBOOT },
        { KHOME_STATE_HOME_CONFIRM, KHOME_CONFIRM_REMOVE_FAVORITE }
    };
    char label[96];
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        KhomeMachine m = at_state(cases[i].state);
        KhomeStep step;

        m.confirm = cases[i].confirm;
        step = khome_step(m, KHOME_EV_LONG_FAVORITE);
        snprintf(label, sizeof(label), "%s + long-favorite",
                 khome_state_name(cases[i].state));
        expect_step(label, step, cases[i].state, NULL, 0);
        KH_CHECK_MSG(step.confirm == cases[i].confirm,
                     "%s: the dialog state must not change (%d, expected %d)",
                     label, (int)step.confirm, (int)cases[i].confirm);
    }
}

static void test_confirm_remove_without_a_dialog_is_inert(void)
{
    KhomeStep step =
        khome_step(at_state(KHOME_STATE_HOME),
                   KHOME_EV_CONFIRM_REMOVE_FAVORITE);

    expect_step("HOME + confirm-remove-favorite", step, KHOME_STATE_HOME, NULL,
                0);
}

/* The dialog's two buttons, in the order they are drawn. */
static void test_remove_dialog_has_remove_and_cancel(void)
{
    TapRect taps[8];
    int count = layout_confirm_taps(KHOME_CONFIRM_REMOVE_FAVORITE, taps, 8);
    int i;

    KH_CHECK_MSG(count == 2, "remove dialog: expected 2 buttons, got %d",
                 count);
    if (count != 2) {
        return;
    }
    KH_CHECK_MSG(strcmp(taps[0].action, "confirm_favorite_remove") == 0,
                 "remove dialog: first button: expected "
                 "confirm_favorite_remove, got %s",
                 taps[0].action);
    KH_CHECK_MSG(strcmp(taps[1].action, "confirm_cancel") == 0,
                 "remove dialog: second button: expected confirm_cancel, got "
                 "%s",
                 taps[1].action);
    for (i = 0; i < count; i++) {
        KH_CHECK_MSG(taps[i].x >= 0 && taps[i].y >= 0 &&
                         taps[i].x + taps[i].w <= DESIGN_W &&
                         taps[i].y + taps[i].h <= DESIGN_H,
                     "remove dialog: %s (%d, %d, %d, %d) is outside the "
                     "design canvas",
                     taps[i].action, taps[i].x, taps[i].y, taps[i].w,
                     taps[i].h);
    }
    KH_CHECK_MSG(taps[1].y >= taps[0].y + taps[0].h,
                 "remove dialog: Cancel at y=%d overlaps Remove at y=%d h=%d",
                 taps[1].y, taps[0].y, taps[0].h);
}

/* Each home page holds six favorites, so seven put one tile on page 2.
 * Removing it leaves six, one page, and the home must show that page even
 * though the model still points at the old page number. */
static int expect_home_page(const char *label, const HomeModel *m, int start,
                            int expected)
{
    TapRect taps[MAX_TAPS];
    int count = layout_home_taps(m, taps, MAX_TAPS);
    int seen = 0;
    int i;

    for (i = 0; i < count; i++) {
        char action[32];

        if (strncmp(taps[i].action, "fav_open_", 9) != 0) {
            continue;
        }
        snprintf(action, sizeof(action), "fav_open_%02d", start + seen + 1);
        KH_CHECK_MSG(strcmp(taps[i].action, action) == 0,
                     "%s: tile %d: expected action %s, got %s", label, seen,
                     action, taps[i].action);
        KH_CHECK_MSG(taps[i].index == start + seen,
                     "%s: tile %d: expected favorite index %d, got %d", label,
                     seen, start + seen, taps[i].index);
        seen++;
    }
    KH_CHECK_MSG(seen == expected, "%s: expected %d favorite tiles from %d, "
                 "got %d",
                 label, expected, start, seen);
    return seen;
}

static void test_removing_the_last_page_falls_back_to_page_one(void)
{
    HomeModel m;
    TapRect taps[MAX_TAPS];
    int count;

    fixture_home_many(&m);
    m.favorite_count = 7;
    m.page = 1;

    KH_CHECK_MSG(expect_home_page("7 favorites on page 2", &m, 6, 1) == 1,
                 "setup: seven favorites must leave one tile on page 2");
    count = layout_home_taps(&m, taps, MAX_TAPS);
    KH_CHECK_MSG(find_tap(taps, count, "page_prev") != NULL,
                 "setup: seven favorites must show the pager");
    KH_CHECK_MSG(find_tap(taps, count, "page_next") != NULL,
                 "setup: seven favorites must show the pager");

    /* main removes the tile's index, which the long press carries. */
    KH_CHECK_MSG(favorites_remove(m.favorites, &m.favorite_count, 6) == 0,
                 "removing the favorite at index 6 must succeed");
    KH_CHECK_MSG(m.favorite_count == 6,
                 "after removing one of seven: expected 6 favorites, got %d",
                 m.favorite_count);

    KH_CHECK_MSG(expect_home_page("after removing the only favorite on page "
                                  "2",
                                  &m, 0, 6) == 6,
                 "after the page disappears the home must show all six "
                 "remaining favorites");
    count = layout_home_taps(&m, taps, MAX_TAPS);
    KH_CHECK_MSG(find_tap(taps, count, "page_prev") == NULL,
                 "one page left: page_prev must be gone");
    KH_CHECK_MSG(find_tap(taps, count, "page_next") == NULL,
                 "one page left: page_next must be gone");
}

void kh_run_long_press_tests(void)
{
    printf("\n[long_press]\n");
    kh_run_test("long_press.favorite_asks_first",
                test_long_press_favorite_asks_first);
    kh_run_test("long_press.confirm_cancel_returns_home",
                test_remove_confirm_cancel_returns_home);
    kh_run_test("long_press.confirm_remove_returns_home",
                test_remove_confirm_remove_returns_home);
    kh_run_test("long_press.outside_home_is_inert",
                test_long_press_outside_home_is_inert);
    kh_run_test("long_press.confirm_remove_without_a_dialog_is_inert",
                test_confirm_remove_without_a_dialog_is_inert);
    kh_run_test("long_press.dialog_has_remove_and_cancel",
                test_remove_dialog_has_remove_and_cancel);
    kh_run_test("long_press.removing_the_last_page_falls_back_to_page_one",
                test_removing_the_last_page_falls_back_to_page_one);
}
