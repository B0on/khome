#ifndef KHOME_TEST_LINUX_INPUT_H
#define KHOME_TEST_LINUX_INPUT_H

/* Host stand-in for <linux/input.h>, which macOS does not ship.  src/input.c
 * only needs the evdev record and the ioctl names, so this header defines
 * those.  The record mirrors 64-bit Linux: a timeval followed by type, code
 * and value.  Tests write it into a pipe that InputDevice.fd reads. */
#include <stdint.h>
#include <sys/time.h>

struct input_event {
    struct timeval time;
    uint16_t type;
    uint16_t code;
    int32_t value;
};

#define input_event_sec time.tv_sec
#define input_event_usec time.tv_usec

#define EV_ABS 0x03
#define ABS_MAX 0x3f
#define ABS_MT_POSITION_X 0x35
#define ABS_MT_POSITION_Y 0x36

/* Never issued by the tests; defined so input.c compiles on the host. */
#define EVIOCGBIT(ev, len) 0
#define EVIOCGRAB 0

#endif
