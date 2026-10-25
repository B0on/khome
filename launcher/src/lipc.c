#define _POSIX_C_SOURCE 200809L

#include "lipc.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

#define LIPC_CMD_TIMEOUT_MS 3000

static int64_t now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

#define LIPC_WAIT_EVENT "/usr/bin/lipc-wait-event"
#define LIPC_GET_PROP "/usr/bin/lipc-get-prop"
#define LIPC_SET_PROP "/usr/bin/lipc-set-prop"

int lipc_parse_line(const char *line, LipcEvent *ev)
{
    const char *p = line;
    size_t n;

    if (line == NULL || ev == NULL) {
        return 0;
    }
    memset(ev, 0, sizeof(*ev));
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '[') {
        const char *close = strchr(p, ']');
        if (close == NULL) {
            return 0;
        }
        p = close + 1;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
    }
    n = 0;
    while (*p != 0 && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
        if (n + 1 < sizeof(ev->name)) {
            ev->name[n++] = *p;
        }
        p++;
    }
    ev->name[n] = 0;
    if (n == 0) {
        return 0;
    }
    while (ev->arg_count < KHOME_LIPC_MAX_ARGS) {
        char *dst = ev->args[ev->arg_count];
        size_t k = 0;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == 0 || *p == '\r' || *p == '\n') {
            break;
        }
        if (*p == '"') {
            p++;
            while (*p != 0 && *p != '"') {
                if (k + 1 < KHOME_LIPC_ARG_LEN) {
                    dst[k++] = *p;
                }
                p++;
            }
            if (*p == '"') {
                p++;
            }
        } else {
            while (*p != 0 && *p != ' ' && *p != '\t' && *p != '\r' &&
                   *p != '\n') {
                if (k + 1 < KHOME_LIPC_ARG_LEN) {
                    dst[k++] = *p;
                }
                p++;
            }
        }
        dst[k] = 0;
        ev->arg_count++;
    }
    return 1;
}

static void set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

int lipc_watch_start(LipcWatch *w, const char *source, const char *events)
{
    int pipefd[2];
#ifdef __linux__
    pid_t parent = getpid();
#endif
    pid_t pid;

    if (w == NULL || source == NULL || events == NULL) {
        return -1;
    }
    memset(w, 0, sizeof(*w));
    w->fd = -1;
    if (pipe(pipefd) < 0) {
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        int devnull;
#ifdef __linux__
        /* If khome dies, the watcher must not survive with the wrapper's
         * flock on khome.pid still held. */
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() != parent) {
            _exit(1);
        }
#endif
        devnull = open("/dev/null", O_WRONLY);
        dup2(pipefd[1], STDOUT_FILENO);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
        }
        close(pipefd[0]);
        close(pipefd[1]);
        /* The wrapper's flock on khome.pid is fd 9.  An orphaned watcher must
         * not keep that lock after khome dies. */
        close(9);
        execl(LIPC_WAIT_EVENT, "lipc-wait-event", "-m", "-t", source, events,
              (char *)NULL);
        _exit(127);
    }
    close(pipefd[1]);
    set_nonblock(pipefd[0]);
    w->pid = pid;
    w->fd = pipefd[0];
    w->len = 0;
    snprintf(w->source, sizeof(w->source), "%s", source);
    snprintf(w->events, sizeof(w->events), "%s", events);
    return 0;
}

#define LIPC_STOP_WAIT_MS 1000
#define LIPC_STOP_POLL_MS 10

static void sleep_ms(int ms)
{
    struct timespec ts;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

void lipc_watch_stop(LipcWatch *w)
{
    int status;
    int i;

    if (w == NULL) {
        return;
    }
    if (w->pid > 0) {
        int reaped = 0;

        kill(w->pid, SIGTERM);
        for (i = 0; i < LIPC_STOP_WAIT_MS / LIPC_STOP_POLL_MS; i++) {
            pid_t got = waitpid(w->pid, &status, WNOHANG);
            if (got == w->pid || (got < 0 && errno == ECHILD)) {
                reaped = 1;
                break;
            }
            if (got < 0 && errno != EINTR) {
                reaped = 1;
                break;
            }
            sleep_ms(LIPC_STOP_POLL_MS);
        }
        if (!reaped) {
            kill(w->pid, SIGKILL);
            while (waitpid(w->pid, &status, 0) < 0) {
                if (errno != EINTR) {
                    break;
                }
            }
        }
        w->pid = 0;
    }
    if (w->fd >= 0) {
        close(w->fd);
        w->fd = -1;
    }
    w->len = 0;
}

int lipc_watch_read(LipcWatch *w, LipcEvent *ev)
{
    char line[1024];

    if (w == NULL || w->fd < 0) {
        return -1;
    }
    for (;;) {
        char *newline = strchr(w->buf, '\n');
        ssize_t got;

        if (newline != NULL) {
            size_t len = (size_t)(newline - w->buf);
            size_t consumed = len + 1;
            if (len >= sizeof(line)) {
                len = sizeof(line) - 1;
            }
            memcpy(line, w->buf, len);
            line[len] = 0;
            memmove(w->buf, w->buf + consumed, w->len - consumed);
            w->len -= consumed;
            w->buf[w->len] = 0;
            if (lipc_parse_line(line, ev)) {
                return 1;
            }
            /* Unparseable lines are noise; drop one and try the next so a
             * long read can still yield the event behind it. */
            continue;
        }
        if (w->len + 1 >= sizeof(w->buf)) {
            /* A line with no newline filled the buffer; drop it. */
            w->len = 0;
            w->buf[0] = 0;
        }
        got = read(w->fd, w->buf + w->len, sizeof(w->buf) - 1 - w->len);
        if (got > 0) {
            w->len += (size_t)got;
            w->buf[w->len] = 0;
            continue;
        }
        if (got == 0) {
            return -1;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
}

/* Waits for a child until the deadline, then kills and reaps it. */
static int reap_until(pid_t pid, int *status, int64_t deadline)
{
    for (;;) {
        pid_t got = waitpid(pid, status, WNOHANG);

        if (got == pid) {
            return 0;
        }
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (now_ms() >= deadline) {
            kill(pid, SIGKILL);
            while (waitpid(pid, status, 0) < 0) {
                if (errno != EINTR) {
                    return -1;
                }
            }
            return -1;
        }
        sleep_ms(10);
    }
}

static int run_capture(char *const argv[], char *out, size_t cap)
{
    int pipefd[2];
    pid_t pid;
    size_t used = 0;
    int status;
    int64_t deadline;
    int timed_out = 0;

    if (out == NULL || cap == 0) {
        return -1;
    }
    out[0] = 0;
    if (pipe(pipefd) < 0) {
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        dup2(pipefd[1], STDOUT_FILENO);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
        }
        close(pipefd[0]);
        close(pipefd[1]);
        execv(argv[0], argv);
        _exit(127);
    }
    close(pipefd[1]);
    deadline = now_ms() + LIPC_CMD_TIMEOUT_MS;
    while (used + 1 < cap) {
        struct pollfd pfd;
        int left = (int)(deadline - now_ms());
        int ready;
        ssize_t got;

        if (left <= 0) {
            timed_out = 1;
            break;
        }
        pfd.fd = pipefd[0];
        pfd.events = POLLIN;
        pfd.revents = 0;
        ready = poll(&pfd, 1, left);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (ready == 0) {
            timed_out = 1;
            break;
        }
        got = read(pipefd[0], out + used, cap - 1 - used);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (got == 0) {
            break;
        }
        used += (size_t)got;
    }
    out[used] = 0;
    if (timed_out) {
        kill(pid, SIGKILL);
    }
    close(pipefd[0]);
    if (reap_until(pid, &status, deadline) < 0) {
        return -1;
    }
    if (timed_out) {
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return -1;
    }
    return 0;
}

static int run_wait(char *const argv[])
{
    pid_t pid;
    int status;
    int64_t deadline;

    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
        }
        execv(argv[0], argv);
        _exit(127);
    }
    deadline = now_ms() + LIPC_CMD_TIMEOUT_MS;
    for (;;) {
        pid_t got = waitpid(pid, &status, WNOHANG);

        if (got == pid) {
            break;
        }
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (now_ms() >= deadline) {
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0) {
                if (errno != EINTR) {
                    break;
                }
            }
            return -1;
        }
        sleep_ms(10);
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return -1;
    }
    return 0;
}

int lipc_get_int(const char *source, const char *prop, int *out)
{
    char *argv[] = { (char *)LIPC_GET_PROP, (char *)"-i", (char *)"-q",
                     (char *)source, (char *)prop, NULL };
    char buf[64];
    char *end = NULL;
    long value;

    if (run_capture(argv, buf, sizeof(buf)) < 0) {
        return -1;
    }
    value = strtol(buf, &end, 10);
    if (end == buf) {
        return -1;
    }
    if (out != NULL) {
        *out = (int)value;
    }
    return 0;
}

int lipc_get_str(const char *source, const char *prop, char *out, size_t cap)
{
    char *argv[] = { (char *)LIPC_GET_PROP, (char *)"-s", (char *)"-q",
                     (char *)source, (char *)prop, NULL };
    char buf[512];
    size_t len;

    if (out == NULL || cap == 0) {
        return -1;
    }
    if (run_capture(argv, buf, sizeof(buf)) < 0) {
        return -1;
    }
    len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
                       buf[len - 1] == ' ')) {
        buf[--len] = 0;
    }
    if (len >= cap) {
        len = cap - 1;
    }
    memcpy(out, buf, len);
    out[len] = 0;
    return 0;
}

int lipc_set_str(const char *source, const char *prop, const char *value)
{
    char *argv[] = { (char *)LIPC_SET_PROP, (char *)source, (char *)prop,
                     (char *)value, NULL };
    return run_wait(argv);
}

int lipc_set_int(const char *source, const char *prop, int value)
{
    char buf[24];
    char *argv[] = { (char *)LIPC_SET_PROP, (char *)"-i", (char *)source,
                     (char *)prop, buf, NULL };

    snprintf(buf, sizeof(buf), "%d", value);
    return run_wait(argv);
}
