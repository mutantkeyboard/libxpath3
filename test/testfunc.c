/*
 * testfunc.c: tests for the table of built-in functions
 *
 * The counts and language levels below come from the W3C
 * specifications that codegen/genFunctions.py reads. If they change,
 * the table was regenerated from different input.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <libxml/parser.h>

#include "private/xp3context.h"
#include "private/xp3func.h"

static int failures = 0;

static void
check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

#define FN BAD_CAST XP3_NS_FN
#define MATH BAD_CAST XP3_NS_MATH
#define MAP BAD_CAST XP3_NS_MAP
#define ARRAY BAD_CAST XP3_NS_ARRAY

static const xp3FuncInfo *
lookup(const xmlChar *uri, const char *name, int arity,
       xp3LanguageLevel level) {
    return(xp3FuncLookup(uri, BAD_CAST name, arity, level));
}

/*
 * The table: sorted for the binary search, no duplicates, and every
 * entry found by a lookup at its own level but not below it.
 */
static void
testTable(void) {
    static const char *const prefixes[] = { "fn", "math", "map", "array" };
    size_t count = xp3FuncCount();
    size_t perLevel[3] = { 0, 0, 0 };
    size_t i;

    check(count == 273, "273 signatures");
    for (i = 0; i < count; i++) {
        const xp3FuncInfo *info = xp3FuncAt(i);
        const xp3FuncInfo *found;
        const xmlChar *uri = xp3FuncNamespaceUri(info->ns);
        char what[200];

        if (i > 0) {
            const xp3FuncInfo *prev = xp3FuncAt(i - 1);
            int cmp = (int) prev->ns - (int) info->ns;

            if (cmp == 0)
                cmp = strcmp(prev->name, info->name);
            if (cmp == 0)
                cmp = prev->arity - info->arity;
            snprintf(what, sizeof(what), "table sorted at %s#%d",
                     info->name, info->arity);
            check(cmp < 0, what);
        }

        snprintf(what, sizeof(what), "lookup of %s", info->signature);
        found = xp3FuncLookup(uri, BAD_CAST info->name, info->arity,
                              info->level);
        check(found == info, what);
        if (info->level == XP3_XPATH_30)
            found = xp3FuncLookup(uri, BAD_CAST info->name, info->arity,
                                  XP3_XPATH_20);
        else if (info->level == XP3_XPATH_31)
            found = xp3FuncLookup(uri, BAD_CAST info->name, info->arity,
                                  XP3_XPATH_30);
        else
            found = NULL;
        snprintf(what, sizeof(what), "%s not below its level",
                 info->signature);
        check(found == NULL, what);

        snprintf(what, sizeof(what), "%s:%s(", prefixes[info->ns],
                 info->name);
        check(strncmp(info->signature, what, strlen(what)) == 0,
              info->signature);

        switch (info->level) {
            case XP3_XPATH_20: perLevel[0]++; break;
            case XP3_XPATH_30: perLevel[1]++; break;
            case XP3_XPATH_31: perLevel[2]++; break;
            default: check(0, "valid level"); break;
        }
    }
    check(perLevel[0] == 153, "153 signatures in XPath 2.0");
    check(perLevel[1] == 63, "63 more in XPath 3.0");
    check(perLevel[2] == 57, "57 more in XPath 3.1");
    check(xp3FuncAt(count) == NULL, "index out of range");
}

static void
testLookup(void) {
    const xp3FuncInfo *info;

    info = lookup(FN, "substring", 2, XP3_XPATH_31);
    check((info != NULL) && (info->arity == 2) &&
          (strcmp(info->signature, "fn:substring($sourceString as "
                  "xs:string?, $start as xs:double) as xs:string") == 0),
          "fn:substring#2");
    check(lookup(FN, "substring", 3, XP3_XPATH_20) != NULL,
          "fn:substring#3");
    check(lookup(FN, "substring", 1, XP3_XPATH_31) == NULL,
          "no fn:substring#1");
    check(lookup(FN, "substring", 4, XP3_XPATH_31) == NULL,
          "no fn:substring#4");

    /* First and last entries, the edges of the binary search */
    check(lookup(FN, "QName", 2, XP3_XPATH_20) == xp3FuncAt(0),
          "fn:QName is first");
    check(lookup(ARRAY, "tail", 1, XP3_XPATH_31) ==
          xp3FuncAt(xp3FuncCount() - 1), "array:tail is last");
    check(lookup(FN, "qname", 2, XP3_XPATH_31) == NULL,
          "names are case-sensitive");

    /* Arities added in later versions */
    check(lookup(FN, "round", 1, XP3_XPATH_20) != NULL, "fn:round#1 in 2.0");
    check(lookup(FN, "round", 2, XP3_XPATH_20) == NULL,
          "no fn:round#2 in 2.0");
    check(lookup(FN, "round", 2, XP3_XPATH_30) != NULL, "fn:round#2 in 3.0");
    check(lookup(FN, "string-join", 1, XP3_XPATH_20) == NULL,
          "no fn:string-join#1 in 2.0");
    check(lookup(FN, "string-join", 1, XP3_XPATH_30) != NULL,
          "fn:string-join#1 in 3.0");
    check(lookup(FN, "tokenize", 1, XP3_XPATH_30) == NULL,
          "no fn:tokenize#1 in 3.0");
    check(lookup(FN, "tokenize", 1, XP3_XPATH_31) != NULL,
          "fn:tokenize#1 in 3.1");

    /* Namespaces added in later versions */
    check(lookup(MATH, "pi", 0, XP3_XPATH_20) == NULL, "no math:pi in 2.0");
    check(lookup(MATH, "pi", 0, XP3_XPATH_30) != NULL, "math:pi in 3.0");
    check(lookup(MAP, "get", 2, XP3_XPATH_30) == NULL, "no map:get in 3.0");
    check(lookup(MAP, "get", 2, XP3_XPATH_31) != NULL, "map:get in 3.1");
    info = lookup(ARRAY, "get", 2, XP3_XPATH_31);
    check((info != NULL) && (info->ns == XP3_FUNC_NS_ARRAY),
          "array:get is not map:get");

    /* fn:concat takes two or more arguments */
    check(lookup(FN, "concat", 1, XP3_XPATH_31) == NULL, "no fn:concat#1");
    info = lookup(FN, "concat", 2, XP3_XPATH_20);
    check((info != NULL) && (info->flags & XP3_FUNC_VARIADIC),
          "fn:concat#2 is variadic");
    check(lookup(FN, "concat", 3, XP3_XPATH_20) == info, "fn:concat#3");
    check(lookup(FN, "concat", INT_MAX, XP3_XPATH_20) == info,
          "fn:concat with INT_MAX arguments");
    check(lookup(FN, "substring", INT_MAX, XP3_XPATH_31) == NULL,
          "fn:substring with INT_MAX arguments");

    /* Not built-in functions */
    check(lookup(BAD_CAST XP3_NS_XS, "integer", 1, XP3_XPATH_31) == NULL,
          "constructor functions are not in the table");
    check(lookup(BAD_CAST "urn:example", "substring", 2, XP3_XPATH_31) ==
          NULL, "other namespace");
    check(lookup(BAD_CAST "", "substring", 2, XP3_XPATH_31) == NULL,
          "no namespace");
    check(lookup(FN, "nosuch", 0, XP3_XPATH_31) == NULL, "unknown name");
    check(lookup(FN, "", 0, XP3_XPATH_31) == NULL, "empty name");
    check(lookup(FN, "true", -1, XP3_XPATH_31) == NULL, "negative arity");
    check(lookup(NULL, "true", 0, XP3_XPATH_31) == NULL, "NULL URI");
    check(lookup(FN, NULL, 0, XP3_XPATH_31) == NULL, "NULL name");
}

static void
testFlags(void) {
    const xp3FuncInfo *info;

    info = lookup(FN, "abs", 1, XP3_XPATH_31);
    check((info != NULL) && (info->flags == 0), "fn:abs has no flags");
    info = lookup(FN, "position", 0, XP3_XPATH_31);
    check((info != NULL) && (info->flags & XP3_FUNC_FOCUS),
          "fn:position is focus-dependent");
    info = lookup(FN, "name", 1, XP3_XPATH_31);
    check((info != NULL) && !(info->flags & XP3_FUNC_FOCUS),
          "fn:name#1 is focus-independent");
    info = lookup(FN, "current-dateTime", 0, XP3_XPATH_31);
    check((info != NULL) && (info->flags & XP3_FUNC_CONTEXT),
          "fn:current-dateTime is context-dependent");
    info = lookup(FN, "error", 0, XP3_XPATH_31);
    check((info != NULL) && (info->flags & XP3_FUNC_NONDETERMINISTIC),
          "fn:error is nondeterministic");
    info = lookup(FN, "sort", 3, XP3_XPATH_31);
    check((info != NULL) && (info->flags & XP3_FUNC_HIGHER_ORDER),
          "fn:sort#3 is higher-order");
    info = lookup(FN, "unordered", 1, XP3_XPATH_31);
    check((info != NULL) && (info->flags == XP3_FUNC_ORDER),
          "fn:unordered is only nondeterministic in its ordering");
    info = lookup(MAP, "keys", 1, XP3_XPATH_31);
    check((info != NULL) && (info->flags == XP3_FUNC_ORDER),
          "map:keys is nondeterministic in its ordering");
    info = lookup(MAP, "for-each", 2, XP3_XPATH_31);
    check((info != NULL) &&
          (info->flags == (XP3_FUNC_ORDER | XP3_FUNC_HIGHER_ORDER)),
          "map:for-each is higher-order and nondeterministic in ordering");
    info = lookup(FN, "distinct-values", 1, XP3_XPATH_31);
    check((info != NULL) &&
          (info->flags == (XP3_FUNC_CONTEXT | XP3_FUNC_ORDER)),
          "fn:distinct-values#1 is context-dependent, order undefined");

    /* Corrections of the function catalog, see codegen/genFunctions.py */
    info = lookup(FN, "boolean", 1, XP3_XPATH_31);
    check((info != NULL) && (info->flags == 0), "fn:boolean has no flags");
    info = lookup(FN, "path", 0, XP3_XPATH_31);
    check((info != NULL) && (info->flags & XP3_FUNC_FOCUS),
          "fn:path#0 is focus-dependent");
    info = lookup(FN, "path", 1, XP3_XPATH_31);
    check((info != NULL) && (info->flags == 0),
          "fn:path#1 is focus-independent");
    info = lookup(ARRAY, "sort", 3, XP3_XPATH_31);
    check((info != NULL) &&
          (info->flags == (XP3_FUNC_CONTEXT | XP3_FUNC_HIGHER_ORDER)),
          "array:sort#3 is context-dependent and higher-order");
    info = lookup(FN, "distinct-values", 2, XP3_XPATH_31);
    check((info != NULL) &&
          (info->flags == (XP3_FUNC_CONTEXT | XP3_FUNC_ORDER)),
          "fn:distinct-values#2 has the same order property as #1");
}

static void
testNames(void) {
    check(xp3FuncNameExists(FN, BAD_CAST "substring", XP3_XPATH_20),
          "fn:substring exists");
    check(!xp3FuncNameExists(FN, BAD_CAST "has-children", XP3_XPATH_20),
          "no fn:has-children in 2.0");
    check(xp3FuncNameExists(FN, BAD_CAST "has-children", XP3_XPATH_30),
          "fn:has-children in 3.0");
    check(!xp3FuncNameExists(FN, BAD_CAST "nosuch", XP3_XPATH_31),
          "unknown name");
    check(!xp3FuncNameExists(MAP, BAD_CAST "substring", XP3_XPATH_31),
          "wrong namespace");
    check(!xp3FuncNameExists(NULL, BAD_CAST "true", XP3_XPATH_31),
          "NULL URI");
    check(!xp3FuncNameExists(FN, NULL, XP3_XPATH_31), "NULL name");

    check(xmlStrEqual(xp3FuncNamespaceUri(XP3_FUNC_NS_MATH), MATH),
          "math namespace");
    check(xp3FuncNamespaceUri((xp3FuncNamespace) 4) == NULL,
          "invalid namespace");
}

int
main(void) {
    xmlInitParser();

    testTable();
    testLookup();
    testFlags();
    testNames();

    if (failures == 0)
        printf("testfunc: OK\n");
    return(failures == 0 ? 0 : 1);
}
