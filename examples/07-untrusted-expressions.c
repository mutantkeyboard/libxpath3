/*
 * 07-untrusted-expressions.c: limits, error codes and the language
 * level, for expressions that come from users
 *
 * The program of doc/tutorial/07-untrusted-expressions.md. Run it with
 * the directory of the example documents:
 *
 *     07-untrusted-expressions examples/data
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/tree.h>
#include <libxpath3/xpath3.h>

#include "common.h"

/* The limits of every evaluation in this program */
#define MAX_STEPS 1000000
#define MAX_ITEMS 100000

/*
 * Build an expression: prefix repeated count times, then middle, then
 * suffix repeated count times. Returns a string to free, or NULL.
 */
static char *
repeatAround(const char *prefix, const char *middle, const char *suffix,
             size_t count) {
    size_t prefixLength = strlen(prefix);
    size_t middleLength = strlen(middle);
    size_t suffixLength = strlen(suffix);
    char *text;
    char *end;
    size_t i;

    text = malloc(count * (prefixLength + suffixLength) + middleLength + 1);
    if (text == NULL)
        return(NULL);
    end = text;
    for (i = 0; i < count; i++) {
        memcpy(end, prefix, prefixLength);
        end += prefixLength;
    }
    memcpy(end, middle, middleLength);
    end += middleLength;
    for (i = 0; i < count; i++) {
        memcpy(end, suffix, suffixLength);
        end += suffixLength;
    }
    *end = 0;
    return(text);
}

/*
 * Build "string-length('xxx...')", with a literal of the given length.
 */
static char *
longLiteral(size_t length) {
    static const char start[] = "string-length('";
    static const char end[] = "')";
    char *text;

    text = malloc(sizeof(start) - 1 + length + sizeof(end));
    if (text == NULL)
        return(NULL);
    memcpy(text, start, sizeof(start) - 1);
    memset(text + sizeof(start) - 1, 'x', length);
    memcpy(text + sizeof(start) - 1 + length, end, sizeof(end));
    return(text);
}

/*
 * Build "let $s0 := 'ab', $s1 := concat($s0, $s0), ... return
 * string-length($sN)": a string that doubles N times.
 */
static char *
doubling(int times) {
    size_t size = 64 + (size_t) times * 64;
    char *text = malloc(size);
    size_t used;
    int i;

    if (text == NULL)
        return(NULL);
    used = (size_t) snprintf(text, size, "let $s0 := 'ab'");
    for (i = 1; i <= times; i++)
        used += (size_t) snprintf(text + used, size - used,
                                  ", $s%d := concat($s%d, $s%d)",
                                  i, i - 1, i - 1);
    snprintf(text + used, size - used, " return string-length($s%d)", times);
    return(text);
}

/*
 * Run an expression from outside, then show that the same contexts
 * still evaluate an ordinary one. An expression that couldn't be
 * built counts as a failure.
 */
static int
untrusted(const xp3StaticContext *sctx, xp3DynamicContext *dctx,
          const char *label, const char *expression,
          const char *expectedError) {
    int failures = 0;

    if (expression == NULL)
        return(1);
    failures += exRun(sctx, dctx, label, expression, expectedError);
    failures += exRun(sctx, dctx, "  afterwards count(//book)",
                      "count(//book)", NULL);
    return(failures);
}

/*
 * The checks the library can't make, because xp3Compile() takes a
 * NUL-terminated string: the bytes received must not contain a NUL,
 * which would end the expression early.
 */
static int
acceptExpression(const char *bytes, size_t length) {
    if (memchr(bytes, 0, length) != NULL)
        return(-1);
    return(0);
}

static int
sizeAndDepth(const xp3StaticContext *sctx, xp3DynamicContext *dctx) {
    char *text;
    int failures = 0;

    /* Nesting is limited when parsing, before any stack is at risk */
    text = repeatAround("(", "1", ")", 100000);
    failures += untrusted(sctx, dctx, "100,000 nested parentheses", text,
                          "LIMIT");
    free(text);

    text = repeatAround("1+", "1", "", 99999);
    failures += untrusted(sctx, dctx, "1+1+...+1 with 100,000 terms", text,
                          "LIMIT");
    free(text);

    text = longLiteral(2 * 1024 * 1024);
    failures += untrusted(sctx, dctx, "string-length() of a 2 MB literal",
                          text, "LIMIT");
    free(text);
    return(failures);
}

static int
workAndMemory(const xp3StaticContext *sctx, xp3DynamicContext *dctx) {
    char *text;
    int failures = 0;

    /* A range is not stored item by item... */
    failures += untrusted(sctx, dctx, NULL, "count(1 to 1000000000000)",
                          NULL);
    /* ... but filtering it builds a sequence */
    failures += untrusted(sctx, dctx, NULL,
                          "(1 to 1000000000000)[. mod 7 = 0][last()]",
                          "LIMIT");
    failures += untrusted(sctx, dctx, NULL,
                          "count(for $a in 1 to 100000, $b in 1 to 100000 "
                          "return 1)", "LIMIT");

    text = doubling(60);
    failures += untrusted(sctx, dctx, "a string doubled 60 times", text,
                          "LIMIT");
    free(text);

    failures += untrusted(sctx, dctx, NULL,
                          "matches(string-join((1 to 30) ! 'a') || 'b', "
                          "'^(a+)+\\1$')", "LIMIT");
    return(failures);
}

static int
badBytes(const xp3StaticContext *sctx, xp3DynamicContext *dctx) {
    static const char withNul[] = "1 + 1\0 + garbage";
    int failures = 0;

    /* xp3Compile() reads up to the NUL and never sees the rest */
    failures += untrusted(sctx, dctx, "1 + 1, a NUL and more", withNul,
                          NULL);
    if (acceptExpression(withNul, sizeof(withNul) - 1) < 0)
        printf("1 + 1, a NUL and more -> rejected before compiling\n");
    else
        failures++;

    /* Bytes that are not UTF-8 */
    failures += untrusted(sctx, dctx, "'\\xC3\\x28', invalid UTF-8",
                          "'\xC3\x28'", "XPST0003");
    failures += untrusted(sctx, dctx, "'\\xC0\\xAF', an overlong /",
                          "'\xC0\xAF'", "XPST0003");
    return(failures);
}

static int
outsideTheProcess(const xp3StaticContext *sctx, xp3DynamicContext *dctx) {
    int failures = 0;

    failures += untrusted(sctx, dctx, NULL, "doc('file:///etc/passwd')",
                          "UNSUPPORTED");
    failures += untrusted(sctx, dctx, NULL,
                          "unparsed-text('file:///etc/passwd')",
                          "UNSUPPORTED");
    failures += untrusted(sctx, dctx, NULL, "environment-variable('HOME')",
                          "UNSUPPORTED");
    return(failures);
}

/*
 * Expressions that need XPath 3.0 or 3.1, compiled at XPath 2.0, where
 * that syntax and those functions don't exist.
 */
static int
xpath20(xp3DynamicContext *dctx) {
    xp3StaticContext *sctx = xp3NewStaticContext();
    int failures = 0;

    if ((sctx == NULL) ||
        (xp3SetStaticContextLanguageLevel(sctx, XP3_XPATH_20) < 0)) {
        xp3FreeStaticContext(sctx);
        return(1);
    }
    failures += untrusted(sctx, dctx, NULL, "//book ! title", "XPST0003");
    failures += untrusted(sctx, dctx, NULL, "'a' || 'b'", "XPST0003");
    failures += untrusted(sctx, dctx, NULL, "'abc' => string-length()",
                          "XPST0003");
    failures += untrusted(sctx, dctx, NULL, "head(//book)", "XPST0017");
    failures += untrusted(sctx, dctx, NULL, "environment-variable('HOME')",
                          "XPST0017");
    xp3FreeStaticContext(sctx);
    return(failures);
}

int
main(int argc, char **argv) {
    xmlDocPtr doc = NULL;
    xp3StaticContext *sctx = NULL;
    xp3DynamicContext *dctx = NULL;
    int failures = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: %s DATA-DIRECTORY\n", argv[0]);
        return(2);
    }

    doc = exReadDocument(argv[1], "library.xml", 0);
    sctx = xp3NewStaticContext();
    dctx = xp3NewDynamicContext();
    if ((doc == NULL) || (sctx == NULL) || (dctx == NULL)) {
        failures++;
        goto done;
    }
    xp3SetContextNode(dctx, (xmlNodePtr) doc);

    /* Without these, an evaluation may run as long as it likes */
    xp3SetMaxSteps(dctx, MAX_STEPS);
    xp3SetMaxSequenceLength(dctx, MAX_ITEMS);

    printf("Size and depth:\n");
    failures += sizeAndDepth(sctx, dctx);
    printf("Work and memory:\n");
    failures += workAndMemory(sctx, dctx);
    printf("Bytes that are not text:\n");
    failures += badBytes(sctx, dctx);
    printf("Outside the process:\n");
    failures += outsideTheProcess(sctx, dctx);
    printf("XPath 2.0 only:\n");
    failures += xpath20(dctx);

done:
    xp3FreeDynamicContext(dctx);
    xp3FreeStaticContext(sctx);
    xmlFreeDoc(doc);
    if (failures > 0)
        return(1);
    return(0);
}
