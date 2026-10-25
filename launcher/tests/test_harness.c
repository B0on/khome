#include "test_harness.h"

#include <stdarg.h>

int kh_checks_passed = 0;
int kh_checks_failed = 0;
int kh_tests_passed = 0;
int kh_tests_failed = 0;

static const char *kh_current_test = "";

void kh_fail(const char *file, int line, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "FAIL %s (%s:%d): ", kh_current_test, file, line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void kh_run_test(const char *name, void (*fn)(void))
{
    int failed_before = kh_checks_failed;
    kh_current_test = name;
    fn();
    kh_current_test = "";
    if (kh_checks_failed == failed_before) {
        kh_tests_passed++;
        printf("PASS  %s\n", name);
    } else {
        kh_tests_failed++;
        printf("FAIL  %s\n", name);
    }
}

void kh_summary(void)
{
    printf("\nunit tests: %d passed, %d failed "
           "(%d checks passed, %d checks failed)\n",
           kh_tests_passed, kh_tests_failed, kh_checks_passed,
           kh_checks_failed);
}
