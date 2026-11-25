/*
 * 02-namespaces-variables.c: namespaces, variables, and one compiled
 * expression evaluated on several context nodes
 *
 * The program of doc/tutorial/02-namespaces-variables.md. Run it with
 * the directory of the example documents:
 *
 *     02-namespaces-variables examples/data
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdio.h>

#include <libxml/tree.h>
#include <libxpath3/xpath3.h>

#include "common.h"

#define ATOM_NS BAD_CAST "http://www.w3.org/2005/Atom"
#define MEDIA_NS BAD_CAST "http://example.com/ns/media/1"
#define EXTRA_NS BAD_CAST "http://example.com/ns/extra"

/* A query, and the error code it raises, or NULL if it succeeds */
typedef struct {
    const char *text;
    const char *expectedError;
} Query;

/* With the prefixes a, media and x declared */
static const Query prefixedQueries[] = {
    { "count(/a:feed/a:entry)", NULL },
    { "count(/Q{http://www.w3.org/2005/Atom}feed"
      "/Q{http://www.w3.org/2005/Atom}entry)", NULL },
    { "count(/*:feed/*:entry)", NULL },
    { "count(//*:title)", NULL },
    /* Two elements written media:thumbnail, in two namespaces */
    { "//*:thumbnail/name()", NULL },
    { "//*:thumbnail/namespace-uri()", NULL },
    { "namespace-uri-for-prefix('media', (//*:thumbnail)[2])", NULL },
    { "//*:payload/namespace-uri()", NULL },
    { "//media:thumbnail/@url", NULL },
    { "//*:thumbnail/@media:width", NULL },
    { "//*:thumbnail/@*:width", NULL },
    /* An unprefixed attribute is in no namespace */
    { "//a:link/@href", NULL },
    { "//a:link/@a:href", NULL },
    { "//x:rating/@x:scale", NULL },
    /* xml:lang is inherited, and "en" matches "en-GB" */
    { "//a:entry[lang('en')]/a:title/string()", NULL },
    { "//a:entry[lang('de')]/a:title/string()", NULL },
    { "//*:payload/*:title/lang('en')", NULL },
    /* No prefix is bound to the Dublin Core namespace */
    { "//dc:creator", "XPST0081" }
};

static int
runQueries(const xp3StaticContext *sctx, xp3DynamicContext *dctx,
           const Query *queries, size_t count) {
    size_t i;
    int failures = 0;

    for (i = 0; i < count; i++)
        failures += exRun(sctx, dctx, NULL, queries[i].text,
                          queries[i].expectedError);
    return(failures);
}

/*
 * Unprefixed names: in no namespace, then in the default element
 * namespace.
 */
static int
unprefixedNames(xp3DynamicContext *dctx) {
    xp3StaticContext *sctx;
    int failures = 0;

    sctx = xp3NewStaticContext();
    if (sctx == NULL)
        return(1);

    printf("No default element namespace:\n");
    failures += exRun(sctx, dctx, NULL, "count(/feed/entry)", NULL);
    failures += exRun(sctx, dctx, NULL, "//title", NULL);

    printf("Default element namespace Atom:\n");
    if (xp3SetDefaultElementNamespace(sctx, ATOM_NS) < 0) {
        xp3FreeStaticContext(sctx);
        return(1);
    }
    failures += exRun(sctx, dctx, NULL, "count(/feed/entry)", NULL);
    failures += exRun(sctx, dctx, NULL, "//title", NULL);
    failures += exRun(sctx, dctx, NULL, "//Q{}title", NULL);
    failures += exRun(sctx, dctx, NULL, "//link/@href", NULL);

    xp3FreeStaticContext(sctx);
    return(failures);
}

/*
 * Bind $ids to a sequence of integers and compare it with 2, with a
 * general comparison and a value comparison. eqError is the error the
 * value comparison raises, or NULL.
 */
static int
compareIds(const xp3StaticContext *sctx, xp3DynamicContext *dctx,
           const int64_t *ids, size_t count, const char *eqError) {
    xp3Sequence *value;
    int failures = 0;

    value = xp3NewIntegerSequence(ids, count);
    if ((value == NULL) ||
        (xp3BindVariable(dctx, NULL, BAD_CAST "ids", value) < 0)) {
        xp3FreeSequence(value);
        return(1);
    }
    /* The dynamic context keeps a reference of its own */
    xp3FreeSequence(value);

    failures += exRun(sctx, dctx, NULL, "$ids = 2", NULL);
    failures += exRun(sctx, dctx, NULL, "$ids != 2", NULL);
    failures += exRun(sctx, dctx, NULL, "$ids eq 2", eqError);
    return(failures);
}

static int
variables(xp3DynamicContext *dctx) {
    static const int64_t someIds[] = { 1, 2, 3 };
    xp3StaticContext *sctx;
    int failures = 0;

    sctx = xp3NewStaticContext();
    if (sctx == NULL)
        return(1);

    printf("Variables:\n");
    failures += exRun(sctx, dctx, NULL, "$limit > 2", "XPST0008");

    if ((xp3DeclareVariable(sctx, NULL, BAD_CAST "ids") < 0) ||
        (xp3DeclareVariable(sctx, NULL, BAD_CAST "missing") < 0)) {
        xp3FreeStaticContext(sctx);
        return(1);
    }
    failures += exRun(sctx, dctx, NULL, "$missing", "XPDY0002");

    printf("$ids bound to ():\n");
    failures += compareIds(sctx, dctx, NULL, 0, NULL);
    printf("$ids bound to (1, 2, 3):\n");
    failures += compareIds(sctx, dctx, someIds, 3, "XPTY0004");

    xp3FreeStaticContext(sctx);
    return(failures);
}

/*
 * Compile one expression, then evaluate it with each entry as the
 * context node.
 */
static int
eachEntry(const xp3StaticContext *sctx, xp3DynamicContext *dctx,
          xmlDocPtr doc) {
    static const char *const expected[] = {
        NULL, NULL, "XPTY0004", NULL
    };
    xp3Expr *entries;
    xp3Expr *rating;
    xp3Expr *focus;
    xp3Sequence *list = NULL;
    size_t i;
    int failures = 0;

    entries = xp3Compile(sctx, BAD_CAST "/a:feed/a:entry", NULL);
    rating = xp3Compile(sctx, BAD_CAST "x:rating + 1", NULL);
    focus = xp3Compile(sctx, BAD_CAST "position(), last()", NULL);
    if ((entries == NULL) || (rating == NULL) || (focus == NULL)) {
        failures++;
        goto done;
    }

    xp3SetContextNode(dctx, (xmlNodePtr) doc);
    list = xp3Evaluate(entries, dctx, NULL);
    if ((list == NULL) || (xp3SequenceLength(list) != 4)) {
        failures++;
        goto done;
    }

    printf("x:rating + 1 on each entry:\n");
    for (i = 0; i < xp3SequenceLength(list); i++) {
        char label[32];

        snprintf(label, sizeof(label), "entry %d", (int) i + 1);
        xp3SetContextNode(dctx, xp3SequenceNode(list, i));
        failures += exEvaluate(rating, dctx, label, expected[i]);
    }
    failures += exEvaluate(focus, dctx, "position(), last()", NULL);

done:
    /* The document is the context again for whatever comes next */
    xp3SetContextNode(dctx, (xmlNodePtr) doc);
    xp3FreeSequence(list);
    xp3FreeExpr(focus);
    xp3FreeExpr(rating);
    xp3FreeExpr(entries);
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

    doc = exReadDocument(argv[1], "feed.xml", 0);
    dctx = xp3NewDynamicContext();
    sctx = xp3NewStaticContext();
    if ((doc == NULL) || (dctx == NULL) || (sctx == NULL)) {
        failures++;
        goto done;
    }
    xp3SetContextNode(dctx, (xmlNodePtr) doc);

    failures += unprefixedNames(dctx);

    /* The prefixes of the expression, which the document doesn't decide */
    if ((xp3DeclareNamespace(sctx, BAD_CAST "a", ATOM_NS) < 0) ||
        (xp3DeclareNamespace(sctx, BAD_CAST "media", MEDIA_NS) < 0) ||
        (xp3DeclareNamespace(sctx, BAD_CAST "x", EXTRA_NS) < 0)) {
        failures++;
        goto done;
    }
    printf("Prefixes a, media and x:\n");
    failures += runQueries(sctx, dctx, prefixedQueries,
                           sizeof(prefixedQueries) /
                           sizeof(prefixedQueries[0]));

    failures += variables(dctx);
    failures += eachEntry(sctx, dctx, doc);

done:
    xp3FreeStaticContext(sctx);
    xp3FreeDynamicContext(dctx);
    xmlFreeDoc(doc);
    if (failures > 0)
        return(1);
    return(0);
}
