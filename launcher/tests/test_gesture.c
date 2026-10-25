#include "suites.h"

#include <stddef.h>

#include "gesture.h"
#include "test_harness.h"

/* Gesture rules from src/gesture.h: a tap is a press released within 600 ms
 * and 30 px of where it started.  Tests drive evdev-style event sequences
 * through the public gesture_handle() API. */

static InputEvent abs_event(int code, int value, int64_t time_ms)
{
    InputEvent ev;
    ev.type = GESTURE_EV_ABS;
    ev.code = code;
    ev.value = value;
    ev.time_ms = time_ms;
    return ev;
}

static InputEvent key_event(int code, int value, int64_t time_ms)
{
    InputEvent ev;
    ev.type = GESTURE_EV_KEY;
    ev.code = code;
    ev.value = value;
    ev.time_ms = time_ms;
    return ev;
}

static InputEvent syn_event(int64_t time_ms)
{
    InputEvent ev;
    ev.type = GESTURE_EV_SYN;
    ev.code = GESTURE_SYN_REPORT;
    ev.value = 0;
    ev.time_ms = time_ms;
    return ev;
}

static int feed(Gesture *g, const InputEvent *events, size_t count, Tap *tap)
{
    size_t i;
    int tapped = 0;

    for (i = 0; i < count; i++) {
        if (gesture_handle(g, &events[i], tap)) {
            tapped = 1;
        }
    }
    return tapped;
}

static GestureResult feed_result(Gesture *g, const InputEvent *events,
                                 size_t count, Tap *tap)
{
    GestureResult result = GESTURE_RESULT_NONE;
    size_t i;

    for (i = 0; i < count; i++) {
        GestureResult step = gesture_handle_result(g, &events[i], tap);
        if (step != GESTURE_RESULT_NONE) {
            result = step;
        }
    }
    return result;
}

/* Press at (start_x, start_y), move to (end_x, end_y) with a SYN_REPORT, then
 * release.  Returns the gesture result, and leaves tap as gesture_end filled
 * it. */
static GestureResult pull_from(int start_x, int start_y, int end_x, int end_y,
                               Tap *tap)
{
    Gesture g;
    InputEvent seq[] = {
        abs_event(GESTURE_ABS_MT_TRACKING_ID, 1, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, start_x, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, start_y, 1000),
        syn_event(1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, end_x, 1200),
        abs_event(GESTURE_ABS_MT_POSITION_Y, end_y, 1200),
        syn_event(1200),
        abs_event(GESTURE_ABS_MT_TRACKING_ID, -1, 1300)
    };

    gesture_init(&g);
    return feed_result(&g, seq, sizeof(seq) / sizeof(seq[0]), tap);
}

/* Drag from (start_x, start_y) to (end_x, end_y), then release, with the move
 * and release at the given offsets from the press. */
static GestureResult drag_from(int start_x, int start_y, int end_x, int end_y,
                               int64_t move_ms, int64_t release_ms, Tap *tap)
{
    Gesture g;
    InputEvent seq[] = {
        abs_event(GESTURE_ABS_MT_TRACKING_ID, 1, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, start_x, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, start_y, 1000),
        syn_event(1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, end_x, 1000 + move_ms),
        abs_event(GESTURE_ABS_MT_POSITION_Y, end_y, 1000 + move_ms),
        syn_event(1000 + move_ms),
        abs_event(GESTURE_ABS_MT_TRACKING_ID, -1, 1000 + release_ms)
    };

    gesture_init(&g);
    return feed_result(&g, seq, sizeof(seq) / sizeof(seq[0]), tap);
}

static void expect_tap(const char *label, int got, const Tap *tap, int x,
                       int y)
{
    KH_CHECK_MSG(got == 1, "%s: expected a tap, got none", label);
    if (got == 1) {
        KH_CHECK_MSG(tap->x == x && tap->y == y,
                     "%s: expected tap at (%d, %d), got (%d, %d)", label, x,
                     y, tap->x, tap->y);
    }
}

static void test_mt_tap_uses_start_point(void)
{
    Gesture g;
    Tap tap = { -1, -1 };
    InputEvent seq[] = {
        abs_event(GESTURE_ABS_MT_TRACKING_ID, 7, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 100, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 200, 1000),
        syn_event(1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 110, 1080),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 215, 1080),
        syn_event(1080),
        abs_event(GESTURE_ABS_MT_TRACKING_ID, -1, 1120)
    };

    gesture_init(&g);
    expect_tap("MT tap", feed(&g, seq, sizeof(seq) / sizeof(seq[0]), &tap),
               &tap, 100, 200);
}

static void test_moved_past_threshold_is_not_a_tap(void)
{
    Gesture g;
    Tap tap = { -1, -1 };
    InputEvent x_seq[] = {
        abs_event(GESTURE_ABS_MT_TRACKING_ID, 1, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 100, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 200, 1000),
        syn_event(1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 131, 1050),
        syn_event(1050),
        abs_event(GESTURE_ABS_MT_TRACKING_ID, -1, 1100)
    };
    InputEvent y_seq[] = {
        abs_event(GESTURE_ABS_MT_TRACKING_ID, 1, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 100, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 200, 1000),
        syn_event(1000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 231, 1050),
        syn_event(1050),
        abs_event(GESTURE_ABS_MT_TRACKING_ID, -1, 1100)
    };
    InputEvent neg_x_seq[] = {
        abs_event(GESTURE_ABS_MT_TRACKING_ID, 1, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 100, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 200, 1000),
        syn_event(1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 69, 1050),
        syn_event(1050),
        abs_event(GESTURE_ABS_MT_TRACKING_ID, -1, 1100)
    };

    gesture_init(&g);
    KH_CHECK_MSG(!feed(&g, x_seq, sizeof(x_seq) / sizeof(x_seq[0]), &tap),
                 "31 px horizontal movement must not be a tap");
    gesture_init(&g);
    KH_CHECK_MSG(!feed(&g, y_seq, sizeof(y_seq) / sizeof(y_seq[0]), &tap),
                 "31 px vertical movement must not be a tap");
    gesture_init(&g);
    KH_CHECK_MSG(!feed(&g, neg_x_seq,
                       sizeof(neg_x_seq) / sizeof(neg_x_seq[0]), &tap),
                 "31 px movement in the negative direction must not be a tap");
}

static void test_movement_at_threshold_is_a_tap(void)
{
    Gesture g;
    Tap tap = { -1, -1 };
    InputEvent seq[] = {
        abs_event(GESTURE_ABS_MT_TRACKING_ID, 1, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 100, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 200, 1000),
        syn_event(1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 130, 1050),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 230, 1050),
        syn_event(1050),
        abs_event(GESTURE_ABS_MT_TRACKING_ID, -1, 1100)
    };

    gesture_init(&g);
    expect_tap("30 px movement",
               feed(&g, seq, sizeof(seq) / sizeof(seq[0]), &tap), &tap, 100,
               200);
}

static void test_press_duration_boundary(void)
{
    Gesture g;
    Tap tap = { -1, -1 };
    InputEvent at_limit[] = {
        abs_event(GESTURE_ABS_MT_TRACKING_ID, 1, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 100, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 200, 1000),
        syn_event(1000),
        abs_event(GESTURE_ABS_MT_TRACKING_ID, -1, 1600)
    };
    InputEvent past_limit[] = {
        abs_event(GESTURE_ABS_MT_TRACKING_ID, 1, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 100, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 200, 1000),
        syn_event(1000),
        abs_event(GESTURE_ABS_MT_TRACKING_ID, -1, 1601)
    };

    gesture_init(&g);
    expect_tap("600 ms press",
               feed(&g, at_limit, sizeof(at_limit) / sizeof(at_limit[0]),
                    &tap),
               &tap, 100, 200);
    gesture_init(&g);
    KH_CHECK_MSG(!feed(&g, past_limit,
                       sizeof(past_limit) / sizeof(past_limit[0]), &tap),
                 "601 ms press must not be a tap");
}

static void test_btn_touch_sequence_without_tracking_id(void)
{
    Gesture g;
    Tap tap = { -1, -1 };
    InputEvent seq[] = {
        key_event(GESTURE_BTN_TOUCH, 1, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 300, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 400, 1000),
        syn_event(1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 310, 1050),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 410, 1050),
        syn_event(1050),
        key_event(GESTURE_BTN_TOUCH, 0, 1100)
    };

    gesture_init(&g);
    expect_tap("BTN_TOUCH tap",
               feed(&g, seq, sizeof(seq) / sizeof(seq[0]), &tap), &tap, 300,
               400);
}

static void test_btn_touch_only_events(void)
{
    Gesture g;
    Tap tap = { -1, -1 };
    InputEvent seq[] = {
        key_event(GESTURE_BTN_TOUCH, 1, 1000),
        key_event(GESTURE_BTN_TOUCH, 0, 1050)
    };

    gesture_init(&g);
    KH_CHECK_MSG(feed(&g, seq, sizeof(seq) / sizeof(seq[0]), &tap) == 1,
                 "BTN_TOUCH press and release without any ABS events must "
                 "still complete a gesture");
}

static void test_spurious_releases_are_ignored(void)
{
    Gesture g;
    Tap tap = { -1, -1 };
    InputEvent tracking_end[] = {
        abs_event(GESTURE_ABS_MT_TRACKING_ID, -1, 1000)
    };
    InputEvent btn_up[] = {
        key_event(GESTURE_BTN_TOUCH, 0, 1000)
    };

    gesture_init(&g);
    KH_CHECK_MSG(!feed(&g, tracking_end,
                       sizeof(tracking_end) / sizeof(tracking_end[0]), &tap),
                 "TRACKING_ID -1 without a press must not make a tap");
    gesture_init(&g);
    KH_CHECK_MSG(!feed(&g, btn_up, sizeof(btn_up) / sizeof(btn_up[0]), &tap),
                 "BTN_TOUCH release without a press must not make a tap");
}

static void test_top_edge_pull_reports_sheet_pull(void)
{
    Tap tap = { -1, -1 };

    KH_CHECK_MSG(pull_from(300, 10, 300, 250, &tap) ==
                     GESTURE_RESULT_SHEET_PULL,
                 "a 240 px downward pull from y=10 must open the sheet");
    KH_CHECK_MSG(tap.x == -1 && tap.y == -1,
                 "a sheet pull must not fill the tap coordinates");
}

static void test_top_edge_band_boundary(void)
{
    Tap tap = { -1, -1 };

    KH_CHECK_MSG(pull_from(300, GESTURE_SHEET_EDGE_PX, 300,
                           GESTURE_SHEET_EDGE_PX + GESTURE_SHEET_PULL_PX,
                           &tap) == GESTURE_RESULT_SHEET_PULL,
                 "a pull that starts at the edge must open the sheet");
    KH_CHECK_MSG(pull_from(300, GESTURE_SHEET_EDGE_PX + 1, 300,
                           GESTURE_SHEET_EDGE_PX + 1 + GESTURE_SHEET_PULL_PX,
                           &tap) == GESTURE_RESULT_NONE,
                 "a pull that starts below the edge must not open the sheet");
}

/* The top band is 96 design px deep: a press at y=96 can still start a pull,
 * one at y=97 cannot.  These are device gestures, so the numbers are literal
 * rather than the constant the rule happens to use. */
static void test_top_edge_band_covers_96_not_97(void)
{
    Tap tap = { -1, -1 };

    KH_CHECK_MSG(pull_from(300, 0, 300, GESTURE_SHEET_PULL_PX, &tap) ==
                     GESTURE_RESULT_SHEET_PULL,
                 "a 120 px pull from y=0 must open the sheet");
    KH_CHECK_MSG(pull_from(300, 96, 300, 96 + GESTURE_SHEET_PULL_PX, &tap) ==
                     GESTURE_RESULT_SHEET_PULL,
                 "a 120 px pull from y=96 must open the sheet");
    KH_CHECK_MSG(pull_from(300, 96, 300,
                           96 + GESTURE_SHEET_PULL_PX - 1, &tap) ==
                     GESTURE_RESULT_NONE,
                 "a 119 px pull from y=96 must not open the sheet");
    KH_CHECK_MSG(pull_from(300, 97, 300, 97 + GESTURE_SHEET_PULL_PX, &tap) ==
                     GESTURE_RESULT_NONE,
                 "a 120 px pull from y=97 must not open the sheet");
}

static void test_pull_distance_boundary(void)
{
    Tap tap = { -1, -1 };

    KH_CHECK_MSG(pull_from(300, 10, 300, 10 + GESTURE_SHEET_PULL_PX - 1,
                           &tap) == GESTURE_RESULT_NONE,
                 "a 119 px downward movement must not open the sheet");
    KH_CHECK_MSG(pull_from(300, 10, 300, 10 + GESTURE_SHEET_PULL_PX, &tap) ==
                     GESTURE_RESULT_SHEET_PULL,
                 "a 120 px downward movement must open the sheet");
}

static void test_sideways_movement_cancels_pull(void)
{
    Tap tap = { -1, -1 };

    KH_CHECK_MSG(pull_from(300, 10, 300 + 60, 10 + GESTURE_SHEET_PULL_PX,
                           &tap) == GESTURE_RESULT_SHEET_PULL,
                 "60 px sideways over 120 px down is still a pull");
    KH_CHECK_MSG(pull_from(300, 10, 300 - 60, 10 + GESTURE_SHEET_PULL_PX,
                           &tap) == GESTURE_RESULT_SHEET_PULL,
                 "60 px sideways in the other direction is still a pull");
    KH_CHECK_MSG(pull_from(300, 10, 300 + 61, 10 + GESTURE_SHEET_PULL_PX,
                           &tap) == GESTURE_RESULT_NONE,
                 "61 px sideways over 120 px down is too diagonal");
}

static void test_swipe_left_and_right(void)
{
    Tap tap = { -1, -1 };

    KH_CHECK_MSG(drag_from(600, 500, 600 - GESTURE_SWIPE_PX, 500, 200, 300,
                           &tap) == GESTURE_RESULT_SWIPE_LEFT,
                 "a 120 px drag to the left must be SWIPE_LEFT");
    KH_CHECK_MSG(drag_from(600, 500, 600 + GESTURE_SWIPE_PX, 500, 200, 300,
                           &tap) == GESTURE_RESULT_SWIPE_RIGHT,
                 "a 120 px drag to the right must be SWIPE_RIGHT");
    KH_CHECK_MSG(tap.x == -1 && tap.y == -1,
                 "a swipe must not fill the tap coordinates, got (%d, %d)",
                 tap.x, tap.y);
}

static void test_swipe_distance_boundary(void)
{
    Tap tap = { -1, -1 };

    KH_CHECK_MSG(drag_from(600, 500, 600 - (GESTURE_SWIPE_PX - 1), 500, 200,
                           300, &tap) == GESTURE_RESULT_NONE,
                 "119 px to the left must not swipe");
    KH_CHECK_MSG(drag_from(600, 500, 600 + GESTURE_SWIPE_PX - 1, 500, 200,
                           300, &tap) == GESTURE_RESULT_NONE,
                 "119 px to the right must not swipe");
    KH_CHECK_MSG(drag_from(600, 500, 600 - GESTURE_SWIPE_PX, 500, 200, 300,
                           &tap) == GESTURE_RESULT_SWIPE_LEFT,
                 "exactly 120 px to the left must swipe");
    KH_CHECK_MSG(drag_from(600, 500, 600 + GESTURE_SWIPE_PX, 500, 200, 300,
                           &tap) == GESTURE_RESULT_SWIPE_RIGHT,
                 "exactly 120 px to the right must swipe");
}

static void test_swipe_angle_boundary(void)
{
    Tap tap = { -1, -1 };

    KH_CHECK_MSG(drag_from(600, 500, 600 + GESTURE_SWIPE_PX,
                           500 + GESTURE_SWIPE_PX / 2, 200, 300,
                           &tap) == GESTURE_RESULT_SWIPE_RIGHT,
                 "|dy|*2 == |dx| must still swipe");
    KH_CHECK_MSG(drag_from(600, 500, 600 + GESTURE_SWIPE_PX,
                           500 - GESTURE_SWIPE_PX / 2, 200, 300,
                           &tap) == GESTURE_RESULT_SWIPE_RIGHT,
                 "|dy|*2 == |dx| in the other direction must still swipe");
    KH_CHECK_MSG(drag_from(600, 500, 600 + GESTURE_SWIPE_PX,
                           500 + GESTURE_SWIPE_PX / 2 + 1, 200, 300,
                           &tap) == GESTURE_RESULT_NONE,
                 "|dy|*2 just over |dx| must not swipe");
    KH_CHECK_MSG(drag_from(600, 500, 600 - GESTURE_SWIPE_PX,
                           500 + GESTURE_SWIPE_PX / 2, 200, 300,
                           &tap) == GESTURE_RESULT_SWIPE_LEFT,
                 "a diagonal left drag within the limit must swipe left");
}

static void test_swipe_is_not_a_tap_or_long_press(void)
{
    Tap tap = { -1, -1 };

    KH_CHECK_MSG(drag_from(600, 500, 600 + GESTURE_SWIPE_PX, 500, 50, 100,
                           &tap) == GESTURE_RESULT_SWIPE_RIGHT,
                 "a quick horizontal drag must be a swipe, not a tap");
    KH_CHECK_MSG(drag_from(600, 500, 600 - GESTURE_SWIPE_PX, 500, 650, 700,
                           &tap) == GESTURE_RESULT_SWIPE_LEFT,
                 "a slow horizontal drag must be a swipe, not a long press");
}

/* The top-edge pull is checked first, but a sideways movement in that band
 * cannot satisfy the pull's vertical rule. */
static void test_top_edge_swipe_is_not_a_pull(void)
{
    Tap tap = { -1, -1 };

    KH_CHECK_MSG(drag_from(600, 10, 600 + GESTURE_SWIPE_PX,
                           10 + GESTURE_SWIPE_PX / 2, 200, 300,
                           &tap) == GESTURE_RESULT_SWIPE_RIGHT,
                 "a horizontal drag from the top band must swipe, not pull");
    KH_CHECK_MSG(drag_from(600, GESTURE_SHEET_EDGE_PX,
                           600 - GESTURE_SWIPE_PX,
                           GESTURE_SHEET_EDGE_PX + GESTURE_SWIPE_PX / 2, 200,
                           300, &tap) == GESTURE_RESULT_SWIPE_LEFT,
                 "a horizontal drag from the edge must swipe, not pull");
    KH_CHECK_MSG(pull_from(600, 10, 600, 10 + GESTURE_SHEET_PULL_PX, &tap) ==
                     GESTURE_RESULT_SHEET_PULL,
                 "a straight downward drag from the top band must still pull");
    KH_CHECK_MSG(drag_from(600, 10, 600 + GESTURE_SWIPE_PX,
                           10 + GESTURE_SWIPE_PX, 200, 300,
                           &tap) == GESTURE_RESULT_NONE,
                 "a 45-degree drag from the top band is neither pull nor swipe");
}

static void test_vertical_drag_is_not_a_swipe(void)
{
    Tap tap = { -1, -1 };

    KH_CHECK_MSG(drag_from(600, 500, 600, 500 + GESTURE_SWIPE_PX, 200, 300,
                           &tap) == GESTURE_RESULT_NONE,
                 "a vertical drag away from the top band must not swipe");
    KH_CHECK_MSG(drag_from(600, 500, 600 + GESTURE_SWIPE_PX / 2, 500,
                           200, 300, &tap) == GESTURE_RESULT_NONE,
                 "a horizontal drag under the swipe distance must not swipe");
}

static void test_result_tap_keeps_coordinates(void)
{
    Gesture g;
    Tap tap = { -1, -1 };
    InputEvent seq[] = {
        key_event(GESTURE_BTN_TOUCH, 1, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 300, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 400, 1000),
        syn_event(1000),
        key_event(GESTURE_BTN_TOUCH, 0, 1100)
    };

    gesture_init(&g);
    KH_CHECK_MSG(feed_result(&g, seq, sizeof(seq) / sizeof(seq[0]), &tap) ==
                     GESTURE_RESULT_TAP,
                 "a short press must still be a tap");
    KH_CHECK_MSG(tap.x == 300 && tap.y == 400,
                 "a tap must report the pressed point, got (%d, %d)", tap.x,
                 tap.y);
}

static void test_handle_reports_taps_but_not_pulls(void)
{
    Gesture g;
    Tap tap = { -1, -1 };
    InputEvent tap_seq[] = {
        key_event(GESTURE_BTN_TOUCH, 1, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 300, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 400, 1000),
        syn_event(1000),
        key_event(GESTURE_BTN_TOUCH, 0, 1100)
    };
    InputEvent pull_seq[] = {
        abs_event(GESTURE_ABS_MT_TRACKING_ID, 1, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 300, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 10, 1000),
        syn_event(1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 300, 1200),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 250, 1200),
        syn_event(1200),
        abs_event(GESTURE_ABS_MT_TRACKING_ID, -1, 1300)
    };

    gesture_init(&g);
    KH_CHECK_MSG(feed(&g, tap_seq, sizeof(tap_seq) / sizeof(tap_seq[0]),
                      &tap) == 1,
                 "gesture_handle must still report a tap");
    gesture_init(&g);
    KH_CHECK_MSG(feed(&g, pull_seq, sizeof(pull_seq) / sizeof(pull_seq[0]),
                      &tap) == 0,
                 "gesture_handle must not report a sheet pull as a tap");
}

/* evdev drops ABS_MT_POSITION_X when it equals the value still held from the
 * last touch, so the press after a sheet pull that ended at x=380 arrives
 * without an X event.  The cancel keeps the position that event would have
 * carried. */
static void test_cancel_keeps_x_when_next_press_omits_it(void)
{
    Gesture g;
    Tap tap = { -1, -1 };
    InputEvent pull[] = {
        abs_event(GESTURE_ABS_MT_TRACKING_ID, 1, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 380, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 10, 1000),
        syn_event(1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 380, 1200),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 250, 1200),
        syn_event(1200),
        abs_event(GESTURE_ABS_MT_TRACKING_ID, -1, 1300)
    };
    InputEvent next_tap[] = {
        abs_event(GESTURE_ABS_MT_TRACKING_ID, 2, 2000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 152, 2000),
        syn_event(2000),
        abs_event(GESTURE_ABS_MT_TRACKING_ID, -1, 2100)
    };

    gesture_init(&g);
    KH_CHECK_MSG(feed_result(&g, pull, sizeof(pull) / sizeof(pull[0]), &tap) ==
                     GESTURE_RESULT_SHEET_PULL,
                 "setup: a 240 px pull must open the sheet");
    gesture_cancel(&g);
    expect_tap("tap at the previous x without an X event",
               feed(&g, next_tap, sizeof(next_tap) / sizeof(next_tap[0]), &tap),
               &tap, 380, 152);
}

static void test_cancel_keeps_y_when_next_press_omits_it(void)
{
    Gesture g;
    Tap tap = { -1, -1 };
    InputEvent prior[] = {
        abs_event(GESTURE_ABS_MT_TRACKING_ID, 1, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 300, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 152, 1000),
        syn_event(1000),
        abs_event(GESTURE_ABS_MT_TRACKING_ID, -1, 1100)
    };
    InputEvent next_tap[] = {
        abs_event(GESTURE_ABS_MT_TRACKING_ID, 2, 2000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 260, 2000),
        syn_event(2000),
        abs_event(GESTURE_ABS_MT_TRACKING_ID, -1, 2100)
    };

    gesture_init(&g);
    expect_tap("setup: tap at (300, 152)",
               feed(&g, prior, sizeof(prior) / sizeof(prior[0]), &tap), &tap,
               300, 152);
    gesture_cancel(&g);
    expect_tap("tap at the previous y without a Y event",
               feed(&g, next_tap, sizeof(next_tap) / sizeof(next_tap[0]), &tap),
               &tap, 260, 152);
}

static void test_cancel_drops_press_in_progress(void)
{
    Gesture g;
    Tap tap = { -1, -1 };
    InputEvent press[] = {
        abs_event(GESTURE_ABS_MT_TRACKING_ID, 1, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 300, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 400, 1000),
        syn_event(1000)
    };
    InputEvent release[] = {
        abs_event(GESTURE_ABS_MT_TRACKING_ID, -1, 1100)
    };
    InputEvent btn_press[] = {
        key_event(GESTURE_BTN_TOUCH, 1, 2000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 500, 2000),
        syn_event(2000)
    };
    InputEvent btn_release[] = {
        key_event(GESTURE_BTN_TOUCH, 0, 2100)
    };

    gesture_init(&g);
    feed(&g, press, sizeof(press) / sizeof(press[0]), &tap);
    gesture_cancel(&g);
    KH_CHECK_MSG(!feed(&g, release, sizeof(release) / sizeof(release[0]),
                       &tap),
                 "a release after cancel must not make a tap");

    feed(&g, btn_press, sizeof(btn_press) / sizeof(btn_press[0]), &tap);
    gesture_cancel(&g);
    KH_CHECK_MSG(!feed(&g, btn_release,
                       sizeof(btn_release) / sizeof(btn_release[0]), &tap),
                 "a BTN_TOUCH release after cancel must not make a tap");
}

/* The drained events are gone, but their positions reach the rules through
 * input_drain() and gesture_set_position().  A press whose axes both match
 * that value sends no ABS events at all and still must tap there. */
static void test_set_position_seeds_tap_after_cancel(void)
{
    Gesture g;
    Tap tap = { -1, -1 };
    InputEvent press[] = {
        key_event(GESTURE_BTN_TOUCH, 1, 3000),
        syn_event(3000),
        key_event(GESTURE_BTN_TOUCH, 0, 3050)
    };

    gesture_init(&g);
    gesture_cancel(&g);
    gesture_set_position(&g, 380, 152);
    expect_tap("tap with no ABS events uses the seeded position",
               feed(&g, press, sizeof(press) / sizeof(press[0]), &tap), &tap,
               380, 152);
}

/* A press held for 600 ms or more without moving is a long press, and a
 * shorter one stays a tap.  The release time is the only thing that changes
 * between these cases. */
static GestureResult hold_from(int start_x, int start_y, int hold_ms,
                               Tap *tap)
{
    Gesture g;
    InputEvent seq[] = {
        abs_event(GESTURE_ABS_MT_TRACKING_ID, 1, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, start_x, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, start_y, 1000),
        syn_event(1000),
        abs_event(GESTURE_ABS_MT_TRACKING_ID, -1, 1000 + hold_ms)
    };

    gesture_init(&g);
    return feed_result(&g, seq, sizeof(seq) / sizeof(seq[0]), tap);
}

static void test_hold_just_past_600ms_is_long_press(void)
{
    Tap tap = { -1, -1 };
    GestureResult result = hold_from(100, 200, GESTURE_LONG_PRESS_MS + 1, &tap);

    KH_CHECK_MSG(result == GESTURE_RESULT_LONG_PRESS,
                 "601 ms hold: expected GESTURE_RESULT_LONG_PRESS, got %d",
                 result);
    KH_CHECK_MSG(tap.x == 100 && tap.y == 200,
                 "601 ms hold: expected the press point (100, 200), got (%d, "
                 "%d)",
                 tap.x, tap.y);
}

static void test_hold_past_600ms_is_long_press(void)
{
    Tap tap = { -1, -1 };
    GestureResult result = hold_from(100, 200, 700, &tap);

    KH_CHECK_MSG(result == GESTURE_RESULT_LONG_PRESS,
                 "700 ms hold: expected GESTURE_RESULT_LONG_PRESS, got %d",
                 result);
    KH_CHECK_MSG(tap.x == 100 && tap.y == 200,
                 "700 ms hold: expected the press point (100, 200), got (%d, "
                 "%d)",
                 tap.x, tap.y);
}

static void test_hold_under_600ms_is_tap(void)
{
    Tap tap = { -1, -1 };
    GestureResult result = hold_from(100, 200, 599, &tap);

    KH_CHECK_MSG(result == GESTURE_RESULT_TAP,
                 "599 ms hold: expected GESTURE_RESULT_TAP, got %d", result);
}

static void test_moved_hold_is_not_long_press(void)
{
    Gesture g;
    Tap tap = { -1, -1 };
    InputEvent seq[] = {
        abs_event(GESTURE_ABS_MT_TRACKING_ID, 1, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 600, 1000),
        abs_event(GESTURE_ABS_MT_POSITION_Y, 400, 1000),
        syn_event(1000),
        abs_event(GESTURE_ABS_MT_POSITION_X, 700, 1700),
        syn_event(1700),
        abs_event(GESTURE_ABS_MT_TRACKING_ID, -1, 1900)
    };
    GestureResult result;

    gesture_init(&g);
    result = feed_result(&g, seq, sizeof(seq) / sizeof(seq[0]), &tap);
    KH_CHECK_MSG(result != GESTURE_RESULT_LONG_PRESS,
                 "a 900 ms press that moved 100 px must not be a long press");
    KH_CHECK_MSG(result != GESTURE_RESULT_TAP,
                 "a press that moved 100 px must not be a tap either");
}

/* At startup main seeds the gesture with the position read from the device.
 * evdev omits axes whose value did not change, so a press can arrive with no
 * coordinate event at all and must still report the seeded point. */
static void test_startup_position_without_abs_is_tap(void)
{
    Gesture g;
    Tap tap = { -1, -1 };
    InputEvent seq[] = {
        key_event(GESTURE_BTN_TOUCH, 1, 3000),
        syn_event(3000),
        key_event(GESTURE_BTN_TOUCH, 0, 3050)
    };
    GestureResult result;

    gesture_init(&g);
    gesture_set_position(&g, 37, 81);
    result = feed_result(&g, seq, sizeof(seq) / sizeof(seq[0]), &tap);
    KH_CHECK_MSG(result == GESTURE_RESULT_TAP,
                 "startup tap without ABS: expected GESTURE_RESULT_TAP, got %d",
                 result);
    KH_CHECK_MSG(tap.x == 37 && tap.y == 81,
                 "startup tap without ABS: expected (37, 81), got (%d, %d)",
                 tap.x, tap.y);
}

static void test_startup_position_without_abs_is_long_press(void)
{
    Gesture g;
    Tap tap = { -1, -1 };
    InputEvent seq[] = {
        key_event(GESTURE_BTN_TOUCH, 1, 3000),
        syn_event(3000),
        key_event(GESTURE_BTN_TOUCH, 0, 3700)
    };
    GestureResult result;

    gesture_init(&g);
    gesture_set_position(&g, 37, 81);
    result = feed_result(&g, seq, sizeof(seq) / sizeof(seq[0]), &tap);
    KH_CHECK_MSG(result == GESTURE_RESULT_LONG_PRESS,
                 "startup hold without ABS: expected GESTURE_RESULT_LONG_PRESS, "
                 "got %d",
                 result);
    KH_CHECK_MSG(tap.x == 37 && tap.y == 81,
                 "startup hold without ABS: expected (37, 81), got (%d, %d)",
                 tap.x, tap.y);
}

void kh_run_gesture_tests(void)
{
    printf("\n[gesture]\n");
    kh_run_test("gesture.mt_tap_uses_start_point",
                test_mt_tap_uses_start_point);
    kh_run_test("gesture.moved_past_threshold_is_not_a_tap",
                test_moved_past_threshold_is_not_a_tap);
    kh_run_test("gesture.movement_at_threshold_is_a_tap",
                test_movement_at_threshold_is_a_tap);
    kh_run_test("gesture.press_duration_boundary", test_press_duration_boundary);
    kh_run_test("gesture.btn_touch_sequence_without_tracking_id",
                test_btn_touch_sequence_without_tracking_id);
    kh_run_test("gesture.btn_touch_only_events",
                test_btn_touch_only_events);
    kh_run_test("gesture.spurious_releases_are_ignored",
                test_spurious_releases_are_ignored);
    kh_run_test("gesture.top_edge_pull_reports_sheet_pull",
                test_top_edge_pull_reports_sheet_pull);
    kh_run_test("gesture.top_edge_band_boundary",
                test_top_edge_band_boundary);
    kh_run_test("gesture.top_edge_band_covers_96_not_97",
                test_top_edge_band_covers_96_not_97);
    kh_run_test("gesture.pull_distance_boundary",
                test_pull_distance_boundary);
    kh_run_test("gesture.sideways_movement_cancels_pull",
                test_sideways_movement_cancels_pull);
    kh_run_test("gesture.swipe_left_and_right", test_swipe_left_and_right);
    kh_run_test("gesture.swipe_distance_boundary",
                test_swipe_distance_boundary);
    kh_run_test("gesture.swipe_angle_boundary", test_swipe_angle_boundary);
    kh_run_test("gesture.swipe_is_not_a_tap_or_long_press",
                test_swipe_is_not_a_tap_or_long_press);
    kh_run_test("gesture.top_edge_swipe_is_not_a_pull",
                test_top_edge_swipe_is_not_a_pull);
    kh_run_test("gesture.vertical_drag_is_not_a_swipe",
                test_vertical_drag_is_not_a_swipe);
    kh_run_test("gesture.result_tap_keeps_coordinates",
                test_result_tap_keeps_coordinates);
    kh_run_test("gesture.handle_reports_taps_but_not_pulls",
                test_handle_reports_taps_but_not_pulls);
    kh_run_test("gesture.cancel_keeps_x_when_next_press_omits_it",
                test_cancel_keeps_x_when_next_press_omits_it);
    kh_run_test("gesture.cancel_keeps_y_when_next_press_omits_it",
                test_cancel_keeps_y_when_next_press_omits_it);
    kh_run_test("gesture.cancel_drops_press_in_progress",
                test_cancel_drops_press_in_progress);
    kh_run_test("gesture.set_position_seeds_tap_after_cancel",
                test_set_position_seeds_tap_after_cancel);
    kh_run_test("gesture.hold_just_past_600ms_is_long_press",
                test_hold_just_past_600ms_is_long_press);
    kh_run_test("gesture.hold_past_600ms_is_long_press",
                test_hold_past_600ms_is_long_press);
    kh_run_test("gesture.hold_under_600ms_is_tap", test_hold_under_600ms_is_tap);
    kh_run_test("gesture.moved_hold_is_not_long_press",
                test_moved_hold_is_not_long_press);
    kh_run_test("gesture.startup_position_without_abs_is_tap",
                test_startup_position_without_abs_is_tap);
    kh_run_test("gesture.startup_position_without_abs_is_long_press",
                test_startup_position_without_abs_is_long_press);
}
