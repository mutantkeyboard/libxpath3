/*
 * teststatic.c: tests for static analysis and xp3Compile()
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdio.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/xmlmemory.h>

#include <libxpath3/xpath3.h>
#include "private/xp3ast.h"
#include "private/xp3context.h"
#include "private/xp3func.h"
#include "private/xp3static.h"
#include "testalloc.h"

static int failures = 0;

static void
check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

/* Namespace URIs, shortened in the expected trees below */
#define FN "Q{http://www.w3.org/2005/xpath-functions}"
#define XS "Q{http://www.w3.org/2001/XMLSchema}"
#define EX "Q{urn:example}"
#define DEF "Q{urn:default}"

/*
 * The context most tests use: prefix ex bound, a default element
 * namespace, and the variables $v and $ex:w declared.
 */
static xp3StaticContext *
testContext(xp3LanguageLevel level) {
    xp3StaticContext *ctx = xp3NewStaticContext();

    if ((ctx == NULL) ||
        (xp3SetStaticContextLanguageLevel(ctx, level) < 0) ||
        (xp3DeclareNamespace(ctx, BAD_CAST "ex",
                             BAD_CAST "urn:example") < 0) ||
        (xp3SetDefaultElementNamespace(ctx, BAD_CAST "urn:default") < 0) ||
        (xp3DeclareVariable(ctx, NULL, BAD_CAST "v") < 0) ||
        (xp3DeclareVariable(ctx, BAD_CAST "urn:example", BAD_CAST "w") < 0)) {
        xp3FreeStaticContext(ctx);
        return(NULL);
    }
    /* These tests are about analysis; testOptimize() covers folding */
    ctx->noOptimize = 1;
    return(ctx);
}

/* Compile and print the annotated tree, or NULL with *err set. */
static xmlChar *
compileToString(const xp3StaticContext *ctx, const char *expr,
                xp3Error **err) {
    xp3Expr *compiled = xp3Compile(ctx, BAD_CAST expr, err);
    xmlChar *ret;

    if (compiled == NULL)
        return(NULL);
    ret = xp3AstToString(compiled->ast->root);
    xp3FreeExpr(compiled);
    return(ret);
}

/*
 * Resolved names
 */

static const struct {
    const char *expr;
    const char *tree;
} treeCases[] = {
    /* Element names get the default namespace, attributes do not */
    { "a/@b/ex:c/@ex:d",
      "(path (step child (name " DEF "a)) (step attribute (name b)) "
      "(step child (name " EX "c)) (step attribute (name " EX "d)))" },
    { "Q{}a/Q{urn:x}b/*:c/ex:*/@*",
      "(path (step child (name a)) (step child (name Q{urn:x}b)) "
      "(step child (name *:c)) (step child (name " EX "*)) "
      "(step attribute (name *)))" },
    { "element(a)/attribute(b)",
      "(path (step child (kind element (name " DEF "a))) "
      "(step attribute (kind attribute (name b))))" },
    { "element(a, xs:untyped)",
      "(path (step child (kind element (name " DEF "a) "
      "(type (atomic " XS "untyped)))))" },

    /* Functions get the function namespace */
    { "true()", "(call " FN "true)" },
    { "fn:count(()), Q{" XP3_NS_FN "}empty(())",
      "(sequence (call " FN "count (empty)) (call " FN "empty (empty)))" },
    { "count#1", "(function-ref " FN "count#1)" },
    { "() => count()", "(arrow " FN "count (empty))" },

    /* Constructor functions become casts */
    { "xs:integer('1')",
      "(cast-as (string \"1\") (type ? (atomic " XS "integer)))" },
    { "'1' => xs:double()",
      "(cast-as (string \"1\") (type ? (atomic " XS "double)))" },
    { "xs:integer(?)", "(call " XS "integer (placeholder))" },
    { "xs:date#1", "(function-ref " XS "date#1)" },

    /* Types */
    { "1 instance of xs:integer*",
      "(instance-of (integer 1) (type * (atomic " XS "integer)))" },
    { "1 cast as xs:NMTOKENS",
      "(cast-as (integer 1) (type (atomic " XS "NMTOKENS)))" },
    { "1 instance of xs:numeric",
      "(instance-of (integer 1) (type (atomic " XS "numeric)))" },

    /* Variables */
    { "$v + $ex:w", "(binary + (var v) (var " EX "w))" },
    { "for $x in 1 return $x", "(for (bind x (integer 1)) (var x))" }
};

static void
testTrees(void) {
    xp3StaticContext *ctx = testContext(XP3_XPATH_31);
    size_t i;

    for (i = 0; i < sizeof(treeCases) / sizeof(treeCases[0]); i++) {
        xp3Error *err = NULL;
        xmlChar *actual = compileToString(ctx, treeCases[i].expr, &err);
        char what[1024];

        snprintf(what, sizeof(what), "'%s': expected %s, got %s",
                 treeCases[i].expr, treeCases[i].tree,
                 actual ? (const char *) actual :
                 err ? (const char *) xp3ErrorMessage(err) : "NULL");
        check((actual != NULL) &&
              (strcmp((const char *) actual, treeCases[i].tree) == 0), what);
        xmlFree(actual);
        xp3ErrorFree(err);
    }
    xp3FreeStaticContext(ctx);
}

/*
 * Static errors
 */

static const struct {
    xp3LanguageLevel level;
    const char *expr;
    const char *code;
    int column;
} errorCases[] = {
    /* Prefixes and variables */
    { XP3_XPATH_31, "p:a",                        "XPST0081", 1 },
    { XP3_XPATH_31, "1 + $p:x",                   "XPST0081", 5 },
    { XP3_XPATH_31, "$x",                         "XPST0008", 1 },
    { XP3_XPATH_31, "$ex:v",                      "XPST0008", 1 },
    { XP3_XPATH_31, "let $x := $x return 1",      "XPST0008", 11 },
    { XP3_XPATH_31, "(for $x in 1 return $x), $x", "XPST0008", 26 },
    { XP3_XPATH_31, "function($a) { $a }, $a",    "XPST0008", 22 },
    { XP3_XPATH_31, "Q{http://www.w3.org/2000/xmlns/}a",
                                                  "XQST0070", 1 },

    /* Functions */
    { XP3_XPATH_31, "foo()",                      "XPST0017", 1 },
    { XP3_XPATH_31, "ex:foo#2",                   "XPST0017", 1 },
    { XP3_XPATH_31, "substring('a')",             "XPST0017", 1 },
    /* An arrow is reported where its left side starts */
    { XP3_XPATH_31, "'a' => substring(1, 2, 3)",  "XPST0017", 1 },
    { XP3_XPATH_31, "concat#1",                   "XPST0017", 1 },
    { XP3_XPATH_31, "concat#99999999999999999999", "FOAR0002", 1 },
    { XP3_XPATH_31, "true#99999999999999999999",  "XPST0017", 1 },
    { XP3_XPATH_20, "has-children()",             "XPST0017", 1 },
    { XP3_XPATH_30, "map:size(1)",                "XPST0017", 1 },
    { XP3_XPATH_31, "xs:integer(1, 2)",           "XPST0017", 1 },
    { XP3_XPATH_31, "xs:NOTATION('a')",           "XPST0017", 1 },
    { XP3_XPATH_31, "xs:untyped('a')",            "XPST0017", 1 },
    { XP3_XPATH_20, "xs:NMTOKENS('a')",           "XPST0017", 1 },

    /* Types */
    { XP3_XPATH_31, "1 instance of xs:NMTOKENS",  "XPST0051", 15 },
    { XP3_XPATH_31, "1 instance of xs:foo",       "XPST0051", 15 },
    { XP3_XPATH_31, "1 treat as integer",         "XPST0051", 12 },
    { XP3_XPATH_30, "1 instance of xs:numeric",   "XPST0051", 15 },
    { XP3_XPATH_31, "() instance of map(xs:untyped, item())",
                                                  "XPST0051", 20 },
    { XP3_XPATH_31, "1 cast as xs:NOTATION",      "XPST0080", 11 },
    { XP3_XPATH_31, "1 cast as xs:anySimpleType", "XPST0080", 11 },
    { XP3_XPATH_31, "1 castable as xs:anyAtomicType", "XPST0080", 15 },
    { XP3_XPATH_31, "1 cast as xs:untyped",       "XQST0052", 11 },
    { XP3_XPATH_31, "1 cast as xs:foo",           "XQST0052", 11 },
    { XP3_XPATH_20, "1 cast as xs:NMTOKENS",      "XPST0051", 11 },
    { XP3_XPATH_20, "1 cast as xs:anySimpleType", "XPST0051", 11 },
    { XP3_XPATH_20, "1 cast as xs:anyAtomicType", "XPST0080", 11 },
    { XP3_XPATH_31, "element(a, foo)",            "XPST0008", 12 },
    { XP3_XPATH_31, "schema-element(a)",          "XPST0008", 16 },
    { XP3_XPATH_31, "schema-attribute(ex:a)",     "XPST0008", 18 },
    { XP3_XPATH_31, "schema-element(p:a)",        "XPST0081", 16 },

    /* Other */
    { XP3_XPATH_31, "namespace::a",               "XPST0010", 1 },
    { XP3_XPATH_31, "function($a, $a) { 1 }",     "XQST0039", 14 },
    { XP3_XPATH_31, "function($a, $Q{}a) { 1 }",  "XQST0039", 14 },

    /* Syntax errors come from the parser */
    { XP3_XPATH_31, "1 +",                        "XPST0003", 4 }
};

static void
testErrors(void) {
    size_t i;

    for (i = 0; i < sizeof(errorCases) / sizeof(errorCases[0]); i++) {
        xp3StaticContext *ctx = testContext(errorCases[i].level);
        xp3Error *err = NULL;
        xp3Expr *compiled;
        char what[1024];

        compiled = xp3Compile(ctx, BAD_CAST errorCases[i].expr, &err);
        snprintf(what, sizeof(what),
                 "'%s' (level %d): expected %s at %d, got %s at %d (%s)",
                 errorCases[i].expr, (int) errorCases[i].level,
                 errorCases[i].code, errorCases[i].column,
                 err ? (const char *) xp3ErrorCode(err) : "no error",
                 xp3ErrorColumn(err),
                 err ? (const char *) xp3ErrorMessage(err) : "");
        check((compiled == NULL) && (err != NULL) &&
              (strcmp((const char *) xp3ErrorCode(err),
                      errorCases[i].code) == 0) &&
              (xp3ErrorLine(err) == 1) &&
              (xp3ErrorColumn(err) == errorCases[i].column), what);
        xp3FreeExpr(compiled);
        xp3ErrorFree(err);
        xp3FreeStaticContext(ctx);
    }
}

/* Check the message of the error for expr. */
static void
expectMessage(xp3LanguageLevel level, const char *expr,
              const char *message) {
    xp3StaticContext *ctx = testContext(level);
    xp3Error *err = NULL;
    char what[512];

    xp3FreeExpr(xp3Compile(ctx, BAD_CAST expr, &err));
    snprintf(what, sizeof(what), "'%s': message \"%s\", expected \"%s\"",
             expr, err ? (const char *) xp3ErrorMessage(err) : "(none)",
             message);
    check((err != NULL) &&
          (strcmp((const char *) xp3ErrorMessage(err), message) == 0),
          what);
    xp3ErrorFree(err);
    xp3FreeStaticContext(ctx);
}

static void
testMessages(void) {
    expectMessage(XP3_XPATH_31, "p:a",
                  "namespace prefix 'p' in p:a is not declared");
    expectMessage(XP3_XPATH_31, "$x", "variable $x is not declared");
    expectMessage(XP3_XPATH_31, "foo()", "unknown function foo#0");
    expectMessage(XP3_XPATH_31, "Q{urn:x}foo#1",
                  "unknown function Q{urn:x}foo#1");
    expectMessage(XP3_XPATH_31, "substring('a')",
                  "function substring does not take 1 argument");
    expectMessage(XP3_XPATH_31, "fn:true(1, 2)",
                  "function fn:true does not take 2 arguments");
    expectMessage(XP3_XPATH_20, "has-children()",
                  "function has-children#0 is not available in XPath 2.0");
    expectMessage(XP3_XPATH_30, "fn:sort(())",
                  "function fn:sort#1 is not available in XPath 3.0");
    expectMessage(XP3_XPATH_31, "xs:integer()",
                  "the constructor function xs:integer takes one "
                  "argument, not 0");
    expectMessage(XP3_XPATH_31, "xs:untyped('a')",
                  "xs:untyped has no constructor function");
    expectMessage(XP3_XPATH_31, "concat#99999999999999999999",
                  "the arity of concat is too large");
    expectMessage(XP3_XPATH_31, "1 instance of foo",
                  "unknown type foo");
    expectMessage(XP3_XPATH_31, "1 instance of xs:IDREFS",
                  "xs:IDREFS is not an atomic type");
    expectMessage(XP3_XPATH_31, "1 cast as xs:anyType",
                  "cannot cast to xs:anyType");
    expectMessage(XP3_XPATH_31, "1 cast as xs:NOTATION",
                  "cannot cast to the abstract type xs:NOTATION");
    expectMessage(XP3_XPATH_31, "schema-element(a)",
                  "no element declaration for a");
    expectMessage(XP3_XPATH_31, "schema-attribute(a)",
                  "no attribute declaration for a");
    expectMessage(XP3_XPATH_31, "function($a, $a) { 1 }",
                  "duplicate parameter $a");
    expectMessage(XP3_XPATH_31, "Q{http://www.w3.org/2000/xmlns/}a",
                  "Q{http://www.w3.org/2000/xmlns/}a is in the namespace "
                  "http://www.w3.org/2000/xmlns/, which is reserved");
}

/*
 * Variables and slots
 */

/* Append "name=slot" of every variable node, in document order. */
static void
describeSlots(const xp3AstNode *node, char *out, size_t size) {
    const xp3AstNode *child;

    if ((node->kind == XP3_AST_VARREF) || (node->kind == XP3_AST_BINDING) ||
        (node->kind == XP3_AST_PARAM)) {
        size_t len = strlen(out);

        snprintf(out + len, size - len, "%s%s%s=%d", (len > 0) ? " " : "",
                 (node->kind == XP3_AST_VARREF) ? "$" : "",
                 (const char *) node->text, node->op);
    }
    for (child = node->first; child != NULL; child = child->next)
        describeSlots(child, out, size);
}

static const struct {
    const char *expr;
    const char *slots;
    int slotCount;
} slotCases[] = {
    /* Listed in document order, a binding before its expression */
    { "for $x in 1, $y in $x return $x + $y",
      "x=0 y=1 $x=0 $x=0 $y=1", 2 },
    /* An inner binding of the same name shadows the outer one */
    { "let $x := 1 return let $x := $x return $x",
      "x=0 x=1 $x=0 $x=1", 2 },
    { "some $x in 1 satisfies every $y in $x satisfies $y",
      "x=0 y=1 $x=0 $y=1", 2 },
    /* Parameters, and a closure over a declared variable */
    { "function($a, $b) { $a + $v }", "a=0 b=1 $a=0 $v=2", 3 },
    /* Declared variables get one slot each, at their first use */
    { "$v, $ex:w, $v, $Q{urn:example}w", "$v=0 $w=1 $v=0 $w=1", 2 }
};

static void
testSlots(void) {
    xp3StaticContext *ctx = testContext(XP3_XPATH_31);
    size_t i;

    for (i = 0; i < sizeof(slotCases) / sizeof(slotCases[0]); i++) {
        xp3Expr *compiled = xp3Compile(ctx, BAD_CAST slotCases[i].expr,
                                       NULL);
        char actual[256] = "";
        char what[512];

        if (compiled != NULL)
            describeSlots(compiled->ast->root, actual, sizeof(actual));
        snprintf(what, sizeof(what), "'%s': slots %s, expected %s",
                 slotCases[i].expr, actual, slotCases[i].slots);
        check((compiled != NULL) &&
              (strcmp(actual, slotCases[i].slots) == 0) &&
              (compiled->slotCount == slotCases[i].slotCount), what);
        xp3FreeExpr(compiled);
    }
    xp3FreeStaticContext(ctx);
}

static void
testDeclaredVariables(void) {
    xp3StaticContext *ctx = testContext(XP3_XPATH_31);
    xp3Expr *compiled;

    compiled = xp3Compile(ctx, BAD_CAST "$ex:w + $v + $ex:w", NULL);
    check((compiled != NULL) && (compiled->variableCount == 2) &&
          xmlStrEqual(compiled->variables[0].uri, BAD_CAST "urn:example") &&
          xmlStrEqual(compiled->variables[0].localName, BAD_CAST "w") &&
          (compiled->variables[0].slot == 0) &&
          (compiled->variables[1].uri == NULL) &&
          xmlStrEqual(compiled->variables[1].localName, BAD_CAST "v") &&
          (compiled->variables[1].slot == 1) &&
          (compiled->level == XP3_XPATH_31),
          "declared variables in order of first use");
    xp3FreeExpr(compiled);

    compiled = xp3Compile(ctx, BAD_CAST "for $v in 1 return $v", NULL);
    check((compiled != NULL) && (compiled->variableCount == 0) &&
          (compiled->variables == NULL),
          "a range variable hides a declared one");
    xp3FreeExpr(compiled);

    /* Declaring again, also with "" for no namespace, changes nothing */
    check(xp3DeclareVariable(ctx, BAD_CAST "", BAD_CAST "v") == 0,
          "declare again");
    check(xp3DeclareVariable(ctx, BAD_CAST "", BAD_CAST "u") == 0,
          "declare with an empty URI");
    compiled = xp3Compile(ctx, BAD_CAST "$u", NULL);
    check(compiled != NULL, "empty URI means no namespace");
    xp3FreeExpr(compiled);

    check(xp3DeclareVariable(NULL, NULL, BAD_CAST "v") < 0, "NULL context");
    check(xp3DeclareVariable(ctx, NULL, NULL) < 0, "NULL name");
    check(xp3DeclareVariable(ctx, NULL, BAD_CAST "1x") < 0, "invalid name");
    check(xp3DeclareVariable(ctx, NULL, BAD_CAST "a:b") < 0,
          "prefixed name");
    check(!xp3StaticContextHasVariable(NULL, NULL, BAD_CAST "v"),
          "no variables without a context");
    xp3FreeStaticContext(ctx);
}

/*
 * The API
 */

static void
testApi(void) {
    xp3StaticContext *ctx = testContext(XP3_XPATH_31);
    xp3Error *err = NULL;
    xp3Expr *compiled;
    xmlChar *tree;

    /* Without a context: the predeclared prefixes, nothing else */
    compiled = xp3Compile(NULL, BAD_CAST "fn:true() and math:pi() > 3", &err);
    check((compiled != NULL) && (err == NULL) &&
          (compiled->level == XP3_XPATH_31), "NULL context");
    xp3FreeExpr(compiled);
    compiled = xp3Compile(NULL, BAD_CAST "$v", &err);
    check((compiled == NULL) && (err != NULL), "no variables without context");
    xp3ErrorFree(err);
    check(xmlStrEqual(xp3StaticContextLookupNs(NULL, BAD_CAST "xs"),
                      BAD_CAST XP3_NS_XS) &&
          (xp3StaticContextLookupNs(NULL, BAD_CAST "ex") == NULL) &&
          (xp3StaticContextLookupNs(NULL, NULL) == NULL),
          "predeclared prefixes without a context");

    check(xp3Compile(ctx, BAD_CAST "p:a", NULL) == NULL,
          "error without an error pointer");
    compiled = xp3Compile(ctx, NULL, &err);
    check((compiled == NULL) && (err != NULL) &&
          (strcmp((const char *) xp3ErrorCode(err), "XPST0003") == 0),
          "NULL expression is empty");
    xp3ErrorFree(err);
    xp3FreeExpr(NULL);

    /* The compiled expression does not depend on the context */
    compiled = xp3Compile(ctx, BAD_CAST "ex:a/b, $ex:w, math:pi()", NULL);
    xp3FreeStaticContext(ctx);
    tree = (compiled != NULL) ? xp3AstToString(compiled->ast->root) : NULL;
    check((tree != NULL) &&
          (strcmp((const char *) tree,
                  "(sequence (path (step child (name " EX "a)) "
                  "(step child (name " DEF "b))) (var " EX "w) "
                  "(call Q{" XP3_NS_MATH "}pi))") == 0),
          "names survive freeing the context");
    xmlFree(tree);
    xp3FreeExpr(compiled);

    /* The default element namespace can be removed again */
    ctx = testContext(XP3_XPATH_31);
    check(xp3SetDefaultElementNamespace(ctx, BAD_CAST "") == 0,
          "remove the default namespace");
    tree = compileToString(ctx, "a", NULL);
    check((tree != NULL) &&
          (strcmp((const char *) tree, "(path (step child (name a)))") == 0),
          "no default namespace");
    xmlFree(tree);
    check(xp3SetDefaultElementNamespace(NULL, NULL) < 0, "NULL context");
    xp3FreeStaticContext(ctx);
}

/*
 * Allocation failures
 */

/*
 * An expression that uses more than XP3_URI_CACHE namespaces from the
 * context, more than 16 variables at once, declared variables and a
 * constructor function.
 */
static const char largeExpr[] =
    "for $a in 1, $b in 2, $c in 3, $d in 4, $e in 5, $f in 6, $g in 7, "
    "$h in 8, $i in 9, $j in 10, $k in 11, $l in 12, $m in 13, $n in 14, "
    "$o in 15, $p in 16, $q in 17 return "
    "(xs:integer($q), ex:a/@xml:lang, b, err:c, xsi:d, fn:true(), "
    "math:pi(), map:size(map{}), array:size([]), $v, $ex:w, $v)";

/* Folding with errors, a range, pruning and a rewritten path */
static const char foldExpr[] =
    "for $i in 1 to 3 return $i * 2, if (0) then 1 div 0 else 1, "
    "//x[@id = 'a']/y, (1 to 5)[. > 3], $v and 0, if (1 to 1) then 2 "
    "else 3, -(1.5e-2 + 4 idiv 2 mod 3) * (1, 2)[. castable as xs:double]";

/*
 * Compile with each allocation failing in turn: compiling must then
 * fail with NO_MEMORY, never hide the failure, and succeed completely
 * once nothing fails.
 */
static void
allocationFailures(int optimizing, const char *expr) {
    xp3StaticContext *ctx = testContext(XP3_XPATH_31);
    xmlChar *expected;
    int i;

    if (ctx == NULL) {
        check(0, "context for allocation failures");
        return;
    }
    ctx->noOptimize = !optimizing;
    expected = compileToString(ctx, expr, NULL);
    check(expected != NULL, "reference tree compiled");

    if (!xp3TestAllocInstall()) {
        printf("teststatic: allocation-failure tests skipped: this "
               "libxml2 ignores xmlMemSetup()\n");
        xp3TestAllocRestore();
        xmlFree(expected);
        xp3FreeStaticContext(ctx);
        return;
    }
    for (i = 0; i < 5000; i++) {
        xp3Error *err = NULL;
        xmlChar *actual;

        xp3TestAllocFailAt(i);
        actual = compileToString(ctx, expr, &err);
        if (!xp3TestAllocFailed()) {
            check((actual != NULL) && (expected != NULL) &&
                  xmlStrEqual(actual, expected),
                  "tree complete after allocation failures");
            xp3TestAllocFailAt(-1);
            xmlFree(actual);
            xp3ErrorFree(err);
            break;
        }
        xp3TestAllocFailAt(-1);
        /* A failure while printing leaves no error, only NULL */
        check(actual == NULL, "failed run returns no tree");
        check((err == NULL) ||
              (strcmp((const char *) xp3ErrorCode(err), "NO_MEMORY") == 0),
              "failed run reports NO_MEMORY");
        xmlFree(actual);
        xp3ErrorFree(err);
    }
    check((i > 0) && (i < 5000), "allocation failure points exercised");

    /* Running out of memory while reporting a static error */
    for (i = 0; i < 1000; i++) {
        xp3Error *err = NULL;
        xp3Expr *compiled;

        xp3TestAllocFailAt(i);
        compiled = xp3Compile(ctx, BAD_CAST "1 + $undeclared", &err);
        if (!xp3TestAllocFailed()) {
            check((compiled == NULL) && (err != NULL) &&
                  (strcmp((const char *) xp3ErrorCode(err), "XPST0008") == 0),
                  "static error after allocation failures");
            xp3TestAllocFailAt(-1);
            xp3ErrorFree(err);
            break;
        }
        xp3TestAllocFailAt(-1);
        check((compiled == NULL) && (err != NULL) &&
              ((strcmp((const char *) xp3ErrorCode(err), "NO_MEMORY") == 0) ||
               (strcmp((const char *) xp3ErrorCode(err), "XPST0008") == 0)),
              "failed run reports NO_MEMORY or the static error");
        xp3ErrorFree(err);
    }
    check((i > 0) && (i < 1000), "error allocation failure points");
    xp3TestAllocRestore();
    xmlFree(expected);
    xp3FreeStaticContext(ctx);
}

static void
testAllocationFailures(void) {
    allocationFailures(0, largeExpr);
    allocationFailures(1, largeExpr);
    allocationFailures(1, foldExpr);
}

/*
 * Optimizing: constant folding, pruning, path order and rewrites
 */

static const struct {
    const char *expr;
    const char *tree;
} optimizeCases[] = {
    /* Folding */
    { "1", "(constant integer:1)" },
    { "1 + 2", "(constant integer:3)" },
    { "(1, 'a', 2.5)", "(constant integer:1 string:a decimal:2.5)" },
    { "(1, ())", "(constant integer:1)" },
    { "()", "(empty)" },
    { "1 to 1000000", "(constant integer:1..1000000)" },
    { "(1 to 5)[. > 3]", "(constant integer:4 integer:5)" },
    { "for $i in 1 to 3 return $i * 2",
      "(constant integer:2 integer:4 integer:6)" },
    { "for $x in 1 return (for $y in 2 return $x + $y)",
      "(constant integer:3)" },
    { "'12' cast as xs:integer", "(constant integer:12)" },
    { "1 instance of xs:integer", "(constant boolean:true)" },
    { "if (0) then 1 div 0 else 1", "(constant integer:1)" },

    /* Not closed: only the closed parts fold */
    { "$v + 1", "(binary + (var v) (constant integer:1))" },
    { ". + 1", "(binary + (context) (constant integer:1))" },
    { "for $i in 1 to 3 return $v",
      "(for (bind i (constant integer:1..3)) (var v))" },
    { "let $a := 1 return $a + $v",
      "(let (bind a (constant integer:1)) (binary + (var a) (var v)))" },
    { "for $x in $v return (for $y in 2 return $y)",
      "(for (bind x (var v)) (constant integer:2))" },
    { "for $x in $v return (for $y in 2 return $x + $y)",
      "(for (bind x (var v)) (for (bind y (constant integer:2)) "
      "(binary + (var x) (var y))))" },
    { "count(1 + 1)",
      "(call Q{http://www.w3.org/2005/xpath-functions}count "
      "(constant integer:2))" },

    /* Errors stay for evaluation to raise */
    { "1 div 0", "(binary div (constant integer:1) (constant integer:0))" },
    { "(1, 2) + 1", "(binary + (constant integer:1 integer:2) "
      "(constant integer:1))" },
    { "'a' cast as xs:integer", "(cast-as (constant string:a) "
      "(type (atomic Q{http://www.w3.org/2001/XMLSchema}integer)))" },

    /* Pruning */
    { "if (1) then $v else 2", "(var v)" },
    { "if (0) then $v else 2", "(constant integer:2)" },
    { "if ($v) then 1 + 1 else 3",
      "(if (var v) (constant integer:2) (constant integer:3))" },
    { "if ((1, 2)) then $v else 2",
      "(if (constant integer:1 integer:2) (var v) (constant integer:2))" },
    { "$v and 0", "(constant boolean:false)" },
    { "0 and $v", "(constant boolean:false)" },
    { "$v or 'x'", "(constant boolean:true)" },
    { "$v and 1", "(binary and (var v) (constant integer:1))" },
    { "$v or ''", "(binary or (var v) (constant string:))" },

    /* Path order and the rewrite */
    { "/a/b", "(path root (step child sorted (name a)) "
      "(step child sorted (name b)))" },
    { "a/b", "(path (step child (name a)) (step child sorted (name b)))" },
    { "a/@b", "(path (step child (name a)) "
      "(step attribute sorted (name b)))" },
    { "a//b", "(path (step child (name a)) "
      "(step descendant sorted (name b)))" },
    { "//x", "(path root (step descendant sorted (name x)))" },
    { "//x[@id]", "(path root (step descendant sorted (name x) "
      "(predicate (path (step attribute (name id))))))" },
    { "//x[@id = 'a']/y",
      "(path root (step descendant sorted (name x) (predicate "
      "(binary = (path (step attribute (name id))) (constant string:a)))) "
      "(step child (name y)))" },
    { "//x[1]", "(path root (step descendant-or-self sorted (kind node)) "
      "(step child (name x) (predicate (constant integer:1))))" },
    { "//x[$v]", "(path root (step descendant-or-self sorted (kind node)) "
      "(step child (name x) (predicate (var v))))" },
    /* Boolean predicates that read the position aren't rewritten, since
     * position() = 1 picks the first x child of each node, not the
     * first x descendant */
    { "//x[position() = 1]",
      "(path root (step descendant-or-self sorted (kind node)) "
      "(step child (name x) (predicate (binary = (call "
      "Q{http://www.w3.org/2005/xpath-functions}position) "
      "(constant integer:1)))))" },
    { "//x[last() = position()]",
      "(path root (step descendant-or-self sorted (kind node)) "
      "(step child (name x) (predicate (binary = (call "
      "Q{http://www.w3.org/2005/xpath-functions}last) (call "
      "Q{http://www.w3.org/2005/xpath-functions}position)))))" },
    { "//x[some $p in position() satisfies $p = 1]",
      "(path root (step descendant-or-self sorted (kind node)) "
      "(step child (name x) (predicate (some (bind p (call "
      "Q{http://www.w3.org/2005/xpath-functions}position)) "
      "(binary = (var p) (constant integer:1))))))" },
    { "//x[y[last()]]",
      "(path root (step descendant-or-self sorted (kind node)) "
      "(step child (name x) (predicate (path (step child (name y) "
      "(predicate (call Q{http://www.w3.org/2005/xpath-functions}last))"
      ")))))" },
    /* Other focus-dependent functions only read the context item */
    { "//x[name() = 'x']",
      "(path root (step descendant sorted (name x) (predicate (binary = "
      "(call Q{http://www.w3.org/2005/xpath-functions}name) "
      "(constant string:x)))))" },
    { "//@x", "(path root (step descendant-or-self sorted (kind node)) "
      "(step attribute (name x)))" },
    { "a/..", "(path (step child (name a)) (step parent (kind node)))" },
    { "a/self::b",
      "(path (step child (name a)) (step self sorted (name b)))" },
    { "//x[. instance of element()]",
      "(path root (step descendant sorted (name x) (predicate (instance-of "
      "(context) (type (kind element))))))" },
    { "$v/a/b", "(path (var v) (step child (name a)) (step child (name b)))" },
    { ".//x", "(path (context) (step descendant (name x)))" },
    { "(a, b)/c", "(path (sequence (path (step child (name a))) "
      "(path (step child (name b)))) (step child (name c)))" },
    { "/a/following-sibling::b",
      "(path root (step child sorted (name a)) "
      "(step following-sibling (name b)))" },
};

/* Boolean predicates that read the position in other ways */
static const char *const positionalCases[] = {
    "//x[some $f in position#0 satisfies $f() = 1]",
    "//x[some $f in fn:last#0 satisfies $f() = 1]",
    "//x[function-lookup(xs:QName('fn:position'), 0)() = 1]",
    "//x[function-lookup#2(xs:QName('fn:last'), 0)() = 1]",
    "//x[(position() => string()) = '1']",
    "//x[. = position()]"
};

/* A context that optimizes, with $v declared */
static xp3StaticContext *
optimizingContext(void) {
    xp3StaticContext *ctx = xp3NewStaticContext();

    if ((ctx == NULL) || (xp3DeclareVariable(ctx, NULL, BAD_CAST "v") < 0)) {
        xp3FreeStaticContext(ctx);
        return(NULL);
    }
    return(ctx);
}

static void
testOptimize(void) {
    xp3StaticContext *ctx = optimizingContext();
    xp3Error *err = NULL;
    xp3Expr *compiled;
    size_t i;

    if (ctx == NULL) {
        check(0, "optimizing context created");
        return;
    }
    for (i = 0; i < sizeof(optimizeCases) / sizeof(optimizeCases[0]); i++) {
        xmlChar *tree = compileToString(ctx, optimizeCases[i].expr, &err);

        if ((tree == NULL) ||
            !xmlStrEqual(tree, BAD_CAST optimizeCases[i].tree)) {
            fprintf(stderr, "FAIL: '%s': expected %s, got %s\n",
                    optimizeCases[i].expr, optimizeCases[i].tree,
                    (tree != NULL) ? (const char *) tree :
                    (const char *) xp3ErrorCode(err));
            failures++;
        }
        xmlFree(tree);
        xp3ErrorFree(err);
        err = NULL;
    }

    /* Position read through a function item or an arrow: //x stays
     * descendant-or-self::node()/child::x */
    for (i = 0; i < sizeof(positionalCases) / sizeof(positionalCases[0]);
         i++) {
        xmlChar *tree = compileToString(ctx, positionalCases[i], &err);
        const char *kept = "(path root (step descendant-or-self sorted "
                           "(kind node)) (step child (name x) ";

        if ((tree == NULL) ||
            (xmlStrncmp(tree, BAD_CAST kept, (int) strlen(kept)) != 0)) {
            fprintf(stderr, "FAIL: '%s' was rewritten: %s\n",
                    positionalCases[i],
                    (tree != NULL) ? (const char *) tree :
                    (const char *) xp3ErrorCode(err));
            failures++;
        }
        xmlFree(tree);
        xp3ErrorFree(err);
        err = NULL;
    }

    /* Folds that would take too long or build too much are left */
    compiled = xp3Compile(ctx, BAD_CAST "(1 to 20000)[. > 0]", NULL);
    check((compiled != NULL) &&
          (compiled->ast->root->kind == XP3_AST_FILTER),
          "a fold with too many items is left");
    xp3FreeExpr(compiled);
    compiled = xp3Compile(ctx, BAD_CAST "(1 to 200)[(1 to 200)[. > 0] = .]",
                          NULL);
    check((compiled != NULL) &&
          (compiled->ast->root->kind == XP3_AST_FILTER),
          "a fold with too many steps is left");
    xp3FreeExpr(compiled);

    /* Without optimizing, nothing changes */
    ctx->noOptimize = 1;
    compiled = xp3Compile(ctx, BAD_CAST "1 + 2", NULL);
    check((compiled != NULL) &&
          (compiled->ast->root->kind == XP3_AST_BINARY) &&
          (compiled->constantCount == 0), "optimizing can be turned off");
    xp3FreeExpr(compiled);
    xp3FreeStaticContext(ctx);
}

/*
 * Many closed subexpressions that fail to fold, each nearly at the
 * budget: the total budget keeps compiling fast.
 */
static void
testFoldBudget(void) {
    size_t parts = 300;
    const char *part = "(1 to 9000)[. > 0][9000] div 0, ";
    size_t size = strlen(part) * parts + 8;
    char *expr = xmlMalloc(size);
    xp3Expr *compiled;
    size_t used = 0;
    size_t i;

    if (expr == NULL) {
        check(0, "budget expression allocated");
        return;
    }
    used += (size_t) snprintf(expr + used, size - used, "(");
    for (i = 0; i < parts; i++)
        used += (size_t) snprintf(expr + used, size - used, "%s", part);
    snprintf(expr + used, size - used, "1)");
    compiled = xp3Compile(NULL, BAD_CAST expr, NULL);
    check(compiled != NULL, "many failing folds still compile");
    xp3FreeExpr(compiled);
    xmlFree(expr);
}

int
main(void) {
    xmlInitParser();

    testTrees();
    testErrors();
    testMessages();
    testSlots();
    testDeclaredVariables();
    testApi();
    testOptimize();
    testFoldBudget();
    testAllocationFailures();

    if (failures == 0)
        printf("teststatic: OK\n");
    return(failures == 0 ? 0 : 1);
}
