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

static void
testAllocationFailures(void) {
    xp3StaticContext *ctx = testContext(XP3_XPATH_31);
    xmlChar *expected;
    int i;

    expected = compileToString(ctx, largeExpr, NULL);
    check(expected != NULL, "reference tree compiled");

    if (!xp3TestAllocInstall()) {
        printf("teststatic: allocation-failure tests skipped: this "
               "libxml2 ignores xmlMemSetup()\n");
        xp3TestAllocRestore();
        xmlFree(expected);
        xp3FreeStaticContext(ctx);
        return;
    }
    for (i = 0; i < 1000; i++) {
        xp3Error *err = NULL;
        xmlChar *actual;

        xp3TestAllocFailAt(i);
        actual = compileToString(ctx, largeExpr, &err);
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
    check((i > 0) && (i < 1000), "allocation failure points exercised");

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

int
main(void) {
    xmlInitParser();

    testTrees();
    testErrors();
    testMessages();
    testSlots();
    testDeclaredVariables();
    testApi();
    testAllocationFailures();

    if (failures == 0)
        printf("teststatic: OK\n");
    return(failures == 0 ? 0 : 1);
}
