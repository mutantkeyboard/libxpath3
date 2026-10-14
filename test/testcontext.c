/*
 * testcontext.c: tests for the static context
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdio.h>
#include <string.h>

#include <libxml/parser.h>

#include <libxpath3/xpath3.h>
#include "private/xp3context.h"
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
testDefaults(void) {
    xp3StaticContext *ctx = xp3NewStaticContext();

    check(ctx != NULL, "context created");
    if (ctx == NULL)
        return;
    check(ctx->level == XP3_XPATH_31, "default language level is 3.1");
    check(ctx->maxExprLength == XP3_DEFAULT_MAX_EXPR_LENGTH,
          "default expression length limit");
    check(ctx->maxParseDepth == XP3_DEFAULT_MAX_PARSE_DEPTH,
          "default parse depth limit");

    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "xml"), XP3_NS_XML),
          "xml predeclared");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "xs"), XP3_NS_XS),
          "xs predeclared");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "xsi"), XP3_NS_XSI),
          "xsi predeclared");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "fn"), XP3_NS_FN),
          "fn predeclared");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "math"), XP3_NS_MATH),
          "math predeclared");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "map"), XP3_NS_MAP),
          "map predeclared");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "array"), XP3_NS_ARRAY),
          "array predeclared");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "err"), XP3_ERR_NS),
          "err predeclared");
    check(xp3StaticContextLookupNs(ctx, BAD_CAST "foo") == NULL,
          "unknown prefix is unbound");

    xp3FreeStaticContext(ctx);
    xp3FreeStaticContext(NULL);    /* must be ignored */
}

static void
testLanguageLevel(void) {
    xp3StaticContext *ctx = xp3NewStaticContext();

    check(xp3SetStaticContextLanguageLevel(ctx, XP3_XPATH_20) == 0,
          "level 2.0 accepted");
    check(ctx->level == XP3_XPATH_20, "level 2.0 stored");
    check(xp3SetStaticContextLanguageLevel(ctx, XP3_XPATH_30) == 0,
          "level 3.0 accepted");
    check(xp3SetStaticContextLanguageLevel(ctx, XP3_XPATH_31) == 0,
          "level 3.1 accepted");

    check(xp3SetStaticContextLanguageLevel(ctx, (xp3LanguageLevel) 0) < 0,
          "level 0 rejected");
    check(xp3SetStaticContextLanguageLevel(ctx, (xp3LanguageLevel) 21) < 0,
          "level 21 rejected");
    check(xp3SetStaticContextLanguageLevel(ctx, (xp3LanguageLevel) 40) < 0,
          "level 40 rejected");
    check(ctx->level == XP3_XPATH_31, "rejected level leaves level unchanged");

    check(xp3SetStaticContextLanguageLevel(NULL, XP3_XPATH_31) < 0,
          "NULL context rejected");

    xp3FreeStaticContext(ctx);
}

static void
testDeclareNamespace(void) {
    xp3StaticContext *ctx = xp3NewStaticContext();

    check(xp3DeclareNamespace(ctx, BAD_CAST "ex", BAD_CAST "urn:example") == 0,
          "declare new prefix");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "ex"), "urn:example"),
          "new prefix bound");

    check(xp3DeclareNamespace(ctx, BAD_CAST "ex", BAD_CAST "urn:other") == 0,
          "override prefix");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "ex"), "urn:other"),
          "override replaces the binding");

    check(xp3DeclareNamespace(ctx, BAD_CAST "fn", BAD_CAST "urn:mine") == 0,
          "predeclared prefix can be rebound");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "fn"), "urn:mine"),
          "predeclared prefix rebound");

    check(xp3DeclareNamespace(ctx, BAD_CAST "ex", BAD_CAST "") == 0,
          "empty URI removes binding");
    check(xp3StaticContextLookupNs(ctx, BAD_CAST "ex") == NULL,
          "binding removed");
    check(xp3DeclareNamespace(ctx, BAD_CAST "ex", NULL) == 0,
          "removing an unbound prefix succeeds");

    check(xp3DeclareNamespace(ctx, BAD_CAST "xml", BAD_CAST XP3_NS_XML) == 0,
          "xml bound to its own namespace is accepted");
    check(xp3DeclareNamespace(ctx, BAD_CAST "xml", BAD_CAST "urn:x") < 0,
          "xml bound elsewhere is rejected");
    check(xp3DeclareNamespace(ctx, BAD_CAST "xml", NULL) < 0,
          "xml cannot be removed");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "xml"), XP3_NS_XML),
          "xml binding unchanged");
    check(xp3DeclareNamespace(ctx, BAD_CAST "xmlns", BAD_CAST "urn:x") < 0,
          "xmlns rejected");
    check(xp3DeclareNamespace(ctx, BAD_CAST "", BAD_CAST "urn:x") < 0,
          "empty prefix rejected");
    check(xp3DeclareNamespace(ctx, BAD_CAST "1abc", BAD_CAST "urn:x") < 0,
          "invalid NCName rejected");
    check(xp3DeclareNamespace(ctx, BAD_CAST "a:b", BAD_CAST "urn:x") < 0,
          "prefix with colon rejected");

    check(xp3DeclareNamespace(NULL, BAD_CAST "p", BAD_CAST "urn:x") < 0,
          "NULL context rejected");
    check(xp3DeclareNamespace(ctx, NULL, BAD_CAST "urn:x") < 0,
          "NULL prefix rejected");
    check(xp3StaticContextLookupNs(NULL, BAD_CAST "xs") == NULL,
          "lookup with NULL context");
    check(xp3StaticContextLookupNs(ctx, NULL) == NULL,
          "lookup with NULL prefix");

    xp3FreeStaticContext(ctx);
}

static void
testAllocationFailures(void) {
    int i;

    if (!xp3TestAllocInstall()) {
        printf("testcontext: allocation-failure tests skipped: this libxml2 "
               "ignores xmlMemSetup()\n");
        xp3TestAllocRestore();
        return;
    }

    /* Creating a context: every failure must return NULL without leaks. */
    for (i = 0; i < 1000; i++) {
        xp3StaticContext *ctx;

        xp3TestAllocFailAt(i);
        ctx = xp3NewStaticContext();
        if (!xp3TestAllocFailed()) {
            check(ctx != NULL &&
                  strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "err"),
                        XP3_ERR_NS),
                  "context complete after allocation failures");
            xp3FreeStaticContext(ctx);
            break;
        }
        check(ctx == NULL, "xp3NewStaticContext returns NULL on failure");
        xp3FreeStaticContext(ctx);
    }
    check(i > 0 && i < 1000, "xp3NewStaticContext failure points exercised");

    /* Declaring: a failure returns -1 and keeps the previous binding. */
    for (i = 0; i < 1000; i++) {
        xp3StaticContext *ctx;
        int ret;

        xp3TestAllocFailAt(-1);
        ctx = xp3NewStaticContext();
        if (ctx == NULL) {
            check(0, "context for declare test");
            break;
        }
        xp3DeclareNamespace(ctx, BAD_CAST "p", BAD_CAST "urn:old");

        xp3TestAllocFailAt(i);
        ret = xp3DeclareNamespace(ctx, BAD_CAST "p", BAD_CAST "urn:new");
        if (!xp3TestAllocFailed()) {
            check(ret == 0 &&
                  strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "p"), "urn:new"),
                  "declare complete after allocation failures");
            xp3TestAllocFailAt(-1);
            xp3FreeStaticContext(ctx);
            break;
        }
        check(ret < 0, "xp3DeclareNamespace returns -1 on failure");
        check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "p"), "urn:old"),
              "previous binding kept on failure");
        xp3TestAllocFailAt(-1);
        xp3FreeStaticContext(ctx);
    }
    check(i > 0 && i < 1000, "xp3DeclareNamespace failure points exercised");

    xp3TestAllocRestore();
}

int
main(void) {
    xmlInitParser();

    testDefaults();
    testLanguageLevel();
    testDeclareNamespace();
    testAllocationFailures();

    if (failures == 0)
        printf("testcontext: OK\n");
    return(failures == 0 ? 0 : 1);
}
