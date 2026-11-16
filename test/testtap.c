/*
 * testtap.c: report unit test results in the Test Anything Protocol
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "testtap.h"

static int testCount = 0;
static int failures = 0;
static const char *skipReason = NULL;

/* Print text as a TAP diagnostic, with "# " before every line. */
static void
printDiagnostic(const char *prefix, const char *text) {
    const char *cur;

    printf("# %s", prefix);
    for (cur = text; *cur != 0; cur++) {
        putchar(*cur);
        if ((*cur == '\n') && (cur[1] != 0))
            printf("# ");
    }
    putchar('\n');
}

void
xp3TestCheck(int cond, const char *what) {
    if (!cond)
        xp3TestFail("%s", what);
}

/*
 * The message is formatted with malloc(), not xmlMalloc(), because
 * checks also run while testalloc.c makes libxml2's allocations fail.
 */
void
xp3TestFail(const char *format, ...) {
    va_list args;
    va_list copy;
    char *message = NULL;
    int len;

    failures++;
    va_start(args, format);
    va_copy(copy, args);
    len = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (len >= 0)
        message = malloc((size_t) len + 1);
    if (message != NULL)
        vsnprintf(message, (size_t) len + 1, format, args);
    va_end(args);

    if (message == NULL) {
        printDiagnostic("FAIL: ", format);
        return;
    }
    printDiagnostic("FAIL: ", message);
    free(message);
}

int
xp3TestFailures(void) {
    return(failures);
}

void
xp3TestSkip(const char *reason) {
    skipReason = reason;
}

void
xp3TestRun(const char *name, void (*test)(void)) {
    int failuresBefore = failures;

    skipReason = NULL;
    test();
    testCount++;
    if (failures > failuresBefore) {
        printf("not ok %d - %s\n", testCount, name);
    } else if (skipReason != NULL) {
        printf("ok %d - %s # SKIP %s\n", testCount, name, skipReason);
    } else {
        printf("ok %d - %s\n", testCount, name);
    }
    /* Keep the results that came before a crash */
    fflush(stdout);
    skipReason = NULL;
}

int
xp3TestDone(void) {
    printf("1..%d\n", testCount);
    if (failures > 0)
        return(1);
    return(0);
}
