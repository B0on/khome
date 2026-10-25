#ifndef KHOME_LIPC_H
#define KHOME_LIPC_H

#include <stddef.h>
#include <sys/types.h>

#define KHOME_LIPC_MAX_ARGS 3
#define KHOME_LIPC_ARG_LEN 96

/* One parsed line of lipc-wait-event output.  The `-t` timestamp prefix is
 * optional, so this parser accepts both forms. */
typedef struct {
    char name[48];
    char args[KHOME_LIPC_MAX_ARGS][KHOME_LIPC_ARG_LEN];
    int arg_count;
} LipcEvent;

/* Pure: line -> event.  Returns 1 on a complete event line. */
int lipc_parse_line(const char *line, LipcEvent *ev);

/* A lipc-wait-event child plus its pipe. */
typedef struct {
    pid_t pid;
    int fd;
    char source[64];
    char events[128];
    char buf[1024];
    size_t len;
} LipcWatch;

int lipc_watch_start(LipcWatch *w, const char *source, const char *events);
void lipc_watch_stop(LipcWatch *w);
/* 1 when a full line was parsed, 0 when no line is ready, -1 after the child
 * closed the pipe. */
int lipc_watch_read(LipcWatch *w, LipcEvent *ev);

int lipc_get_int(const char *source, const char *prop, int *out);
int lipc_get_str(const char *source, const char *prop, char *out, size_t cap);
int lipc_set_str(const char *source, const char *prop, const char *value);
int lipc_set_int(const char *source, const char *prop, int value);

#endif
