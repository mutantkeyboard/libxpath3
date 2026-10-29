/*
 * testthreads.c: several threads evaluating one compiled expression
 *
 * A compiled expression is never written after compiling, so threads
 * may evaluate it at once, each with a dynamic context and sequences of
 * its own, which are counted without locks (doc/DESIGN.md 6.2). Folded
 * constants make that worth testing: they are shared by every
 * evaluation, and their counts must never change. The test checks
 * every result; under ThreadSanitizer (the clang:tsan CI job) it also
 * checks that no two threads write the same memory.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

#include <libxpath3/xpath3.h>

#define XP3_THREADS 8
#define XP3_EVALUATIONS 300

static const char xp3ThreadDoc[] =
    "<r><a id='i1'>one</a><a id='i2'><b>two</b><b>three</b></a>"
    "<c>4.5</c></r>";

/* Constants (folded arithmetic, casts, ranges, a pruned branch) next
 * to paths and variables that are evaluated each time */
static const char xp3ThreadExpr[] =
    "for $i in 1 to 6 return ((1, 'a', 2.5)[($i mod 3) + 1], $i * 2.5, "
    "if (1) then 'yes' else 'no', 0.1e0 cast as xs:decimal, "
    "(1 to 5)[. > 3], //a[@id = 'i2']/b, $v, c + 1)";

static xmlDocPtr threadDoc;
static xp3Expr *threadExpr;
static char expected[4096];

/* The string values of a result, separated by spaces */
static void
describe(const xp3Sequence *seq, char *out, size_t size) {
    size_t used = 0;
    size_t i;

    out[0] = 0;
    for (i = 0; i < xp3SequenceLength(seq); i++) {
        xmlChar *str = xp3SequenceStringValue(seq, i);
        int written = snprintf(out + used, size - used, "%s%s",
                               (i > 0) ? " " : "",
                               (str != NULL) ? (const char *) str : "?");

        xmlFree(str);
        if ((written < 0) || ((size_t) written >= size - used))
            return;
        used += (size_t) written;
    }
}

/* A dynamic context with the context node and $v, all of its own */
static xp3DynamicContext *
newThreadContext(void) {
    const xmlChar *text = BAD_CAST "shared";
    xp3DynamicContext *ctx = xp3NewDynamicContext();
    xp3Sequence *value = xp3NewStringSequence(&text, 1);
    int ok = (ctx != NULL) && (value != NULL) &&
             (xp3SetContextNode(ctx, xmlDocGetRootElement(threadDoc)) == 0) &&
             (xp3BindVariable(ctx, NULL, BAD_CAST "v", value) == 0);

    xp3FreeSequence(value);
    if (!ok) {
        xp3FreeDynamicContext(ctx);
        return(NULL);
    }
    return(ctx);
}

/* One thread: the shared expression and document, its own context */
static void *
evaluateMany(void *arg) {
    int *mismatches = arg;
    xp3DynamicContext *ctx = newThreadContext();
    char actual[4096];
    int i;

    if (ctx == NULL) {
        (*mismatches)++;
        return(NULL);
    }
    for (i = 0; i < XP3_EVALUATIONS; i++) {
        xp3Sequence *result = xp3Evaluate(threadExpr, ctx, NULL);

        describe(result, actual, sizeof(actual));
        if ((result == NULL) || (strcmp(actual, expected) != 0))
            (*mismatches)++;
        xp3FreeSequence(result);
    }
    xp3FreeDynamicContext(ctx);
    return(NULL);
}

int
main(void) {
    pthread_t threads[XP3_THREADS];
    int mismatches[XP3_THREADS];
    xp3StaticContext *sctx;
    xp3DynamicContext *ctx;
    xp3Sequence *result;
    int failures = 0;
    int i;

    xmlInitParser();
    threadDoc = xmlReadMemory(xp3ThreadDoc, (int) strlen(xp3ThreadDoc),
                              "threads.xml", NULL, 0);
    sctx = xp3NewStaticContext();
    if ((threadDoc == NULL) || (sctx == NULL) ||
        (xp3DeclareVariable(sctx, NULL, BAD_CAST "v") < 0)) {
        fprintf(stderr, "FAIL: test set up\n");
        return(1);
    }
    threadExpr = xp3Compile(sctx, BAD_CAST xp3ThreadExpr, NULL);
    xp3FreeStaticContext(sctx);
    if (threadExpr == NULL) {
        fprintf(stderr, "FAIL: expression compiled\n");
        return(1);
    }

    /* The expected result, from one thread first */
    ctx = newThreadContext();
    if (ctx == NULL) {
        fprintf(stderr, "FAIL: dynamic context set up\n");
        return(1);
    }
    result = xp3Evaluate(threadExpr, ctx, NULL);
    describe(result, expected, sizeof(expected));
    xp3FreeSequence(result);
    xp3FreeDynamicContext(ctx);
    /* For each $i: the item at ($i mod 3) + 1, $i * 2.5, then the same
     * eight values */
    if (strcmp(expected,
               "a 2.5 yes 0.1 4 5 two three shared 5.5 "
               "2.5 5 yes 0.1 4 5 two three shared 5.5 "
               "1 7.5 yes 0.1 4 5 two three shared 5.5 "
               "a 10 yes 0.1 4 5 two three shared 5.5 "
               "2.5 12.5 yes 0.1 4 5 two three shared 5.5 "
               "1 15 yes 0.1 4 5 two three shared 5.5") != 0) {
        fprintf(stderr, "FAIL: unexpected result %s\n", expected);
        failures++;
    }

    for (i = 0; i < XP3_THREADS; i++) {
        mismatches[i] = 0;
        if (pthread_create(&threads[i], NULL, evaluateMany,
                           &mismatches[i]) != 0) {
            fprintf(stderr, "FAIL: thread %d not created\n", i);
            return(1);
        }
    }
    for (i = 0; i < XP3_THREADS; i++) {
        pthread_join(threads[i], NULL);
        if (mismatches[i] > 0) {
            fprintf(stderr, "FAIL: thread %d got %d wrong results\n", i,
                    mismatches[i]);
            failures++;
        }
    }

    xp3FreeExpr(threadExpr);
    xmlFreeDoc(threadDoc);
    if (failures == 0)
        printf("testthreads: OK\n");
    return((failures == 0) ? 0 : 1);
}
