#ifndef KHOME_GESTURE_H
#define KHOME_GESTURE_H

#include <stdint.h>

/* A decoded evdev event.  Keeping this struct separate from struct
 * input_event is what lets the gesture rules build and run on the host. */
typedef struct {
    int type;
    int code;
    int value;
    int64_t time_ms;
} InputEvent;

typedef struct {
    int x;
    int y;
} Tap;

typedef struct {
    int down;
    int have_pos;
    int start_x;
    int start_y;
    int last_x;
    int last_y;
    int moved;
    int64_t start_ms;
} Gesture;

/* What a completed press turned out to be. */
typedef enum {
    GESTURE_RESULT_NONE = 0,
    GESTURE_RESULT_TAP,
    GESTURE_RESULT_SHEET_PULL,
    GESTURE_RESULT_LONG_PRESS,
    GESTURE_RESULT_SWIPE_LEFT,
    GESTURE_RESULT_SWIPE_RIGHT
} GestureResult;

/* The evdev constants used below, spelled out so this header does not need
 * <linux/input.h> on the host. */
enum {
    GESTURE_EV_SYN = 0,
    GESTURE_EV_KEY = 1,
    GESTURE_EV_ABS = 3,
    GESTURE_SYN_REPORT = 0,
    GESTURE_ABS_MT_POSITION_X = 0x35,
    GESTURE_ABS_MT_POSITION_Y = 0x36,
    GESTURE_ABS_MT_TRACKING_ID = 0x39,
    GESTURE_BTN_TOUCH = 0x14a,
    GESTURE_TAP_MAX_MS = 600,
    GESTURE_LONG_PRESS_MS = 600,
    GESTURE_TAP_MAX_PX = 30,
    /* Device pixels: a pull starts in the top band, travels down at least
     * this far, and stays within half that distance horizontally.  A hand
     * aiming at the top edge landed 64-84 px down on the PW2 (khome.log), so
     * 40 px missed every real pull.  xband covers this band; widening it
     * costs the browser the top 97 px of press starts. */
    GESTURE_SHEET_EDGE_PX = 96,
    GESTURE_SHEET_PULL_PX = 120,
    /* A horizontal swipe travels this far sideways and stays within half that
     * distance vertically, so the top-edge pull and a page turn cannot both
     * match one movement. */
    GESTURE_SWIPE_PX = 120,
    /* The touch panel keeps reporting these portrait bounds while winmgr
     * rotates the display (758x1024). */
    GESTURE_PANEL_W = 758,
    GESTURE_PANEL_H = 1024,
    /* In landscape the panel's long edges are the top and bottom of the
     * rotated screen, so a pull starts within this distance of x=0 or x=757. */
    GESTURE_LANDSCAPE_EDGE_PX = 96
};

void gesture_init(Gesture *g);
/* Drops the press in progress, but keeps last_x/last_y.  evdev suppresses an
 * ABS_MT_POSITION_X/Y value equal to the previous one, so zeroing the position
 * makes the next press at the same x or y arrive without that axis. */
void gesture_cancel(Gesture *g);
/* Records the finger position without starting a press, for the values read
 * from events that were drained before they reached the gesture rules. */
void gesture_set_position(Gesture *g, int x, int y);
/* Returns 1 and fills tap when this event completes a tap. */
int gesture_handle(Gesture *g, const InputEvent *ev, Tap *tap);

/* 1 when (start_x, start_y) -> (start_x+dx, start_y+dy) is a pull away from
 * the user-visible top edge.  In portrait that is y=0..96 with downward
 * travel.  With landscape=1 the panel still reports portrait coordinates, so
 * the rotated screen's top/bottom are its long edges (x=0 and x=757); a pull
 * inward from either is accepted, and the portrait top is accepted as well.
 * Pure, so the orientation-to-edge mapping is testable without evdev. */
int gesture_is_top_pull(int landscape, int start_x, int start_y, int dx,
                        int dy);

/* Same state machine, but also reports a completed top-edge pull as
 * GESTURE_RESULT_SHEET_PULL and a horizontal swipe as SWIPE_LEFT/RIGHT.  tap
 * is filled only for GESTURE_RESULT_TAP.  landscape selects the edge mapping
 * above; gesture_handle_result() is the portrait-only form. */
GestureResult gesture_handle_result(Gesture *g, const InputEvent *ev, Tap *tap);
GestureResult gesture_handle_result_oriented(Gesture *g, const InputEvent *ev,
                                             Tap *tap, int landscape);

#endif
