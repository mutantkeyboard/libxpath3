/*
 * testerror.c: tests for error objects
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdio.h>
#include <string.h>

#include <libxml/xmlerror.h>

#include <libxpath3/xpath3.h>
#include "private/xp3error.h"
#include "testalloc.h"

static int failures = 0;

static void
check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static int
strEq(const xmlChar *a, const char *b) {
    if ((a == NULL) || (b == NULL))
        return((a == NULL) && (b == NULL));
    return(strcmp((const char *) a, b) == 0);
}

static void
testCodeNames(void) {
    check(strcmp(xp3ErrCodeName(XP3_ERR_XPST0003), "XPST0003") == 0,
          "name of XPST0003");
    check(strcmp(xp3ErrCodeName(XP3_ERR_FORG0001), "FORG0001") == 0,
          "name of FORG0001");
    check(xp3ErrCodeName(XP3_ERR_USER) == NULL,
          "XP3_ERR_USER has no fixed name");
    check(xp3ErrCodeName((xp3ErrCode) 9999) == NULL,
          "unknown code has no name");
}

static void
testNew(void) {
    xp3Error *err;

    err = xp3ErrorNew(XP3_ERR_XPST0003, 2, 7, "unexpected token '%s'", "]");
    check(err != xp3ErrorNoMemory(), "error allocated");
    check(strEq(xp3ErrorCode(err), "XPST0003"), "code of new error");
    check(strEq(xp3ErrorNamespace(err), XP3_ERR_NS), "W3C namespace");
    check(strEq(xp3ErrorMessage(err), "unexpected token ']'"),
          "formatted message");
    check(xp3ErrorLine(err) == 2, "line");
    check(xp3ErrorColumn(err) == 7, "column");
    xp3ErrorFree(err);

    err = xp3ErrorNew(XP3_ERR_FOAR0001, 0, 0, NULL);
    check(xp3ErrorMessage(err) == NULL, "error without message");
    check(xp3ErrorLine(err) == 0 && xp3ErrorColumn(err) == 0,
          "unknown position is 0");
    xp3ErrorFree(err);
}

static void
testUser(void) {
    xp3Error *err;

    err = xp3ErrorNewUser(BAD_CAST "urn:example", BAD_CAST "E42",
                          BAD_CAST "custom failure");
    check(strEq(xp3ErrorCode(err), "E42"), "user code");
    check(strEq(xp3ErrorNamespace(err), "urn:example"), "user namespace");
    check(strEq(xp3ErrorMessage(err), "custom failure"), "user message");
    xp3ErrorFree(err);

    err = xp3ErrorNewUser(NULL, NULL, NULL);
    check(strEq(xp3ErrorCode(err), "FOER0000"), "default code FOER0000");
    check(strEq(xp3ErrorNamespace(err), XP3_ERR_NS), "default code namespace");
    xp3ErrorFree(err);

    err = xp3ErrorNewUser(NULL, BAD_CAST "local", NULL);
    check(xp3ErrorNamespace(err) == NULL, "code without namespace");
    xp3ErrorFree(err);
}

static void
testNoMemoryAndNull(void) {
    xp3Error *oom = xp3ErrorNoMemory();

    check(strEq(xp3ErrorCode(oom), "NO_MEMORY"), "out-of-memory code");
    check(xp3ErrorNamespace(oom) == NULL, "out-of-memory has no namespace");
    check(xp3ErrorMessage(oom) != NULL, "out-of-memory message");
    xp3ErrorFree(oom);    /* must be ignored */
    check(xp3ErrorNoMemory() == oom, "out-of-memory object survives free");

    xp3ErrorFree(NULL);
    check(xp3ErrorCode(NULL) == NULL, "code of NULL");
    check(xp3ErrorNamespace(NULL) == NULL, "namespace of NULL");
    check(xp3ErrorMessage(NULL) == NULL, "message of NULL");
    check(xp3ErrorLine(NULL) == 0 && xp3ErrorColumn(NULL) == 0,
          "position of NULL");
}

static void
testAllocationFailures(void) {
    int i;

    xp3TestAllocInstall();

    /* Fail the 1st, 2nd, ... allocation until a run completes without
     * an injected failure. Every run must return either a complete
     * error or the shared out-of-memory error, without leaks. */
    for (i = 0; i < 100; i++) {
        xp3Error *err;
        char expected[32];

        snprintf(expected, sizeof(expected), "message %d", i);
        xp3TestAllocFailAt(i);
        err = xp3ErrorNew(XP3_ERR_XPST0003, 1, 1, "message %d", i);
        if (xp3TestAllocFailed()) {
            check(err == xp3ErrorNoMemory(),
                  "xp3ErrorNew returns out-of-memory on failure");
        } else {
            check(strEq(xp3ErrorCode(err), "XPST0003") &&
                  strEq(xp3ErrorMessage(err), expected),
                  "xp3ErrorNew complete after allocation failures");
            xp3ErrorFree(err);
            break;
        }
        xp3ErrorFree(err);
    }
    check(i > 0 && i < 100, "xp3ErrorNew failure points exercised");

    for (i = 0; i < 100; i++) {
        xp3Error *err;

        xp3TestAllocFailAt(i);
        err = xp3ErrorNewUser(BAD_CAST "urn:x", BAD_CAST "E1", BAD_CAST "m");
        if (xp3TestAllocFailed()) {
            check(err == xp3ErrorNoMemory(),
                  "xp3ErrorNewUser returns out-of-memory on failure");
        } else {
            check(strEq(xp3ErrorCode(err), "E1") &&
                  strEq(xp3ErrorNamespace(err), "urn:x") &&
                  strEq(xp3ErrorMessage(err), "m"),
                  "xp3ErrorNewUser complete after allocation failures");
            xp3ErrorFree(err);
            break;
        }
        xp3ErrorFree(err);
    }
    check(i > 0 && i < 100, "xp3ErrorNewUser failure points exercised");

    xp3TestAllocRestore();
}

static int reportCalls;
static int reportDomain;
static int reportColumn;
static char reportCode[32];

static void
reportHandler(void *userData, const xmlError *error) {
    (void) userData;
    reportCalls++;
    reportDomain = error->domain;
    reportColumn = error->int2;
    snprintf(reportCode, sizeof(reportCode), "%s",
             error->str1 ? error->str1 : "");
}

static void
testReport(void) {
    xp3Error *err = xp3ErrorNew(XP3_ERR_XPTY0004, 3, 14, "type error");

    xp3ErrorReport(err, (xmlStructuredErrorFunc) reportHandler, NULL);
    check(reportCalls == 1, "handler called once");
    check(reportDomain == XML_FROM_XPATH, "XPath domain");
    check(strcmp(reportCode, "XPTY0004") == 0, "code passed in str1");
    check(reportColumn == 14, "column passed in int2");

    xp3ErrorReport(err, NULL, NULL);    /* must not crash */
    xp3ErrorReport(NULL, (xmlStructuredErrorFunc) reportHandler, NULL);
    check(reportCalls == 1, "NULL error is not reported");
    xp3ErrorFree(err);
}

int
main(void) {
    testCodeNames();
    testNew();
    testUser();
    testNoMemoryAndNull();
    testAllocationFailures();
    testReport();

    if (failures == 0)
        printf("testerror: OK\n");
    return(failures == 0 ? 0 : 1);
}
