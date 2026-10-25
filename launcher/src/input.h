#ifndef KHOME_INPUT_H
#define KHOME_INPUT_H

#include "gesture.h"

#define INPUT_PENDING_MAX 16

typedef struct {
    int fd;
    char path[64];
    /* Events already read from fd but not yet handed to the gesture rules,
     * because a completed gesture ended that call. */
    InputEvent pending[INPUT_PENDING_MAX];
    int pending_count;
    int pending_pos;
    /* Position the kernel holds when the device is opened.  evdev omits an
     * axis equal to its previous value, so the first press can arrive without
     * coordinates; the gesture starts from here instead. */
    int initial_x;
    int initial_y;
    int has_initial;
} InputDevice;

/* Finds the cyttsp4_mt touchscreen in /proc/bus/input/devices, falling back
 * to any evdev node that reports ABS_MT_POSITION_X. */
int input_open(InputDevice *dev);
void input_close(InputDevice *dev);
/* EVIOCGRAB, so the stock UI does not also act on our taps. */
int input_grab(InputDevice *dev, int on);
/* Reads and discards queued events.  ABS_MT_POSITION_X/Y values in the
 * discarded events still update the gesture position, because evdev compares
 * the next press against those values and may omit an unchanged axis.
 * Returns 0, or -1 on a read error. */
int input_drain(InputDevice *dev, Gesture *gesture);
/* Drains queued events.  Returns 1 with a completed tap, 0 otherwise,
 * -1 on a read error. */
int input_poll(InputDevice *dev, Gesture *gesture, Tap *tap);
/* Same read loop, but reports every completed gesture.  Returns 1 when one
 * completed (in *result), 0 otherwise, -1 on a read error.  The _oriented
 * form applies the landscape edge mapping in gesture_is_top_pull(); the plain
 * form is portrait-only. */
int input_poll_gesture(InputDevice *dev, Gesture *gesture, Tap *tap,
                       GestureResult *result);
int input_poll_gesture_oriented(InputDevice *dev, Gesture *gesture, Tap *tap,
                                GestureResult *result, int landscape);

#endif
