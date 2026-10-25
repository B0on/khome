#include "suites.h"

#include <stddef.h>
#include <string.h>

#include "lipc.h"
#include "test_harness.h"

/* lipc-wait-event prints either
 *   [HH:MM:SS.ffffff] event "arg" "arg" 0
 * with -t, or the same line without the timestamp.  See
 * docs/launcher-architecture.md, "第 2 段階の実測メモ". */

static void test_timestamped_line(void)
{
    LipcEvent ev;

    KH_CHECK(lipc_parse_line(
        "[04:05:06.123456] appStarted \"com.lab126.booklet.home\" "
        "\"second arg\" 0",
        &ev));
    KH_CHECK_MSG(strcmp(ev.name, "appStarted") == 0,
                 "expected name appStarted, got \"%s\"", ev.name);
    KH_CHECK_MSG(ev.arg_count == 3, "expected 3 args, got %d", ev.arg_count);
    if (ev.arg_count == 3) {
        KH_CHECK_MSG(strcmp(ev.args[0], "com.lab126.booklet.home") == 0,
                     "arg 0: expected com.lab126.booklet.home, got \"%s\"",
                     ev.args[0]);
        KH_CHECK_MSG(strcmp(ev.args[1], "second arg") == 0,
                     "arg 1: expected \"second arg\", got \"%s\"", ev.args[1]);
        KH_CHECK_MSG(strcmp(ev.args[2], "0") == 0,
                     "arg 2: expected 0, got \"%s\"", ev.args[2]);
    }
}

static void test_untimestamped_line(void)
{
    LipcEvent ev;

    KH_CHECK(lipc_parse_line("outOfScreenSaver 0", &ev));
    KH_CHECK_MSG(strcmp(ev.name, "outOfScreenSaver") == 0,
                 "expected name outOfScreenSaver, got \"%s\"", ev.name);
    KH_CHECK_MSG(ev.arg_count == 1, "expected 1 arg, got %d", ev.arg_count);
    if (ev.arg_count == 1) {
        KH_CHECK_MSG(strcmp(ev.args[0], "0") == 0,
                     "arg 0: expected 0, got \"%s\"", ev.args[0]);
    }
}

static void test_quoted_and_numeric_args(void)
{
    LipcEvent ev;

    KH_CHECK(lipc_parse_line("custom \"a b\" 42 \"quoted number\"", &ev));
    KH_CHECK_MSG(strcmp(ev.name, "custom") == 0,
                 "expected name custom, got \"%s\"", ev.name);
    KH_CHECK_MSG(ev.arg_count == 3, "expected 3 args, got %d", ev.arg_count);
    if (ev.arg_count == 3) {
        KH_CHECK_MSG(strcmp(ev.args[0], "a b") == 0,
                     "quoted arg: expected \"a b\", got \"%s\"", ev.args[0]);
        KH_CHECK_MSG(strcmp(ev.args[1], "42") == 0,
                     "numeric arg: expected 42, got \"%s\"", ev.args[1]);
        KH_CHECK_MSG(strcmp(ev.args[2], "quoted number") == 0,
                     "quoted numeric arg: expected \"quoted number\", got "
                     "\"%s\"", ev.args[2]);
    }
}

static void test_leading_whitespace_and_crlf(void)
{
    LipcEvent ev;

    KH_CHECK(lipc_parse_line(" \t[12:00:00.000001] powerd 0 1\r\n", &ev));
    KH_CHECK_MSG(strcmp(ev.name, "powerd") == 0,
                 "expected name powerd, got \"%s\"", ev.name);
    KH_CHECK_MSG(ev.arg_count == 2, "expected 2 args, got %d", ev.arg_count);
    if (ev.arg_count == 2) {
        KH_CHECK_MSG(strcmp(ev.args[0], "0") == 0,
                     "arg 0: expected 0, got \"%s\"", ev.args[0]);
        KH_CHECK_MSG(strcmp(ev.args[1], "1") == 0,
                     "arg 1: expected 1, got \"%s\"", ev.args[1]);
    }
}

static void test_broken_lines(void)
{
    LipcEvent ev;

    KH_CHECK_MSG(!lipc_parse_line(NULL, &ev), "NULL line must be rejected");
    KH_CHECK_MSG(!lipc_parse_line("outOfScreenSaver 0", NULL),
                 "NULL output must be rejected");
    KH_CHECK_MSG(!lipc_parse_line("", &ev), "empty line must be rejected");
    KH_CHECK_MSG(!lipc_parse_line(" \t\r\n", &ev),
                 "whitespace-only line must be rejected");
    KH_CHECK_MSG(!lipc_parse_line("[12:00:00.000000", &ev),
                 "timestamp without a closing bracket must be rejected");
    KH_CHECK_MSG(!lipc_parse_line("[12:00:00.000000]", &ev),
                 "timestamp without an event name must be rejected");
}

static void test_arg_limit(void)
{
    LipcEvent ev;

    KH_CHECK(lipc_parse_line("event a b c d e", &ev));
    KH_CHECK_MSG(ev.arg_count == KHOME_LIPC_MAX_ARGS,
                 "expected at most %d args, got %d", KHOME_LIPC_MAX_ARGS,
                 ev.arg_count);
    if (ev.arg_count == KHOME_LIPC_MAX_ARGS) {
        KH_CHECK_MSG(strcmp(ev.args[0], "a") == 0 &&
                         strcmp(ev.args[1], "b") == 0 &&
                         strcmp(ev.args[2], "c") == 0,
                     "extra args must be ignored after the first three");
    }
}

void kh_run_lipc_parse_tests(void)
{
    printf("\n[lipc]\n");
    kh_run_test("lipc.timestamped_line", test_timestamped_line);
    kh_run_test("lipc.untimestamped_line", test_untimestamped_line);
    kh_run_test("lipc.quoted_and_numeric_args", test_quoted_and_numeric_args);
    kh_run_test("lipc.leading_whitespace_and_crlf",
                test_leading_whitespace_and_crlf);
    kh_run_test("lipc.broken_lines", test_broken_lines);
    kh_run_test("lipc.arg_limit", test_arg_limit);
}
