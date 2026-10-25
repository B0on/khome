#include "suites.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "lipc.h"
#include "test_harness.h"

/* lipc_watch_read only touches fd, buf and len, so a pipe standing in for
 * lipc-wait-event drives the line reader without forking a child. */

static int open_watch(LipcWatch *w, int *write_fd)
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
    memset(w, 0, sizeof(*w));
    w->fd = fds[0];
    w->pid = 0;
    *write_fd = fds[1];
    return 0;
}

static void write_bytes(int fd, const char *text)
{
    size_t len = strlen(text);

    KH_CHECK_MSG(write(fd, text, len) == (ssize_t)len,
                 "write %zu bytes to the watch pipe", len);
}

static void expect_event(const char *label, int got, const LipcEvent *ev,
                         const char *name, const char *arg)
{
    KH_CHECK_MSG(got == 1, "%s: expected one event, got %d", label, got);
    if (got != 1) {
        return;
    }
    KH_CHECK_MSG(strcmp(ev->name, name) == 0, "%s: expected %s, got %s", label,
                 name, ev->name);
    KH_CHECK_MSG(ev->arg_count == 1, "%s: expected 1 arg, got %d", label,
                 ev->arg_count);
    if (ev->arg_count == 1) {
        KH_CHECK_MSG(strcmp(ev->args[0], arg) == 0,
                     "%s: expected arg %s, got %s", label, arg, ev->args[0]);
    }
}

static void test_noise_before_event_in_one_read(void)
{
    LipcWatch w;
    LipcEvent ev;
    int write_fd;
    int got;

    if (open_watch(&w, &write_fd) != 0) {
        KH_CHECK_MSG(0, "could not open a watch pipe");
        return;
    }
    write_bytes(write_fd, "\n[12:00:00.000000\noutOfScreenSaver 0\n");
    got = lipc_watch_read(&w, &ev);
    expect_event("noise then event", got, &ev, "outOfScreenSaver", "0");
    close(write_fd);
    close(w.fd);
}

static void test_noise_does_not_stop_the_next_read(void)
{
    LipcWatch w;
    LipcEvent ev;
    int write_fd;
    int got;

    if (open_watch(&w, &write_fd) != 0) {
        KH_CHECK_MSG(0, "could not open a watch pipe");
        return;
    }
    write_bytes(write_fd, "[broken\n");
    got = lipc_watch_read(&w, &ev);
    KH_CHECK_MSG(got == 0, "noise alone: expected 0, got %d", got);

    write_bytes(write_fd, "outOfScreenSaver 0\n");
    got = lipc_watch_read(&w, &ev);
    expect_event("event after noise", got, &ev, "outOfScreenSaver", "0");
    close(write_fd);
    close(w.fd);
}

static void test_partial_line_waits_for_the_rest(void)
{
    LipcWatch w;
    LipcEvent ev;
    int write_fd;
    int got;

    if (open_watch(&w, &write_fd) != 0) {
        KH_CHECK_MSG(0, "could not open a watch pipe");
        return;
    }
    write_bytes(write_fd, "outOfScreen");
    got = lipc_watch_read(&w, &ev);
    KH_CHECK_MSG(got == 0, "a line without a newline: expected 0, got %d",
                 got);

    write_bytes(write_fd, "Saver 0\n");
    got = lipc_watch_read(&w, &ev);
    expect_event("rest of the line", got, &ev, "outOfScreenSaver", "0");
    close(write_fd);
    close(w.fd);
}

void kh_run_lipc_watch_tests(void)
{
    printf("\n[lipc-watch]\n");
    kh_run_test("lipc_watch.noise_before_event_in_one_read",
                test_noise_before_event_in_one_read);
    kh_run_test("lipc_watch.noise_does_not_stop_the_next_read",
                test_noise_does_not_stop_the_next_read);
    kh_run_test("lipc_watch.partial_line_waits_for_the_rest",
                test_partial_line_waits_for_the_rest);
}
