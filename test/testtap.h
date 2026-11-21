/*
 * testtap.h: report unit test results in the Test Anything Protocol
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#ifndef XP3_TESTTAP_H
#define XP3_TESTTAP_H

/*
 * Each test function becomes one TAP test, so that Meson's JUnit report
 * lists the functions one by one. main() runs every test function with
 * XP3_TEST_RUN() and returns xp3TestDone(). Failure messages are TAP
 * diagnostics ("# " lines) on stdout, which must carry nothing else.
 * Not thread-safe: check results in the main thread only.
 */

/* Count a failure and print "# FAIL: what" unless cond is true. */
void
xp3TestCheck(int cond, const char *what);

/* Count a failure and print the formatted message as "# FAIL: ". */
void
xp3TestFail(const char *format, ...)
#ifdef __GNUC__
__attribute__((format(printf, 1, 2)))
#endif
;

/* The number of failures so far, in all tests. */
int
xp3TestFailures(void);

/* Report the running test as skipped, with the reason, unless it also
 * fails. The test function should return after calling it. */
void
xp3TestSkip(const char *reason);

/* Run one test function and report "ok" or "not ok" under the name. */
void
xp3TestRun(const char *name, void (*test)(void));

#define XP3_TEST_RUN(test) xp3TestRun(#test, test)

/* Print the plan and return the exit status: 1 if anything failed,
 * also outside a test, else 0. */
int
xp3TestDone(void);

#endif /* XP3_TESTTAP_H */
