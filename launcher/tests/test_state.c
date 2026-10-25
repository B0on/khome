#include "suites.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "state.h"
#include "test_harness.h"

/* State table from src/state.c: HOME draws the framebuffer and holds the
 * touchscreen; BROWSER and KOREADER have both until the app exits.  The pure
 * step function returns both the next state and the actions main must run. */

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

static void expect_confirm(const char *label, KhomeStep got,
                           KhomeConfirm confirm)
{
    KH_CHECK_MSG(got.confirm == confirm,
                 "%s: expected confirm %d, got %d", label, (int)confirm,
                 (int)got.confirm);
}

static KhomeMachine at_confirm(KhomeConfirm confirm)
{
    KhomeMachine m = at_state(KHOME_STATE_SETTINGS_CONFIRM);

    m.confirm = confirm;
    return m;
}

static void test_home_app_taps_release(void)
{
    static const KhomeAction browser_actions[] = { KHOME_ACT_UNGRAB,
                                                   KHOME_ACT_START_BROWSER };
    static const KhomeAction manga_actions[] = { KHOME_ACT_UNGRAB,
                                                 KHOME_ACT_START_KOREADER };

    expect_step("HOME + tap-browser",
                khome_step(at_state(KHOME_STATE_HOME), KHOME_EV_TAP_BROWSER),
                KHOME_STATE_BROWSER, browser_actions, 2);
    expect_step("HOME + tap-add",
                khome_step(at_state(KHOME_STATE_HOME), KHOME_EV_TAP_ADD),
                KHOME_STATE_BROWSER, browser_actions, 2);
    expect_step("HOME + tap-manga",
                khome_step(at_state(KHOME_STATE_HOME), KHOME_EV_TAP_MANGA),
                KHOME_STATE_KOREADER, manga_actions, 2);
}

static void test_out_of_screen_saver_redraws_only_when_owning(void)
{
    static const KhomeAction schedule[] = { KHOME_ACT_SCHEDULE_REDRAW };
    static const KhomeAction redraw[] = { KHOME_ACT_DRAW_HOME };
    KhomeStep step;

    step = khome_step(at_state(KHOME_STATE_HOME), KHOME_EV_POWER_OUT_OF_SS);
    expect_step("HOME + outOfScreenSaver", step, KHOME_STATE_HOME, schedule,
                1);
    step = khome_step(after_step(step), KHOME_EV_DEFER_REDRAW);
    expect_step("HOME + defer-redraw", step, KHOME_STATE_HOME, redraw, 1);

    expect_step("BROWSER + outOfScreenSaver",
                khome_step(at_state(KHOME_STATE_BROWSER),
                           KHOME_EV_POWER_OUT_OF_SS),
                KHOME_STATE_BROWSER, NULL, 0);
    expect_step("KOREADER + outOfScreenSaver",
                khome_step(at_state(KHOME_STATE_KOREADER),
                           KHOME_EV_POWER_OUT_OF_SS),
                KHOME_STATE_KOREADER, NULL, 0);
}

static void test_start_owns_home(void)
{
    static const KhomeAction start_actions[] = { KHOME_ACT_GRAB,
                                                 KHOME_ACT_DRAW_HOME };

    expect_step("HOME + start",
                khome_step(at_state(KHOME_STATE_HOME), KHOME_EV_START),
                KHOME_STATE_HOME, start_actions, 2);
}

static void test_browser_pull_opens_sheet(void)
{
    static const KhomeAction actions[] = { KHOME_ACT_GRAB,
                                           KHOME_ACT_DRAW_SHEET };

    expect_step("BROWSER + sheet-pull",
                khome_step(at_state(KHOME_STATE_BROWSER), KHOME_EV_SHEET_PULL),
                KHOME_STATE_SHEET, actions, 2);
}

static void test_sheet_buttons(void)
{
    static const KhomeAction dismiss[] = { KHOME_ACT_UNGRAB,
                                           KHOME_ACT_HIDE_SHEET };
    static const KhomeAction back[] = { KHOME_ACT_BROWSER_BACK,
                                        KHOME_ACT_UNGRAB,
                                        KHOME_ACT_HIDE_SHEET };
    static const KhomeAction home[] = { KHOME_ACT_CLOSE_BROWSER };
    static const KhomeAction new_tab[] = { KHOME_ACT_DRAW_EDIT };
    static const KhomeAction tab[] = { KHOME_ACT_SHEET_TAB,
                                       KHOME_ACT_UNGRAB,
                                       KHOME_ACT_HIDE_SHEET };

    expect_step("SHEET + sheet-dismiss",
                khome_step(at_state(KHOME_STATE_SHEET), KHOME_EV_SHEET_DISMISS),
                KHOME_STATE_BROWSER, dismiss, 2);
    expect_step("SHEET + sheet-back",
                khome_step(at_state(KHOME_STATE_SHEET), KHOME_EV_SHEET_BACK),
                KHOME_STATE_BROWSER, back, 3);
    expect_step("SHEET + sheet-home",
                khome_step(at_state(KHOME_STATE_SHEET), KHOME_EV_SHEET_HOME),
                KHOME_STATE_HOME, home, 1);
    expect_step("SHEET + sheet-new",
                khome_step(at_state(KHOME_STATE_SHEET), KHOME_EV_SHEET_NEW),
                KHOME_STATE_EDIT, new_tab, 1);
    expect_step("SHEET + sheet-tab",
                khome_step(at_state(KHOME_STATE_SHEET), KHOME_EV_SHEET_TAB),
                KHOME_STATE_BROWSER, tab, 3);
}

static void test_sheet_close_variants(void)
{
    static const KhomeAction background[] = { KHOME_ACT_SHEET_CLOSE };
    static const KhomeAction active[] = { KHOME_ACT_SHEET_CLOSE_ACTIVE,
                                           KHOME_ACT_UNGRAB,
                                           KHOME_ACT_HIDE_SHEET };
    static const KhomeAction last[] = { KHOME_ACT_SHEET_CLOSE_LAST };

    expect_step("SHEET + sheet-close on a background tab",
                khome_step(at_state(KHOME_STATE_SHEET), KHOME_EV_SHEET_CLOSE),
                KHOME_STATE_SHEET, background, 1);
    expect_step("SHEET + sheet-close-active",
                khome_step(at_state(KHOME_STATE_SHEET), KHOME_EV_SHEET_CLOSE_ACTIVE),
                KHOME_STATE_BROWSER, active, 3);
    expect_step("SHEET + sheet-close-last",
                khome_step(at_state(KHOME_STATE_SHEET), KHOME_EV_SHEET_CLOSE_LAST),
                KHOME_STATE_HOME, last, 1);
}

static void test_sheet_wake_and_browser_end(void)
{
    static const KhomeAction ungrab_sheet[] = { KHOME_ACT_UNGRAB,
                                                KHOME_ACT_HIDE_SHEET };
    static const KhomeAction resume[] = { KHOME_ACT_GRAB,
                                          KHOME_ACT_DRAW_HOME };
    static const KhomeAction close[] = { KHOME_ACT_CLOSE_BROWSER };

    expect_step("SHEET + outOfScreenSaver",
                khome_step(at_state(KHOME_STATE_SHEET), KHOME_EV_POWER_OUT_OF_SS),
                KHOME_STATE_BROWSER, ungrab_sheet, 2);
    expect_step("SHEET + browser-exit",
                khome_step(at_state(KHOME_STATE_SHEET), KHOME_EV_BROWSER_EXIT),
                KHOME_STATE_HOME, resume, 2);
    expect_step("SHEET + browser-close",
                khome_step(at_state(KHOME_STATE_SHEET), KHOME_EV_BROWSER_CLOSE),
                KHOME_STATE_HOME, close, 1);
}

/* browser_launch_abort() sets the footer notice and then feeds BROWSER_EXIT,
 * which is the state-table half of returning the panel to HOME. */
static void test_browser_exit_returns_home(void)
{
    static const KhomeAction resume[] = { KHOME_ACT_GRAB,
                                          KHOME_ACT_DRAW_HOME };

    expect_step("BROWSER + browser-exit",
                khome_step(at_state(KHOME_STATE_BROWSER),
                           KHOME_EV_BROWSER_EXIT),
                KHOME_STATE_HOME, resume, 2);
}

static KhomeMachine asleep_at(KhomeState state)
{
    KhomeMachine m;

    m.state = state;
    m.asleep = 1;
    m.confirm = KHOME_CONFIRM_NONE;
    return m;
}

static void expect_step_asleep(const char *label, KhomeStep got,
                               KhomeState state, int asleep,
                               const KhomeAction *actions, int action_count)
{
    expect_step(label, got, state, actions, action_count);
    KH_CHECK_MSG(got.asleep == asleep, "%s: expected asleep %d, got %d", label,
                 asleep, got.asleep);
}

static void expect_step_asleep_confirm(const char *label, KhomeStep got,
                                       KhomeState state, int asleep,
                                       KhomeConfirm confirm,
                                       const KhomeAction *actions,
                                       int action_count)
{
    expect_step_asleep(label, got, state, asleep, actions, action_count);
    expect_confirm(label, got, confirm);
}

static void test_going_to_sleep_keeps_owning_state(void)
{
    static const KhomeState owning[] = { KHOME_STATE_HOME, KHOME_STATE_BROWSER,
                                         KHOME_STATE_KOREADER };
    static const KhomeAction sheet_hide[] = { KHOME_ACT_UNGRAB,
                                              KHOME_ACT_HIDE_SHEET };
    static const KhomeAction edit_hide[] = { KHOME_ACT_UNGRAB,
                                             KHOME_ACT_HIDE_EDIT };
    char label[96];
    size_t i;

    for (i = 0; i < sizeof(owning) / sizeof(owning[0]); i++) {
        snprintf(label, sizeof(label), "%s + goingToScreenSaver",
                 khome_state_name(owning[i]));
        expect_step_asleep(label,
                           khome_step(at_state(owning[i]),
                                      KHOME_EV_POWER_GOING_TO_SS),
                           owning[i], 1, NULL, 0);
    }
    expect_step_asleep("SHEET + goingToScreenSaver",
                       khome_step(at_state(KHOME_STATE_SHEET),
                                  KHOME_EV_POWER_GOING_TO_SS),
                       KHOME_STATE_BROWSER, 1, sheet_hide, 2);
    expect_step_asleep("EDIT + goingToScreenSaver",
                       khome_step(at_state(KHOME_STATE_EDIT),
                                  KHOME_EV_POWER_GOING_TO_SS),
                       KHOME_STATE_BROWSER, 1, edit_hide, 2);
}

static void test_sleeping_ignores_background_events(void)
{
    static const KhomeState states[] = { KHOME_STATE_HOME, KHOME_STATE_BROWSER,
                                         KHOME_STATE_SHEET, KHOME_STATE_EDIT,
                                         KHOME_STATE_KOREADER };
    static const KhomeEvent events[] = {
        KHOME_EV_MINUTE,        KHOME_EV_STATUS_CHANGED,
        KHOME_EV_DEFER_REDRAW,  KHOME_EV_DAY_CHANGED,
        KHOME_EV_TAP_BROWSER,   KHOME_EV_TAP_ADD,
        KHOME_EV_TAP_MANGA,     KHOME_EV_TAP_NEWSPAPER,
        KHOME_EV_TAP_SETTINGS,  KHOME_EV_SHEET_PULL
    };
    char label[96];
    size_t i;
    size_t j;

    for (i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        for (j = 0; j < sizeof(events) / sizeof(events[0]); j++) {
            snprintf(label, sizeof(label), "asleep %s + %s",
                     khome_state_name(states[i]),
                     khome_event_name(events[j]));
            expect_step_asleep(label,
                               khome_step(asleep_at(states[i]), events[j]),
                               states[i], 1, NULL, 0);
        }
    }
}

static void test_sleeping_app_exits_do_not_draw(void)
{
    static const KhomeAction grab[] = { KHOME_ACT_GRAB };
    static const KhomeAction close[] = { KHOME_ACT_CLOSE_BROWSER };

    expect_step_asleep("asleep BROWSER + browser-exit",
                       khome_step(asleep_at(KHOME_STATE_BROWSER),
                                  KHOME_EV_BROWSER_EXIT),
                       KHOME_STATE_HOME, 1, grab, 1);
    expect_step_asleep("asleep SHEET + browser-exit",
                       khome_step(asleep_at(KHOME_STATE_SHEET),
                                  KHOME_EV_BROWSER_EXIT),
                       KHOME_STATE_HOME, 1, grab, 1);
    expect_step_asleep("asleep EDIT + browser-exit",
                       khome_step(asleep_at(KHOME_STATE_EDIT),
                                  KHOME_EV_BROWSER_EXIT),
                       KHOME_STATE_HOME, 1, grab, 1);
    expect_step_asleep("asleep KOREADER + koreader-exit",
                       khome_step(asleep_at(KHOME_STATE_KOREADER),
                                  KHOME_EV_KOREADER_EXIT),
                       KHOME_STATE_HOME, 1, grab, 1);
    expect_step_asleep("asleep BROWSER + browser-close",
                       khome_step(asleep_at(KHOME_STATE_BROWSER),
                                  KHOME_EV_BROWSER_CLOSE),
                       KHOME_STATE_HOME, 1, close, 1);
    expect_step_asleep("asleep SHEET + browser-close",
                       khome_step(asleep_at(KHOME_STATE_SHEET),
                                  KHOME_EV_BROWSER_CLOSE),
                       KHOME_STATE_HOME, 1, close, 1);
    expect_step_asleep("asleep EDIT + browser-close",
                       khome_step(asleep_at(KHOME_STATE_EDIT),
                                  KHOME_EV_BROWSER_CLOSE),
                       KHOME_STATE_HOME, 1, close, 1);
}

static void test_sleeping_quit_still_quits(void)
{
    static const KhomeState states[] = { KHOME_STATE_HOME, KHOME_STATE_BROWSER,
                                         KHOME_STATE_SHEET, KHOME_STATE_EDIT,
                                         KHOME_STATE_KOREADER };
    static const KhomeAction quit[] = { KHOME_ACT_QUIT };
    char label[96];
    size_t i;

    for (i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        snprintf(label, sizeof(label), "asleep %s + quit",
                 khome_state_name(states[i]));
        expect_step_asleep(label, khome_step(asleep_at(states[i]),
                                             KHOME_EV_QUIT),
                           states[i], 1, quit, 1);
    }
}

static void test_out_of_screen_saver_wakes(void)
{
    static const KhomeAction schedule[] = { KHOME_ACT_SCHEDULE_REDRAW };
    static const KhomeAction ungrab_sheet[] = { KHOME_ACT_UNGRAB,
                                                KHOME_ACT_HIDE_SHEET };
    static const KhomeAction ungrab_edit[] = { KHOME_ACT_UNGRAB,
                                               KHOME_ACT_HIDE_EDIT };

    expect_step_asleep("asleep HOME + outOfScreenSaver",
                       khome_step(asleep_at(KHOME_STATE_HOME),
                                  KHOME_EV_POWER_OUT_OF_SS),
                       KHOME_STATE_HOME, 0, schedule, 1);
    expect_step_asleep("asleep SHEET + outOfScreenSaver",
                       khome_step(asleep_at(KHOME_STATE_SHEET),
                                  KHOME_EV_POWER_OUT_OF_SS),
                       KHOME_STATE_BROWSER, 0, ungrab_sheet, 2);
    expect_step_asleep("asleep EDIT + outOfScreenSaver",
                       khome_step(asleep_at(KHOME_STATE_EDIT),
                                  KHOME_EV_POWER_OUT_OF_SS),
                       KHOME_STATE_BROWSER, 0, ungrab_edit, 2);
    expect_step_asleep("asleep BROWSER + outOfScreenSaver",
                       khome_step(asleep_at(KHOME_STATE_BROWSER),
                                  KHOME_EV_POWER_OUT_OF_SS),
                       KHOME_STATE_BROWSER, 0, NULL, 0);
    expect_step_asleep("asleep KOREADER + outOfScreenSaver",
                       khome_step(asleep_at(KHOME_STATE_KOREADER),
                                  KHOME_EV_POWER_OUT_OF_SS),
                       KHOME_STATE_KOREADER, 0, NULL, 0);
}

static void test_sleep_and_wake_sequence_draws_once(void)
{
    static const KhomeAction schedule[] = { KHOME_ACT_SCHEDULE_REDRAW };
    static const KhomeAction redraw[] = { KHOME_ACT_DRAW_HOME };
    KhomeStep step;

    step = khome_step(at_state(KHOME_STATE_HOME), KHOME_EV_POWER_GOING_TO_SS);
    expect_step_asleep("sequence: goingToScreenSaver", step, KHOME_STATE_HOME,
                       1, NULL, 0);
    step = khome_step(after_step(step), KHOME_EV_MINUTE);
    expect_step_asleep("sequence: minute while asleep", step, KHOME_STATE_HOME,
                       1, NULL, 0);
    step = khome_step(after_step(step), KHOME_EV_STATUS_CHANGED);
    expect_step_asleep("sequence: status-changed while asleep", step,
                       KHOME_STATE_HOME, 1, NULL, 0);
    step = khome_step(after_step(step), KHOME_EV_POWER_OUT_OF_SS);
    expect_step_asleep("sequence: outOfScreenSaver", step, KHOME_STATE_HOME, 0,
                       schedule, 1);
    step = khome_step(after_step(step), KHOME_EV_DEFER_REDRAW);
    expect_step_asleep("sequence: defer-redraw", step, KHOME_STATE_HOME, 0,
                       redraw, 1);
}

static void test_day_changed_redraws_only_home(void)
{
    static const KhomeState others[] = { KHOME_STATE_BROWSER,
                                         KHOME_STATE_SHEET, KHOME_STATE_EDIT,
                                         KHOME_STATE_KOREADER };
    static const KhomeAction redraw[] = { KHOME_ACT_DRAW_HOME };
    char label[96];
    size_t i;

    expect_step("HOME + day-changed",
                khome_step(at_state(KHOME_STATE_HOME), KHOME_EV_DAY_CHANGED),
                KHOME_STATE_HOME, redraw, 1);
    for (i = 0; i < sizeof(others) / sizeof(others[0]); i++) {
        snprintf(label, sizeof(label), "%s + day-changed",
                 khome_state_name(others[i]));
        expect_step(label,
                    khome_step(at_state(others[i]), KHOME_EV_DAY_CHANGED),
                    others[i], NULL, 0);
    }
}

static void test_awake_timers_keep_their_actions(void)
{
    static const KhomeState others[] = { KHOME_STATE_BROWSER,
                                         KHOME_STATE_EDIT,
                                         KHOME_STATE_KOREADER };
    static const KhomeAction clock[] = { KHOME_ACT_DRAW_CLOCK };
    static const KhomeAction sheet_clock[] = { KHOME_ACT_DRAW_SHEET_STATUS };
    static const KhomeAction redraw[] = { KHOME_ACT_DRAW_HOME };
    static const KhomeAction draw_settings[] = { KHOME_ACT_DRAW_SETTINGS };
    static const KhomeAction news[] = { KHOME_ACT_DRAW_NEWS,
                                        KHOME_ACT_NEWS_FETCH };
    char label[96];
    size_t i;

    expect_step("HOME + minute",
                khome_step(at_state(KHOME_STATE_HOME), KHOME_EV_MINUTE),
                KHOME_STATE_HOME, clock, 1);
    expect_step("SHEET + minute",
                khome_step(at_state(KHOME_STATE_SHEET), KHOME_EV_MINUTE),
                KHOME_STATE_SHEET, sheet_clock, 1);
    for (i = 0; i < sizeof(others) / sizeof(others[0]); i++) {
        snprintf(label, sizeof(label), "%s + minute",
                 khome_state_name(others[i]));
        expect_step(label, khome_step(at_state(others[i]), KHOME_EV_MINUTE),
                    others[i], NULL, 0);
    }

    expect_step("HOME + status-changed",
                khome_step(at_state(KHOME_STATE_HOME),
                           KHOME_EV_STATUS_CHANGED),
                KHOME_STATE_HOME, redraw, 1);
    expect_step("SHEET + status-changed",
                khome_step(at_state(KHOME_STATE_SHEET),
                           KHOME_EV_STATUS_CHANGED),
                KHOME_STATE_SHEET, sheet_clock, 1);
    for (i = 0; i < sizeof(others) / sizeof(others[0]); i++) {
        snprintf(label, sizeof(label), "%s + status-changed",
                 khome_state_name(others[i]));
        expect_step(label,
                    khome_step(at_state(others[i]), KHOME_EV_STATUS_CHANGED),
                    others[i], NULL, 0);
    }

    expect_step("HOME + defer-redraw",
                khome_step(at_state(KHOME_STATE_HOME), KHOME_EV_DEFER_REDRAW),
                KHOME_STATE_HOME, redraw, 1);
    for (i = 0; i < sizeof(others) / sizeof(others[0]); i++) {
        snprintf(label, sizeof(label), "%s + defer-redraw",
                 khome_state_name(others[i]));
        expect_step(label,
                    khome_step(at_state(others[i]), KHOME_EV_DEFER_REDRAW),
                    others[i], NULL, 0);
    }

    expect_step("HOME + tap-newspaper",
                khome_step(at_state(KHOME_STATE_HOME), KHOME_EV_TAP_NEWSPAPER),
                KHOME_STATE_NEWS, news, 2);
    expect_step("HOME + tap-settings",
                khome_step(at_state(KHOME_STATE_HOME), KHOME_EV_TAP_SETTINGS),
                KHOME_STATE_SETTINGS, draw_settings, 1);
}

static void test_sheet_events_outside_sheet_are_inert(void)
{
    static const KhomeState states[] = { KHOME_STATE_HOME, KHOME_STATE_BROWSER,
                                         KHOME_STATE_KOREADER };
    static const KhomeEvent events[] = {
        KHOME_EV_SHEET_DISMISS,      KHOME_EV_SHEET_BACK,
        KHOME_EV_SHEET_HOME,         KHOME_EV_SHEET_NEW,
        KHOME_EV_SHEET_TAB,          KHOME_EV_SHEET_CLOSE,
        KHOME_EV_SHEET_CLOSE_ACTIVE, KHOME_EV_SHEET_CLOSE_LAST
    };
    char label[96];
    size_t i;
    size_t j;

    for (i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        for (j = 0; j < sizeof(events) / sizeof(events[0]); j++) {
            snprintf(label, sizeof(label), "%s + %s",
                     khome_state_name(states[i]),
                     khome_event_name(events[j]));
            expect_step(label, khome_step(at_state(states[i]), events[j]),
                        states[i], NULL, 0);
        }
    }
    expect_step("HOME + sheet-pull",
                khome_step(at_state(KHOME_STATE_HOME), KHOME_EV_SHEET_PULL),
                KHOME_STATE_HOME, NULL, 0);
    expect_step("KOREADER + sheet-pull",
                khome_step(at_state(KHOME_STATE_KOREADER), KHOME_EV_SHEET_PULL),
                KHOME_STATE_KOREADER, NULL, 0);
}

static void test_sheet_edit_enters_editor(void)
{
    static const KhomeAction draw_edit[] = { KHOME_ACT_DRAW_EDIT };
    static const KhomeState non_sheet[] = { KHOME_STATE_HOME,
                                            KHOME_STATE_BROWSER,
                                            KHOME_STATE_EDIT,
                                            KHOME_STATE_KOREADER };
    char label[96];
    size_t i;

    expect_step("SHEET + sheet-edit",
                khome_step(at_state(KHOME_STATE_SHEET), KHOME_EV_SHEET_EDIT),
                KHOME_STATE_EDIT, draw_edit, 1);
    for (i = 0; i < sizeof(non_sheet) / sizeof(non_sheet[0]); i++) {
        snprintf(label, sizeof(label), "%s + sheet-edit",
                 khome_state_name(non_sheet[i]));
        expect_step(label,
                    khome_step(at_state(non_sheet[i]), KHOME_EV_SHEET_EDIT),
                    non_sheet[i], NULL, 0);
    }
}

static void test_edit_cancel_returns_to_sheet(void)
{
    static const KhomeAction back_to_sheet[] = { KHOME_ACT_HIDE_EDIT,
                                                  KHOME_ACT_DRAW_SHEET };
    static const KhomeState non_edit[] = { KHOME_STATE_HOME,
                                           KHOME_STATE_BROWSER,
                                           KHOME_STATE_SHEET,
                                           KHOME_STATE_KOREADER };
    char label[96];
    size_t i;

    expect_step("EDIT + edit-cancel",
                khome_step(at_state(KHOME_STATE_EDIT), KHOME_EV_EDIT_CANCEL),
                KHOME_STATE_SHEET, back_to_sheet, 2);
    for (i = 0; i < sizeof(non_edit) / sizeof(non_edit[0]); i++) {
        snprintf(label, sizeof(label), "%s + edit-cancel",
                 khome_state_name(non_edit[i]));
        expect_step(label,
                    khome_step(at_state(non_edit[i]), KHOME_EV_EDIT_CANCEL),
                    non_edit[i], NULL, 0);
    }
}

static void test_edit_open_navigates_to_browser(void)
{
    static const KhomeAction open[] = { KHOME_ACT_EDIT_OPEN,
                                        KHOME_ACT_UNGRAB,
                                        KHOME_ACT_HIDE_EDIT,
                                        KHOME_ACT_OPEN_URL };
    static const KhomeState non_edit[] = { KHOME_STATE_HOME,
                                           KHOME_STATE_BROWSER,
                                           KHOME_STATE_SHEET,
                                           KHOME_STATE_KOREADER };
    char label[96];
    size_t i;

    expect_step("EDIT + edit-open",
                khome_step(at_state(KHOME_STATE_EDIT), KHOME_EV_EDIT_OPEN),
                KHOME_STATE_BROWSER, open, 4);
    for (i = 0; i < sizeof(non_edit) / sizeof(non_edit[0]); i++) {
        snprintf(label, sizeof(label), "%s + edit-open",
                 khome_state_name(non_edit[i]));
        expect_step(label,
                    khome_step(at_state(non_edit[i]), KHOME_EV_EDIT_OPEN),
                    non_edit[i], NULL, 0);
    }
}

static void test_edit_ignores_unrelated_events(void)
{
    static const KhomeEvent events[] = {
        KHOME_EV_MINUTE,         KHOME_EV_STATUS_CHANGED,
        KHOME_EV_DEFER_REDRAW,   KHOME_EV_DAY_CHANGED,
        KHOME_EV_TAP_BROWSER,    KHOME_EV_TAP_ADD,
        KHOME_EV_TAP_MANGA,      KHOME_EV_TAP_NEWSPAPER,
        KHOME_EV_TAP_SETTINGS,   KHOME_EV_SHEET_PULL,
        KHOME_EV_SHEET_BACK,     KHOME_EV_SHEET_HOME,
        KHOME_EV_SHEET_NEW,      KHOME_EV_SHEET_TAB,
        KHOME_EV_SHEET_CLOSE,      KHOME_EV_SHEET_CLOSE_ACTIVE,
        KHOME_EV_SHEET_CLOSE_LAST, KHOME_EV_SHEET_DISMISS,
        KHOME_EV_KOREADER_EXIT,    KHOME_EV_START
    };
    char label[96];
    size_t i;

    for (i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
        snprintf(label, sizeof(label), "EDIT + %s",
                 khome_event_name(events[i]));
        expect_step(label, khome_step(at_state(KHOME_STATE_EDIT), events[i]),
                    KHOME_STATE_EDIT, NULL, 0);
    }
}

static void test_edit_sleep_and_browser_end(void)
{
    static const KhomeAction wake_edit[] = { KHOME_ACT_UNGRAB,
                                             KHOME_ACT_HIDE_EDIT };
    static const KhomeAction grab[] = { KHOME_ACT_GRAB };
    static const KhomeAction resume[] = { KHOME_ACT_GRAB,
                                          KHOME_ACT_DRAW_HOME };
    static const KhomeAction close[] = { KHOME_ACT_CLOSE_BROWSER };

    expect_step_asleep("EDIT + goingToScreenSaver",
                       khome_step(at_state(KHOME_STATE_EDIT),
                                  KHOME_EV_POWER_GOING_TO_SS),
                       KHOME_STATE_BROWSER, 1, wake_edit, 2);
    expect_step_asleep("EDIT + outOfScreenSaver",
                       khome_step(at_state(KHOME_STATE_EDIT),
                                  KHOME_EV_POWER_OUT_OF_SS),
                       KHOME_STATE_BROWSER, 0, wake_edit, 2);
    expect_step_asleep("asleep EDIT + browser-exit",
                       khome_step(asleep_at(KHOME_STATE_EDIT),
                                  KHOME_EV_BROWSER_EXIT),
                       KHOME_STATE_HOME, 1, grab, 1);
    expect_step_asleep("asleep EDIT + browser-close",
                       khome_step(asleep_at(KHOME_STATE_EDIT),
                                  KHOME_EV_BROWSER_CLOSE),
                       KHOME_STATE_HOME, 1, close, 1);
    expect_step("EDIT + browser-exit",
                khome_step(at_state(KHOME_STATE_EDIT), KHOME_EV_BROWSER_EXIT),
                KHOME_STATE_HOME, resume, 2);
    expect_step("EDIT + browser-close",
                khome_step(at_state(KHOME_STATE_EDIT), KHOME_EV_BROWSER_CLOSE),
                KHOME_STATE_HOME, close, 1);
}

static void test_settings_opens_only_from_home(void)
{
    static const KhomeAction draw_settings[] = { KHOME_ACT_DRAW_SETTINGS };
    static const KhomeAction home[] = { KHOME_ACT_DRAW_HOME };
    static const KhomeState others[] = { KHOME_STATE_BROWSER, KHOME_STATE_SHEET,
                                         KHOME_STATE_EDIT, KHOME_STATE_KOREADER,
                                         KHOME_STATE_SETTINGS,
                                         KHOME_STATE_SETTINGS_CONFIRM };
    static const KhomeState non_settings[] = {
        KHOME_STATE_HOME,     KHOME_STATE_BROWSER, KHOME_STATE_SHEET,
        KHOME_STATE_EDIT,     KHOME_STATE_KOREADER,
        KHOME_STATE_SETTINGS_CONFIRM
    };
    char label[96];
    size_t i;

    expect_step("HOME + tap-settings",
                khome_step(at_state(KHOME_STATE_HOME), KHOME_EV_TAP_SETTINGS),
                KHOME_STATE_SETTINGS, draw_settings, 1);
    expect_step("SETTINGS + settings-back",
                khome_step(at_state(KHOME_STATE_SETTINGS),
                           KHOME_EV_SETTINGS_BACK),
                KHOME_STATE_HOME, home, 1);
    for (i = 0; i < sizeof(others) / sizeof(others[0]); i++) {
        snprintf(label, sizeof(label), "%s + tap-settings",
                 khome_state_name(others[i]));
        expect_step(label,
                    khome_step(at_state(others[i]), KHOME_EV_TAP_SETTINGS),
                    others[i], NULL, 0);
    }
    for (i = 0; i < sizeof(non_settings) / sizeof(non_settings[0]); i++) {
        snprintf(label, sizeof(label), "%s + settings-back",
                 khome_state_name(non_settings[i]));
        expect_step(label,
                    khome_step(at_state(non_settings[i]),
                               KHOME_EV_SETTINGS_BACK),
                    non_settings[i], NULL, 0);
    }
}

static void test_settings_confirms_open_from_settings(void)
{
    static const KhomeAction draw_confirm[] = { KHOME_ACT_DRAW_CONFIRM };
    static const KhomeState others[] = { KHOME_STATE_HOME,    KHOME_STATE_BROWSER,
                                         KHOME_STATE_KOREADER, KHOME_STATE_SHEET,
                                         KHOME_STATE_EDIT,
                                         KHOME_STATE_SETTINGS_CONFIRM };
    KhomeStep step;
    char label[96];
    size_t i;

    step = khome_step(at_state(KHOME_STATE_SETTINGS), KHOME_EV_SETTINGS_REBOOT);
    expect_step("SETTINGS + settings-reboot", step, KHOME_STATE_SETTINGS_CONFIRM,
                draw_confirm, 1);
    expect_confirm("SETTINGS + settings-reboot", step, KHOME_CONFIRM_REBOOT);

    step = khome_step(at_state(KHOME_STATE_SETTINGS), KHOME_EV_SETTINGS_STOCK);
    expect_step("SETTINGS + settings-stock", step, KHOME_STATE_SETTINGS_CONFIRM,
                draw_confirm, 1);
    expect_confirm("SETTINGS + settings-stock", step, KHOME_CONFIRM_STOCK);

    for (i = 0; i < sizeof(others) / sizeof(others[0]); i++) {
        snprintf(label, sizeof(label), "%s + settings-reboot",
                 khome_state_name(others[i]));
        expect_step(label,
                    khome_step(at_state(others[i]), KHOME_EV_SETTINGS_REBOOT),
                    others[i], NULL, 0);
        snprintf(label, sizeof(label), "%s + settings-stock",
                 khome_state_name(others[i]));
        expect_step(label,
                    khome_step(at_state(others[i]), KHOME_EV_SETTINGS_STOCK),
                    others[i], NULL, 0);
    }
}

static void test_settings_charge_limit_toggle(void)
{
    static const KhomeAction toggle[] = { KHOME_ACT_CHARGE_LIMIT_TOGGLE };
    static const KhomeState others[] = {
        KHOME_STATE_HOME,     KHOME_STATE_BROWSER, KHOME_STATE_SHEET,
        KHOME_STATE_EDIT,     KHOME_STATE_KOREADER,
        KHOME_STATE_SETTINGS_CONFIRM
    };
    char label[96];
    size_t i;

    KH_CHECK_MSG(strcmp(khome_event_name(KHOME_EV_SETTINGS_CHARGE_LIMIT),
                        "settings-charge-limit") == 0,
                 "event name: expected \"settings-charge-limit\", got \"%s\"",
                 khome_event_name(KHOME_EV_SETTINGS_CHARGE_LIMIT));
    KH_CHECK_MSG(strcmp(khome_action_name(KHOME_ACT_CHARGE_LIMIT_TOGGLE),
                        "charge-limit-toggle") == 0,
                 "action name: expected \"charge-limit-toggle\", got \"%s\"",
                 khome_action_name(KHOME_ACT_CHARGE_LIMIT_TOGGLE));

    expect_step("SETTINGS + settings-charge-limit",
                khome_step(at_state(KHOME_STATE_SETTINGS),
                           KHOME_EV_SETTINGS_CHARGE_LIMIT),
                KHOME_STATE_SETTINGS, toggle, 1);
    for (i = 0; i < sizeof(others) / sizeof(others[0]); i++) {
        snprintf(label, sizeof(label), "%s + settings-charge-limit",
                 khome_state_name(others[i]));
        expect_step(label,
                    khome_step(at_state(others[i]),
                               KHOME_EV_SETTINGS_CHARGE_LIMIT),
                    others[i], NULL, 0);
    }
}

static void test_confirm_cancel_returns_to_settings(void)
{
    static const KhomeAction draw_settings[] = { KHOME_ACT_DRAW_SETTINGS };
    static const KhomeState others[] = { KHOME_STATE_HOME, KHOME_STATE_BROWSER,
                                         KHOME_STATE_KOREADER,
                                         KHOME_STATE_SETTINGS };
    KhomeStep step;
    char label[96];
    size_t i;

    step = khome_step(at_confirm(KHOME_CONFIRM_REBOOT), KHOME_EV_CONFIRM_CANCEL);
    expect_step("REBOOT + confirm-cancel", step, KHOME_STATE_SETTINGS,
                draw_settings, 1);
    expect_confirm("REBOOT + confirm-cancel", step, KHOME_CONFIRM_NONE);

    step = khome_step(at_confirm(KHOME_CONFIRM_STOCK), KHOME_EV_CONFIRM_CANCEL);
    expect_step("STOCK + confirm-cancel", step, KHOME_STATE_SETTINGS,
                draw_settings, 1);
    expect_confirm("STOCK + confirm-cancel", step, KHOME_CONFIRM_NONE);

    for (i = 0; i < sizeof(others) / sizeof(others[0]); i++) {
        snprintf(label, sizeof(label), "%s + confirm-cancel",
                 khome_state_name(others[i]));
        expect_step(label,
                    khome_step(at_state(others[i]), KHOME_EV_CONFIRM_CANCEL),
                    others[i], NULL, 0);
        expect_confirm(label,
                       khome_step(at_state(others[i]), KHOME_EV_CONFIRM_CANCEL),
                       KHOME_CONFIRM_NONE);
    }
}

static void test_confirm_actions_are_gated_by_dialog(void)
{
    static const KhomeAction reboot_home[] = { KHOME_ACT_QUIT_REBOOT_HOME };
    static const KhomeAction reboot_kindle[] = { KHOME_ACT_QUIT_REBOOT_KINDLE };
    static const KhomeAction quit[] = { KHOME_ACT_QUIT };
    static const KhomeState others[] = { KHOME_STATE_HOME, KHOME_STATE_BROWSER,
                                         KHOME_STATE_KOREADER,
                                         KHOME_STATE_SETTINGS };
    KhomeStep step;
    char label[96];
    size_t i;

    step = khome_step(at_confirm(KHOME_CONFIRM_REBOOT),
                      KHOME_EV_CONFIRM_REBOOT_HOME);
    expect_step("REBOOT + confirm-reboot-home", step,
                KHOME_STATE_SETTINGS_CONFIRM, reboot_home, 1);
    expect_confirm("REBOOT + confirm-reboot-home", step, KHOME_CONFIRM_REBOOT);

    step = khome_step(at_confirm(KHOME_CONFIRM_REBOOT),
                      KHOME_EV_CONFIRM_REBOOT_KINDLE);
    expect_step("REBOOT + confirm-reboot-kindle", step,
                KHOME_STATE_SETTINGS_CONFIRM, reboot_kindle, 1);
    expect_confirm("REBOOT + confirm-reboot-kindle", step,
                   KHOME_CONFIRM_REBOOT);

    step = khome_step(at_confirm(KHOME_CONFIRM_STOCK),
                      KHOME_EV_CONFIRM_STOCK_BACK);
    expect_step("STOCK + confirm-stock-back", step, KHOME_STATE_SETTINGS_CONFIRM,
                quit, 1);
    expect_confirm("STOCK + confirm-stock-back", step, KHOME_CONFIRM_STOCK);

    expect_step("REBOOT + confirm-stock-back",
                khome_step(at_confirm(KHOME_CONFIRM_REBOOT),
                           KHOME_EV_CONFIRM_STOCK_BACK),
                KHOME_STATE_SETTINGS_CONFIRM, NULL, 0);
    expect_step("STOCK + confirm-reboot-home",
                khome_step(at_confirm(KHOME_CONFIRM_STOCK),
                           KHOME_EV_CONFIRM_REBOOT_HOME),
                KHOME_STATE_SETTINGS_CONFIRM, NULL, 0);
    expect_step("STOCK + confirm-reboot-kindle",
                khome_step(at_confirm(KHOME_CONFIRM_STOCK),
                           KHOME_EV_CONFIRM_REBOOT_KINDLE),
                KHOME_STATE_SETTINGS_CONFIRM, NULL, 0);

    for (i = 0; i < sizeof(others) / sizeof(others[0]); i++) {
        snprintf(label, sizeof(label), "%s + confirm-reboot-home",
                 khome_state_name(others[i]));
        expect_step(label,
                    khome_step(at_state(others[i]),
                               KHOME_EV_CONFIRM_REBOOT_HOME),
                    others[i], NULL, 0);
        snprintf(label, sizeof(label), "%s + confirm-reboot-kindle",
                 khome_state_name(others[i]));
        expect_step(label,
                    khome_step(at_state(others[i]),
                               KHOME_EV_CONFIRM_REBOOT_KINDLE),
                    others[i], NULL, 0);
        snprintf(label, sizeof(label), "%s + confirm-stock-back",
                 khome_state_name(others[i]));
        expect_step(label,
                    khome_step(at_state(others[i]),
                               KHOME_EV_CONFIRM_STOCK_BACK),
                    others[i], NULL, 0);
    }
}

static KhomeMachine asleep_confirm(KhomeConfirm confirm)
{
    KhomeMachine m = asleep_at(KHOME_STATE_SETTINGS_CONFIRM);

    m.confirm = confirm;
    return m;
}

static void test_settings_sleep_keeps_dialog(void)
{
    KhomeStep step;

    step = khome_step(at_state(KHOME_STATE_SETTINGS), KHOME_EV_POWER_GOING_TO_SS);
    expect_step_asleep_confirm("SETTINGS + goingToScreenSaver", step,
                               KHOME_STATE_SETTINGS, 1, KHOME_CONFIRM_NONE,
                               NULL, 0);

    step = khome_step(at_confirm(KHOME_CONFIRM_REBOOT),
                      KHOME_EV_POWER_GOING_TO_SS);
    expect_step_asleep_confirm("REBOOT + goingToScreenSaver", step,
                               KHOME_STATE_SETTINGS_CONFIRM, 1,
                               KHOME_CONFIRM_REBOOT, NULL, 0);

    step = khome_step(at_confirm(KHOME_CONFIRM_STOCK),
                      KHOME_EV_POWER_GOING_TO_SS);
    expect_step_asleep_confirm("STOCK + goingToScreenSaver", step,
                               KHOME_STATE_SETTINGS_CONFIRM, 1,
                               KHOME_CONFIRM_STOCK, NULL, 0);
}

static void test_settings_wake_redraws_the_same_screen(void)
{
    static const KhomeAction schedule[] = { KHOME_ACT_SCHEDULE_REDRAW };
    static const KhomeAction draw_settings[] = { KHOME_ACT_DRAW_SETTINGS };
    static const KhomeAction draw_confirm[] = { KHOME_ACT_DRAW_CONFIRM };
    KhomeStep step;

    step = khome_step(asleep_at(KHOME_STATE_SETTINGS), KHOME_EV_POWER_OUT_OF_SS);
    expect_step_asleep_confirm("asleep SETTINGS + outOfScreenSaver", step,
                               KHOME_STATE_SETTINGS, 0, KHOME_CONFIRM_NONE,
                               schedule, 1);
    step = khome_step(after_step(step), KHOME_EV_DEFER_REDRAW);
    expect_step_asleep_confirm("asleep SETTINGS + defer-redraw", step,
                               KHOME_STATE_SETTINGS, 0, KHOME_CONFIRM_NONE,
                               draw_settings, 1);

    step = khome_step(asleep_confirm(KHOME_CONFIRM_REBOOT),
                      KHOME_EV_POWER_OUT_OF_SS);
    expect_step_asleep_confirm("asleep REBOOT + outOfScreenSaver", step,
                               KHOME_STATE_SETTINGS_CONFIRM, 0,
                               KHOME_CONFIRM_REBOOT, schedule, 1);
    step = khome_step(after_step(step), KHOME_EV_DEFER_REDRAW);
    expect_step_asleep_confirm("asleep REBOOT + defer-redraw", step,
                               KHOME_STATE_SETTINGS_CONFIRM, 0,
                               KHOME_CONFIRM_REBOOT, draw_confirm, 1);

    step = khome_step(asleep_confirm(KHOME_CONFIRM_STOCK),
                      KHOME_EV_POWER_OUT_OF_SS);
    expect_step_asleep_confirm("asleep STOCK + outOfScreenSaver", step,
                               KHOME_STATE_SETTINGS_CONFIRM, 0,
                               KHOME_CONFIRM_STOCK, schedule, 1);
    step = khome_step(after_step(step), KHOME_EV_DEFER_REDRAW);
    expect_step_asleep_confirm("asleep STOCK + defer-redraw", step,
                               KHOME_STATE_SETTINGS_CONFIRM, 0,
                               KHOME_CONFIRM_STOCK, draw_confirm, 1);
}

static void test_sleeping_settings_events_are_dropped(void)
{
    static const KhomeEvent events[] = {
        KHOME_EV_TAP_SETTINGS,        KHOME_EV_SETTINGS_BACK,
        KHOME_EV_SETTINGS_REBOOT,     KHOME_EV_SETTINGS_STOCK,
        KHOME_EV_CONFIRM_CANCEL,      KHOME_EV_CONFIRM_REBOOT_HOME,
        KHOME_EV_CONFIRM_REBOOT_KINDLE, KHOME_EV_CONFIRM_STOCK_BACK,
        KHOME_EV_MINUTE,              KHOME_EV_STATUS_CHANGED,
        KHOME_EV_DEFER_REDRAW,        KHOME_EV_DAY_CHANGED
    };
    static const KhomeConfirm confirms[] = { KHOME_CONFIRM_NONE,
                                             KHOME_CONFIRM_REBOOT,
                                             KHOME_CONFIRM_STOCK };
    char label[96];
    size_t i;
    size_t j;

    for (i = 0; i < sizeof(confirms) / sizeof(confirms[0]); i++) {
        for (j = 0; j < sizeof(events) / sizeof(events[0]); j++) {
            KhomeStep got = khome_step(asleep_confirm(confirms[i]),
                                       events[j]);

            snprintf(label, sizeof(label), "asleep confirm %d + %s",
                     (int)confirms[i], khome_event_name(events[j]));
            expect_step_asleep_confirm(label, got,
                                       KHOME_STATE_SETTINGS_CONFIRM, 1,
                                       confirms[i], NULL, 0);
        }
    }
    for (j = 0; j < sizeof(events) / sizeof(events[0]); j++) {
        snprintf(label, sizeof(label), "asleep SETTINGS + %s",
                 khome_event_name(events[j]));
        expect_step_asleep_confirm(label,
                                   khome_step(asleep_at(KHOME_STATE_SETTINGS),
                                              events[j]),
                                   KHOME_STATE_SETTINGS, 1, KHOME_CONFIRM_NONE,
                                   NULL, 0);
    }
}

static void test_settings_timers(void)
{
    static const KhomeAction draw_settings[] = { KHOME_ACT_DRAW_SETTINGS };
    static const KhomeAction draw_confirm[] = { KHOME_ACT_DRAW_CONFIRM };
    static const KhomeEvent redraw_events[] = { KHOME_EV_STATUS_CHANGED,
                                                KHOME_EV_DAY_CHANGED };
    KhomeStep step;
    char label[96];
    size_t i;

    expect_step("SETTINGS + minute",
                khome_step(at_state(KHOME_STATE_SETTINGS), KHOME_EV_MINUTE),
                KHOME_STATE_SETTINGS, NULL, 0);
    expect_step("REBOOT + minute",
                khome_step(at_confirm(KHOME_CONFIRM_REBOOT), KHOME_EV_MINUTE),
                KHOME_STATE_SETTINGS_CONFIRM, NULL, 0);
    expect_step("STOCK + minute",
                khome_step(at_confirm(KHOME_CONFIRM_STOCK), KHOME_EV_MINUTE),
                KHOME_STATE_SETTINGS_CONFIRM, NULL, 0);

    for (i = 0; i < sizeof(redraw_events) / sizeof(redraw_events[0]); i++) {
        snprintf(label, sizeof(label), "SETTINGS + %s",
                 khome_event_name(redraw_events[i]));
        step = khome_step(at_state(KHOME_STATE_SETTINGS), redraw_events[i]);
        expect_step(label, step, KHOME_STATE_SETTINGS, draw_settings, 1);
        expect_confirm(label, step, KHOME_CONFIRM_NONE);

        snprintf(label, sizeof(label), "REBOOT + %s",
                 khome_event_name(redraw_events[i]));
        step = khome_step(at_confirm(KHOME_CONFIRM_REBOOT), redraw_events[i]);
        expect_step(label, step, KHOME_STATE_SETTINGS_CONFIRM, draw_confirm, 1);
        expect_confirm(label, step, KHOME_CONFIRM_REBOOT);

        snprintf(label, sizeof(label), "STOCK + %s",
                 khome_event_name(redraw_events[i]));
        step = khome_step(at_confirm(KHOME_CONFIRM_STOCK), redraw_events[i]);
        expect_step(label, step, KHOME_STATE_SETTINGS_CONFIRM, draw_confirm, 1);
        expect_confirm(label, step, KHOME_CONFIRM_STOCK);
    }
}

static char *read_first_file(const char *const *paths, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        FILE *f = fopen(paths[i], "rb");
        long size;
        char *text;

        if (f == NULL) {
            continue;
        }
        if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 ||
            fseek(f, 0, SEEK_SET) != 0) {
            fclose(f);
            continue;
        }
        text = malloc((size_t)size + 1);
        if (text == NULL) {
            fclose(f);
            return NULL;
        }
        if (fread(text, 1, (size_t)size, f) != (size_t)size) {
            free(text);
            fclose(f);
            continue;
        }
        text[size] = 0;
        fclose(f);
        return text;
    }
    return NULL;
}

static void test_exit_codes_match_khome_script(void)
{
    static const char *const paths[] = { "../kindle/khome/khome.sh",
                                         "kindle/khome/khome.sh" };
    char restart_branch[64];
    char reboot_branch[64];
    char *script;

    KH_CHECK_MSG(KHOME_EXIT_REBOOT_HOME == 75,
                 "state.h: expected KHOME_EXIT_REBOOT_HOME 75, got %d",
                 KHOME_EXIT_REBOOT_HOME);
    KH_CHECK_MSG(KHOME_EXIT_REBOOT_KINDLE == 76,
                 "state.h: expected KHOME_EXIT_REBOOT_KINDLE 76, got %d",
                 KHOME_EXIT_REBOOT_KINDLE);

    script = read_first_file(paths, (int)(sizeof(paths) / sizeof(paths[0])));
    KH_CHECK_MSG(script != NULL,
                 "cannot read kindle/khome/khome.sh from the test directory");
    if (script == NULL) {
        return;
    }
    snprintf(restart_branch, sizeof(restart_branch), "[ \"$rc\" -eq %d ]",
             KHOME_EXIT_REBOOT_HOME);
    snprintf(reboot_branch, sizeof(reboot_branch), "[ \"$rc\" -eq %d ]",
             KHOME_EXIT_REBOOT_KINDLE);
    KH_CHECK_MSG(strstr(script, restart_branch) != NULL,
                 "khome.sh: missing the home-restart branch \"%s\"",
                 restart_branch);
    KH_CHECK_MSG(strstr(script, reboot_branch) != NULL,
                 "khome.sh: missing the Kindle-reboot branch \"%s\"",
                 reboot_branch);
    free(script);
}

void kh_run_state_tests(void)
{
    printf("\n[state]\n");
    kh_run_test("state.home_app_taps_release", test_home_app_taps_release);
    kh_run_test("state.out_of_screen_saver_redraws_only_when_owning",
                test_out_of_screen_saver_redraws_only_when_owning);
    kh_run_test("state.start_owns_home", test_start_owns_home);
    kh_run_test("state.browser_pull_opens_sheet",
                test_browser_pull_opens_sheet);
    kh_run_test("state.sheet_buttons", test_sheet_buttons);
    kh_run_test("state.sheet_close_variants", test_sheet_close_variants);
    kh_run_test("state.sheet_wake_and_browser_end",
                test_sheet_wake_and_browser_end);
    kh_run_test("state.browser_exit_returns_home",
                test_browser_exit_returns_home);
    kh_run_test("state.sheet_events_outside_sheet_are_inert",
                test_sheet_events_outside_sheet_are_inert);
    kh_run_test("state.sheet_edit_enters_editor",
                test_sheet_edit_enters_editor);
    kh_run_test("state.edit_cancel_returns_to_sheet",
                test_edit_cancel_returns_to_sheet);
    kh_run_test("state.edit_open_navigates_to_browser",
                test_edit_open_navigates_to_browser);
    kh_run_test("state.edit_ignores_unrelated_events",
                test_edit_ignores_unrelated_events);
    kh_run_test("state.edit_sleep_and_browser_end",
                test_edit_sleep_and_browser_end);
    kh_run_test("state.going_to_sleep_keeps_owning_state",
                test_going_to_sleep_keeps_owning_state);
    kh_run_test("state.sleeping_ignores_background_events",
                test_sleeping_ignores_background_events);
    kh_run_test("state.sleeping_app_exits_do_not_draw",
                test_sleeping_app_exits_do_not_draw);
    kh_run_test("state.sleeping_quit_still_quits",
                test_sleeping_quit_still_quits);
    kh_run_test("state.out_of_screen_saver_wakes",
                test_out_of_screen_saver_wakes);
    kh_run_test("state.sleep_and_wake_sequence_draws_once",
                test_sleep_and_wake_sequence_draws_once);
    kh_run_test("state.day_changed_redraws_only_home",
                test_day_changed_redraws_only_home);
    kh_run_test("state.awake_timers_keep_their_actions",
                test_awake_timers_keep_their_actions);
    kh_run_test("state.settings_opens_only_from_home",
                test_settings_opens_only_from_home);
    kh_run_test("state.settings_confirms_open_from_settings",
                test_settings_confirms_open_from_settings);
    kh_run_test("state.settings_charge_limit_toggle",
                test_settings_charge_limit_toggle);
    kh_run_test("state.confirm_cancel_returns_to_settings",
                test_confirm_cancel_returns_to_settings);
    kh_run_test("state.confirm_actions_are_gated_by_dialog",
                test_confirm_actions_are_gated_by_dialog);
    kh_run_test("state.settings_sleep_keeps_dialog",
                test_settings_sleep_keeps_dialog);
    kh_run_test("state.settings_wake_redraws_the_same_screen",
                test_settings_wake_redraws_the_same_screen);
    kh_run_test("state.sleeping_settings_events_are_dropped",
                test_sleeping_settings_events_are_dropped);
    kh_run_test("state.settings_timers", test_settings_timers);
    kh_run_test("state.exit_codes_match_khome_script",
                test_exit_codes_match_khome_script);
}
