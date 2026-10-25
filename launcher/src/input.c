#define _POSIX_C_SOURCE 200809L

#include "input.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define DEVICES_FILE "/proc/bus/input/devices"
#define MAX_DEVICES 16

/* The host test stub only provides the names input.c already used. */
#ifndef ABS_X
#define ABS_X 0x00
#endif
#ifndef ABS_Y
#define ABS_Y 0x01
#endif
#ifndef EVIOCGABS
#define EVIOCGABS(abs) _IOR('E', 0x40 + (abs), int[6])
#endif

typedef struct {
    char name[128];
    char event[32];
} DeviceEntry;

static int parse_devices(DeviceEntry *out, int max)
{
    FILE *fp;
    char line[512];
    int count = 0;
    DeviceEntry cur;

    memset(&cur, 0, sizeof(cur));
    fp = fopen(DEVICES_FILE, "r");
    if (fp == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (line[0] == '\n' || line[0] == '\r') {
            if (cur.event[0] != 0 && count < max) {
                out[count++] = cur;
            }
            memset(&cur, 0, sizeof(cur));
            continue;
        }
        if (strncmp(line, "N: Name=", 8) == 0) {
            char *start = strchr(line, '"');
            char *end = start != NULL ? strchr(start + 1, '"') : NULL;
            if (start != NULL && end != NULL) {
                size_t len = (size_t)(end - start - 1);
                if (len >= sizeof(cur.name)) {
                    len = sizeof(cur.name) - 1;
                }
                memcpy(cur.name, start + 1, len);
                cur.name[len] = 0;
            }
        } else if (strncmp(line, "H: Handlers=", 12) == 0) {
            char *tok = strtok(line + 12, " \t\r\n");
            while (tok != NULL) {
                if (strncmp(tok, "event", 5) == 0) {
                    snprintf(cur.event, sizeof(cur.event), "/dev/input/%s",
                             tok);
                    break;
                }
                tok = strtok(NULL, " \t\r\n");
            }
        }
    }
    if (cur.event[0] != 0 && count < max) {
        out[count++] = cur;
    }
    fclose(fp);
    return count;
}

static int has_mt_position(int fd)
{
    unsigned long bits[ABS_MAX / (int)(8 * sizeof(unsigned long)) + 2];
    memset(bits, 0, sizeof(bits));
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits) < 0) {
        return 0;
    }
    return (bits[ABS_MT_POSITION_X / (int)(8 * sizeof(unsigned long))] &
            (1UL << (ABS_MT_POSITION_X % (int)(8 * sizeof(unsigned long))))) !=
           0;
}

static int read_abs_value(int fd, int code, int *value)
{
    int info[6];

    memset(info, 0, sizeof(info));
    if (ioctl(fd, EVIOCGABS(code), info) < 0) {
        return 0;
    }
    *value = info[0];
    return 1;
}

/* Linux 3.0 keeps MT axes in per-slot state and only updates the matching
 * absinfo value for non-MT events, so on this device this read returns 0,0.
 * main resets the slot to 0,0 once it has grabbed the device. */
static void read_initial_position(InputDevice *dev, int fd)
{
    int x;
    int y;

    dev->initial_x = 0;
    dev->initial_y = 0;
    dev->has_initial = 0;
    if (read_abs_value(fd, ABS_MT_POSITION_X, &x) &&
        read_abs_value(fd, ABS_MT_POSITION_Y, &y)) {
        dev->initial_x = x;
        dev->initial_y = y;
        dev->has_initial = 1;
    } else if (read_abs_value(fd, ABS_X, &x) &&
               read_abs_value(fd, ABS_Y, &y)) {
        dev->initial_x = x;
        dev->initial_y = y;
        dev->has_initial = 1;
    }
}

static int open_probe(const char *path)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return -1;
    }
    if (!has_mt_position(fd)) {
        close(fd);
        return -1;
    }
    return fd;
}

int input_open(InputDevice *dev)
{
    DeviceEntry entries[MAX_DEVICES];
    int count;
    int i;
    int fd;

    if (dev == NULL) {
        return -1;
    }
    dev->fd = -1;
    dev->path[0] = 0;
    dev->pending_count = 0;
    dev->pending_pos = 0;
    dev->initial_x = 0;
    dev->initial_y = 0;
    dev->has_initial = 0;
    count = parse_devices(entries, MAX_DEVICES);
    for (i = 0; i < count; i++) {
        if (strstr(entries[i].name, "cyttsp4_mt") == NULL) {
            continue;
        }
        fd = open_probe(entries[i].event);
        if (fd >= 0) {
            dev->fd = fd;
            snprintf(dev->path, sizeof(dev->path), "%s", entries[i].event);
            read_initial_position(dev, fd);
            return 0;
        }
    }
    for (i = 0; i < count; i++) {
        fd = open_probe(entries[i].event);
        if (fd >= 0) {
            dev->fd = fd;
            snprintf(dev->path, sizeof(dev->path), "%s", entries[i].event);
            read_initial_position(dev, fd);
            return 0;
        }
    }
    fprintf(stderr, "khome: no multitouch input device found\n");
    return -1;
}

void input_close(InputDevice *dev)
{
    if (dev == NULL) {
        return;
    }
    if (dev->fd >= 0) {
        close(dev->fd);
        dev->fd = -1;
    }
    dev->pending_count = 0;
    dev->pending_pos = 0;
}

int input_grab(InputDevice *dev, int on)
{
    int arg = on ? 1 : 0;

    if (dev == NULL || dev->fd < 0) {
        return -1;
    }
    if (ioctl(dev->fd, EVIOCGRAB, arg) < 0) {
        fprintf(stderr, "khome: EVIOCGRAB(%d) on %s failed: %s\n", on,
                dev->path, strerror(errno));
        return -1;
    }
    return 0;
}

int input_drain(InputDevice *dev, Gesture *gesture)
{
    struct input_event buf[16];
    ssize_t got;
    ssize_t i;
    int x = gesture != NULL ? gesture->last_x : 0;
    int y = gesture != NULL ? gesture->last_y : 0;

    if (dev == NULL || dev->fd < 0) {
        return -1;
    }
    for (i = dev->pending_pos; i < dev->pending_count; i++) {
        const InputEvent *ev = &dev->pending[i];
        if (ev->type != GESTURE_EV_ABS) {
            continue;
        }
        if (ev->code == GESTURE_ABS_MT_POSITION_X) {
            x = ev->value;
        } else if (ev->code == GESTURE_ABS_MT_POSITION_Y) {
            y = ev->value;
        }
    }
    dev->pending_count = 0;
    dev->pending_pos = 0;
    for (;;) {
        got = read(dev->fd, buf, sizeof(buf));
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (got == 0) {
            return -1;
        }
        for (i = 0; i < got / (ssize_t)sizeof(buf[0]); i++) {
            if (buf[i].type != EV_ABS) {
                continue;
            }
            if (buf[i].code == ABS_MT_POSITION_X) {
                x = buf[i].value;
            } else if (buf[i].code == ABS_MT_POSITION_Y) {
                y = buf[i].value;
            }
        }
    }
    if (gesture != NULL) {
        gesture_set_position(gesture, x, y);
    }
    return 0;
}

static int64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int64_t realtime_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Anything older than this is treated as a clock jump rather than read lag. */
#define EVENT_TIME_MAX_AGE_MS 60000

/* The 3.0.35 kernel has no EVIOCSCLOCKID, so ev.time carries CLOCK_REALTIME.
 * Rebasing it onto monotonic_ms() keeps press duration measured against the
 * same clock as the rest of the gesture timing while a blocked main loop can
 * no longer stretch or shrink a tap.  A clock jump shows up as a non-positive
 * or absurd age, so fall back to the read time there. */
static int64_t event_time_ms(const struct input_event *ev, int64_t mono_now,
                             int64_t real_now)
{
    int64_t ev_real = (int64_t)ev->input_event_sec * 1000 +
                      ev->input_event_usec / 1000;
    int64_t age = real_now - ev_real;

    if (age < 0 || age > EVENT_TIME_MAX_AGE_MS) {
        return mono_now;
    }
    return mono_now - age;
}

int input_poll_gesture_oriented(InputDevice *dev, Gesture *gesture, Tap *tap,
                                GestureResult *result, int landscape)
{
    struct input_event buf[16];
    ssize_t got;
    ssize_t i;
    int64_t mono_now;
    int64_t real_now;

    if (dev == NULL || dev->fd < 0) {
        return -1;
    }
    for (;;) {
        /* A completed gesture ends this call, so the events after it stay
         * queued here for the next one. */
        while (dev->pending_pos < dev->pending_count) {
            InputEvent *ev = &dev->pending[dev->pending_pos++];
            GestureResult r =
                gesture_handle_result_oriented(gesture, ev, tap, landscape);
            if (r != GESTURE_RESULT_NONE) {
                if (result != NULL) {
                    *result = r;
                }
                return 1;
            }
        }
        dev->pending_count = 0;
        dev->pending_pos = 0;

        got = read(dev->fd, buf, sizeof(buf));
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (got == 0) {
            return -1;
        }
        mono_now = monotonic_ms();
        real_now = realtime_ms();
        dev->pending_count = (int)(got / (ssize_t)sizeof(buf[0]));
        for (i = 0; i < dev->pending_count; i++) {
            InputEvent *ev = &dev->pending[i];
            ev->type = buf[i].type;
            ev->code = buf[i].code;
            ev->value = buf[i].value;
            ev->time_ms = event_time_ms(&buf[i], mono_now, real_now);
        }
        dev->pending_pos = 0;
    }
}

int input_poll_gesture(InputDevice *dev, Gesture *gesture, Tap *tap,
                       GestureResult *result)
{
    return input_poll_gesture_oriented(dev, gesture, tap, result, 0);
}

int input_poll(InputDevice *dev, Gesture *gesture, Tap *tap)
{
    GestureResult result = GESTURE_RESULT_NONE;
    int got = input_poll_gesture(dev, gesture, tap, &result);
    return got > 0 && result == GESTURE_RESULT_TAP;
}

