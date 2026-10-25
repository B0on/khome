#include "gesture.h"

#include <stddef.h>

static void gesture_begin(Gesture *g, int64_t time_ms)
{
    g->down = 1;
    g->have_pos = 0;
    g->moved = 0;
    g->start_x = g->last_x;
    g->start_y = g->last_y;
    g->start_ms = time_ms;
}

/* X and Y arrive as separate events, so the start point and any movement are
 * only taken at SYN_REPORT, when the pair is complete. */
static void gesture_sync(Gesture *g)
{
    if (!g->down) {
        return;
    }
    if (!g->have_pos) {
        g->have_pos = 1;
        g->start_x = g->last_x;
        g->start_y = g->last_y;
        return;
    }
    if (g->last_x - g->start_x > GESTURE_TAP_MAX_PX ||
        g->start_x - g->last_x > GESTURE_TAP_MAX_PX ||
        g->last_y - g->start_y > GESTURE_TAP_MAX_PX ||
        g->start_y - g->last_y > GESTURE_TAP_MAX_PX) {
        g->moved = 1;
    }
}

int gesture_is_top_pull(int landscape, int start_x, int start_y, int dx,
                        int dy)
{
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;

    /* Portrait, and the portrait top edge while the display is rotated: both
     * are the long-standing sheet-pull gesture. */
    if (start_y >= 0 && start_y <= GESTURE_SHEET_EDGE_PX &&
        dy >= GESTURE_SHEET_PULL_PX && adx * 2 <= dy) {
        return 1;
    }
    if (!landscape) {
        return 0;
    }
    /* The rotated screen's top is one of the panel's long edges.  khome does
     * not know which one winmgr puts up for landscapeLeft, and the brief
     * allows either, so a pull inward from x=0 or x=757 both count. */
    if (start_x >= 0 && start_x <= GESTURE_LANDSCAPE_EDGE_PX &&
        dx >= GESTURE_SHEET_PULL_PX && ady * 2 <= dx) {
        return 1;
    }
    if (start_x >= GESTURE_PANEL_W - 1 - GESTURE_LANDSCAPE_EDGE_PX &&
        start_x < GESTURE_PANEL_W && -dx >= GESTURE_SHEET_PULL_PX &&
        ady * 2 <= -dx) {
        return 1;
    }
    return 0;
}

static GestureResult gesture_end(Gesture *g, int64_t time_ms, Tap *tap,
                                 int landscape)
{
    int x;
    int y;

    if (!g->down) {
        return GESTURE_RESULT_NONE;
    }
    g->down = 0;
    x = g->have_pos ? g->start_x : g->last_x;
    y = g->have_pos ? g->start_y : g->last_y;
    if (!g->moved && time_ms - g->start_ms <= GESTURE_TAP_MAX_MS) {
        if (tap != NULL) {
            tap->x = x;
            tap->y = y;
        }
        return GESTURE_RESULT_TAP;
    }
    if (!g->moved && time_ms - g->start_ms >= GESTURE_LONG_PRESS_MS) {
        if (tap != NULL) {
            tap->x = x;
            tap->y = y;
        }
        return GESTURE_RESULT_LONG_PRESS;
    }
    if (g->have_pos) {
        int dx = g->last_x - g->start_x;
        int dy = g->last_y - g->start_y;
        int adx = dx < 0 ? -dx : dx;
        int ady = dy < 0 ? -dy : dy;

        if (gesture_is_top_pull(landscape, g->start_x, g->start_y, dx, dy)) {
            return GESTURE_RESULT_SHEET_PULL;
        }
        if (adx >= GESTURE_SWIPE_PX && ady * 2 <= adx) {
            return dx < 0 ? GESTURE_RESULT_SWIPE_LEFT
                          : GESTURE_RESULT_SWIPE_RIGHT;
        }
    }
    return GESTURE_RESULT_NONE;
}

void gesture_init(Gesture *g)
{
    if (g == NULL) {
        return;
    }
    g->down = 0;
    g->have_pos = 0;
    g->start_x = 0;
    g->start_y = 0;
    g->last_x = 0;
    g->last_y = 0;
    g->moved = 0;
    g->start_ms = 0;
}

void gesture_cancel(Gesture *g)
{
    if (g == NULL) {
        return;
    }
    g->down = 0;
    g->have_pos = 0;
    g->moved = 0;
    g->start_x = g->last_x;
    g->start_y = g->last_y;
    g->start_ms = 0;
}

void gesture_set_position(Gesture *g, int x, int y)
{
    if (g == NULL) {
        return;
    }
    g->last_x = x;
    g->last_y = y;
}

GestureResult gesture_handle_result_oriented(Gesture *g, const InputEvent *ev,
                                             Tap *tap, int landscape)
{
    if (g == NULL || ev == NULL) {
        return GESTURE_RESULT_NONE;
    }
    if (ev->type == GESTURE_EV_ABS) {
        if (ev->code == GESTURE_ABS_MT_POSITION_X) {
            g->last_x = ev->value;
        } else if (ev->code == GESTURE_ABS_MT_POSITION_Y) {
            g->last_y = ev->value;
        } else if (ev->code == GESTURE_ABS_MT_TRACKING_ID) {
            if (ev->value >= 0) {
                if (!g->down) {
                    gesture_begin(g, ev->time_ms);
                }
            } else {
                return gesture_end(g, ev->time_ms, tap, landscape);
            }
        }
        return GESTURE_RESULT_NONE;
    }
    if (ev->type == GESTURE_EV_KEY && ev->code == GESTURE_BTN_TOUCH) {
        if (ev->value == 1) {
            if (!g->down) {
                gesture_begin(g, ev->time_ms);
            }
            return GESTURE_RESULT_NONE;
        }
        return gesture_end(g, ev->time_ms, tap, landscape);
    }
    if (ev->type == GESTURE_EV_SYN && ev->code == GESTURE_SYN_REPORT) {
        gesture_sync(g);
    }
    return GESTURE_RESULT_NONE;
}

GestureResult gesture_handle_result(Gesture *g, const InputEvent *ev, Tap *tap)
{
    return gesture_handle_result_oriented(g, ev, tap, 0);
}

int gesture_handle(Gesture *g, const InputEvent *ev, Tap *tap)
{
    return gesture_handle_result(g, ev, tap) == GESTURE_RESULT_TAP;
}
