#include "suites.h"

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>

#include "gesture.h"
#include "input.h"
#include "test_harness.h"

/* src/input.c reads evdev records from InputDevice.fd.  A pipe supplies the
 * records, so the test fixes the kernel timestamps while the caller reads
 * everything in one go. */

static int64_t realtime_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void put_event(int fd, uint16_t type, uint16_t code, int32_t value,
                      int64_t time_ms)
{
    struct input_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.time.tv_sec = (time_t)(time_ms / 1000);
    ev.time.tv_usec = (suseconds_t)((time_ms % 1000) * 1000);
    ev.type = type;
    ev.code = code;
    ev.value = value;
    KH_CHECK_MSG(write(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev),
                 "write an input_event to the device pipe");
}

static void put_press(int fd, int64_t time_ms)
{
    put_event(fd, GESTURE_EV_KEY, GESTURE_BTN_TOUCH, 1, time_ms);
    put_event(fd, GESTURE_EV_ABS, GESTURE_ABS_MT_POSITION_X, 300, time_ms);
    put_event(fd, GESTURE_EV_ABS, GESTURE_ABS_MT_POSITION_Y, 400, time_ms);
    put_event(fd, GESTURE_EV_SYN, GESTURE_SYN_REPORT, 0, time_ms);
}

static void put_release(int fd, int64_t time_ms)
{
    put_event(fd, GESTURE_EV_KEY, GESTURE_BTN_TOUCH, 0, time_ms);
}

/* Writes one complete tap at (x, y) that lasts 50 ms starting at time_ms. */
static void put_tap(int fd, int x, int y, int64_t time_ms)
{
    put_event(fd, GESTURE_EV_KEY, GESTURE_BTN_TOUCH, 1, time_ms);
    put_event(fd, GESTURE_EV_ABS, GESTURE_ABS_MT_POSITION_X, x, time_ms);
    put_event(fd, GESTURE_EV_ABS, GESTURE_ABS_MT_POSITION_Y, y, time_ms);
    put_event(fd, GESTURE_EV_SYN, GESTURE_SYN_REPORT, 0, time_ms);
    put_release(fd, time_ms + 50);
}

static int open_device(InputDevice *dev, int *write_fd)
{
    int fds[2];
    int flags;

    if (pipe(fds) != 0) {
        return -1;
    }
    flags = fcntl(fds[0], F_GETFL, 0);
    if (flags < 0 || fcntl(fds[0], F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    memset(dev, 0, sizeof(*dev));
    dev->fd = fds[0];
    *write_fd = fds[1];
    return 0;
}

static void test_tap_duration_comes_from_event_time(void)
{
    InputDevice dev;
    Gesture g;
    Tap tap;
    GestureResult result;
    int write_fd;
    int64_t now;

    if (open_device(&dev, &write_fd) != 0) {
        KH_CHECK_MSG(0, "could not open a device pipe");
        return;
    }

    now = realtime_ms();
    put_press(write_fd, now - 100);
    put_release(write_fd, now);
    gesture_init(&g);
    tap.x = -1;
    tap.y = -1;
    result = GESTURE_RESULT_NONE;
    KH_CHECK_MSG(input_poll_gesture(&dev, &g, &tap, &result) == 1,
                 "100 ms press read at once: expected a completed gesture");
    KH_CHECK_MSG(result == GESTURE_RESULT_TAP,
                 "100 ms press: expected a tap, got result %d", result);
    KH_CHECK_MSG(tap.x == 300 && tap.y == 400,
                 "100 ms press: expected (300, 400), got (%d, %d)", tap.x,
                 tap.y);

    now = realtime_ms();
    put_press(write_fd, now - 700);
    put_release(write_fd, now);
    gesture_init(&g);
    result = GESTURE_RESULT_NONE;
    KH_CHECK_MSG(input_poll_gesture(&dev, &g, &tap, &result) == 1,
                 "700 ms press read at once: expected a completed gesture");
    KH_CHECK_MSG(result == GESTURE_RESULT_LONG_PRESS,
                 "700 ms press: expected a long press, got result %d", result);

    close(write_fd);
    close(dev.fd);
}

static void test_drain_keeps_discarded_position(void)
{
    InputDevice dev;
    Gesture g;
    Tap tap;
    GestureResult result;
    int write_fd;
    int64_t now;

    if (open_device(&dev, &write_fd) != 0) {
        KH_CHECK_MSG(0, "could not open a device pipe");
        return;
    }

    now = realtime_ms();
    put_event(write_fd, GESTURE_EV_ABS, GESTURE_ABS_MT_TRACKING_ID, 1,
              now - 100);
    put_event(write_fd, GESTURE_EV_ABS, GESTURE_ABS_MT_POSITION_X, 380,
              now - 100);
    put_event(write_fd, GESTURE_EV_ABS, GESTURE_ABS_MT_POSITION_Y, 250,
              now - 100);
    put_event(write_fd, GESTURE_EV_SYN, GESTURE_SYN_REPORT, 0, now - 100);
    put_event(write_fd, GESTURE_EV_ABS, GESTURE_ABS_MT_TRACKING_ID, -1, now);

    gesture_init(&g);
    tap.x = -1;
    tap.y = -1;
    result = GESTURE_RESULT_NONE;
    KH_CHECK_MSG(input_poll_gesture(&dev, &g, &tap, &result) == 1,
                 "setup: expected the first gesture to complete");

    /* A press abandoned during a drain, as when the grab is handed back. */
    put_event(write_fd, GESTURE_EV_ABS, GESTURE_ABS_MT_TRACKING_ID, 2,
              now + 10);
    put_event(write_fd, GESTURE_EV_ABS, GESTURE_ABS_MT_POSITION_X, 120,
              now + 10);
    put_event(write_fd, GESTURE_EV_ABS, GESTURE_ABS_MT_POSITION_Y, 160,
              now + 10);
    put_event(write_fd, GESTURE_EV_SYN, GESTURE_SYN_REPORT, 0, now + 10);
    KH_CHECK_MSG(input_drain(&dev, &g) == 0,
                 "input_drain must stop at EAGAIN");
    gesture_cancel(&g);

    /* The next press repeats x=120, which evdev suppresses; only y arrives. */
    put_event(write_fd, GESTURE_EV_ABS, GESTURE_ABS_MT_TRACKING_ID, 3,
              now + 20);
    put_event(write_fd, GESTURE_EV_ABS, GESTURE_ABS_MT_POSITION_Y, 200,
              now + 20);
    put_event(write_fd, GESTURE_EV_SYN, GESTURE_SYN_REPORT, 0, now + 20);
    put_event(write_fd, GESTURE_EV_ABS, GESTURE_ABS_MT_TRACKING_ID, -1,
              now + 30);
    result = GESTURE_RESULT_NONE;
    KH_CHECK_MSG(input_poll_gesture(&dev, &g, &tap, &result) == 1,
                 "tap after drain: expected a completed gesture");
    KH_CHECK_MSG(result == GESTURE_RESULT_TAP,
                 "tap after drain: expected a tap, got result %d", result);
    KH_CHECK_MSG(tap.x == 120 && tap.y == 200,
                 "tap after drain: expected (120, 200), got (%d, %d)", tap.x,
                 tap.y);

    close(write_fd);
    close(dev.fd);
}

/* One read can carry several completed taps.  Each call must report the taps
 * in order instead of keeping only the last one from that read. */
static void test_two_taps_in_one_read_come_back_in_order(void)
{
    InputDevice dev;
    Gesture g;
    Tap tap;
    GestureResult result;
    int write_fd;
    int64_t now;

    if (open_device(&dev, &write_fd) != 0) {
        KH_CHECK_MSG(0, "could not open a device pipe");
        return;
    }

    now = realtime_ms();
    put_tap(write_fd, 300, 400, now - 300);
    put_tap(write_fd, 500, 600, now - 150);
    gesture_init(&g);
    tap.x = -1;
    tap.y = -1;
    result = GESTURE_RESULT_NONE;
    KH_CHECK_MSG(input_poll_gesture(&dev, &g, &tap, &result) == 1,
                 "first of two taps: expected a completed gesture");
    KH_CHECK_MSG(result == GESTURE_RESULT_TAP,
                 "first of two taps: expected a tap, got result %d", result);
    KH_CHECK_MSG(tap.x == 300 && tap.y == 400,
                 "first of two taps: expected (300, 400), got (%d, %d)", tap.x,
                 tap.y);

    result = GESTURE_RESULT_NONE;
    KH_CHECK_MSG(input_poll_gesture(&dev, &g, &tap, &result) == 1,
                 "second of two taps: expected a completed gesture from the "
                 "events left over by the first call");
    KH_CHECK_MSG(result == GESTURE_RESULT_TAP,
                 "second of two taps: expected a tap, got result %d", result);
    KH_CHECK_MSG(tap.x == 500 && tap.y == 600,
                 "second of two taps: expected (500, 600), got (%d, %d)",
                 tap.x, tap.y);

    KH_CHECK_MSG(input_poll_gesture(&dev, &g, &tap, &result) == 0,
                 "after both taps: expected no more gestures");

    close(write_fd);
    close(dev.fd);
}

/* Draining throws away a half-read gesture but keeps its last position, so a
 * following press that evdev reports without an unchanged axis still taps at
 * the right place. */
static void test_drain_drops_pending_events_but_keeps_their_position(void)
{
    InputDevice dev;
    Gesture g;
    Tap tap;
    GestureResult result;
    int write_fd;
    int64_t now;

    if (open_device(&dev, &write_fd) != 0) {
        KH_CHECK_MSG(0, "could not open a device pipe");
        return;
    }

    now = realtime_ms();
    put_tap(write_fd, 300, 400, now - 300);
    put_tap(write_fd, 500, 600, now - 150);
    gesture_init(&g);
    result = GESTURE_RESULT_NONE;
    KH_CHECK_MSG(input_poll_gesture(&dev, &g, &tap, &result) == 1,
                 "setup: expected the first tap");
    KH_CHECK_MSG(tap.x == 300 && tap.y == 400,
                 "setup: expected the first tap at (300, 400), got (%d, %d)",
                 tap.x, tap.y);

    /* The second tap is still queued in dev.pending here, with x=500, y=600. */
    KH_CHECK_MSG(input_drain(&dev, &g) == 0,
                 "input_drain must stop at EAGAIN");
    KH_CHECK_MSG(input_poll_gesture(&dev, &g, &tap, &result) == 0,
                 "after the drain: the second tap must be gone");

    /* The next press repeats x=500, which evdev suppresses; only y arrives. */
    put_event(write_fd, GESTURE_EV_KEY, GESTURE_BTN_TOUCH, 1, now);
    put_event(write_fd, GESTURE_EV_ABS, GESTURE_ABS_MT_POSITION_Y, 650, now);
    put_event(write_fd, GESTURE_EV_SYN, GESTURE_SYN_REPORT, 0, now);
    put_release(write_fd, now + 50);
    result = GESTURE_RESULT_NONE;
    KH_CHECK_MSG(input_poll_gesture(&dev, &g, &tap, &result) == 1,
                 "tap after the drain: expected a completed gesture");
    KH_CHECK_MSG(result == GESTURE_RESULT_TAP,
                 "tap after the drain: expected a tap, got result %d", result);
    KH_CHECK_MSG(tap.x == 500 && tap.y == 650,
                 "tap after the drain: expected (500, 650) from the drained "
                 "position, got (%d, %d)", tap.x, tap.y);

    close(write_fd);
    close(dev.fd);
}

void kh_run_input_tests(void)
{
    printf("\n[input]\n");
    kh_run_test("input.tap_duration_comes_from_event_time",
                test_tap_duration_comes_from_event_time);
    kh_run_test("input.drain_keeps_discarded_position",
                test_drain_keeps_discarded_position);
    kh_run_test("input.two_taps_in_one_read_come_back_in_order",
                test_two_taps_in_one_read_come_back_in_order);
    kh_run_test("input.drain_drops_pending_events_but_keeps_their_position",
                test_drain_drops_pending_events_but_keeps_their_position);
}
