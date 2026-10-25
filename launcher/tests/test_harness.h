#ifndef KHOME_TEST_HARNESS_H
#define KHOME_TEST_HARNESS_H

/* Minimal assertion harness for the khome host unit tests.  Every test is a
 * void function registered with kh_run_test(); failures print file:line and
 * the expected/actual detail, and the process exits non-zero if any test
 * failed. */

#include <math.h>
#include <stdio.h>

extern int kh_checks_passed;
extern int kh_checks_failed;
extern int kh_tests_passed;
extern int kh_tests_failed;

void kh_fail(const char *file, int line, const char *fmt, ...);
void kh_run_test(const char *name, void (*fn)(void));
void kh_summary(void);

#define KH_CHECK(cond)                                                      \
    do {                                                                    \
        if (cond) {                                                         \
            kh_checks_passed++;                                             \
        } else {                                                            \
            kh_checks_failed++;                                             \
            kh_fail(__FILE__, __LINE__, "check failed: %s", #cond);         \
        }                                                                   \
    } while (0)

#define KH_CHECK_MSG(cond, ...)                                             \
    do {                                                                    \
        if (cond) {                                                         \
            kh_checks_passed++;                                             \
        } else {                                                            \
            kh_checks_failed++;                                             \
            kh_fail(__FILE__, __LINE__, __VA_ARGS__);                       \
        }                                                                   \
    } while (0)

#define KH_CHECK_NEAR(actual, expected, eps, label)                         \
    do {                                                                    \
        double kh_a = (double)(actual);                                     \
        double kh_e = (double)(expected);                                   \
        KH_CHECK_MSG(fabs(kh_a - kh_e) <= (eps),                            \
                     "%s: expected %.3f, got %.3f", (label), kh_e, kh_a);   \
    } while (0)

#endif
