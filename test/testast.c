/*
 * testast.c: tests for the abstract syntax tree and its arena
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/xmlmemory.h>

#include "private/xp3ast.h"
#include "testalloc.h"

static int failures = 0;

static void
check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

/* Create a node with an operator and optional text. NULL on failure. */
static xp3AstNode *
mk(xp3Ast *ast, xp3AstKind kind, int op, const char *text) {
    xp3AstNode *node = xp3AstNewNode(ast, kind, 1, 1);

    if (node == NULL)
        return(NULL);
    node->op = op;
    if (text != NULL) {
        node->text = xp3AstStrndup(ast, BAD_CAST text, strlen(text));
        if (node->text == NULL)
            return(NULL);
    }
    return(node);
}

/* Create a node with a name. NULL on failure. */
static xp3AstNode *
mkName(xp3Ast *ast, xp3AstKind kind, int op, const char *prefix,
       const char *uri, const char *local) {
    xp3AstNode *node = mk(ast, kind, op, local);

    if (node == NULL)
        return(NULL);
    if (prefix != NULL) {
        node->prefix = xp3AstStrndup(ast, BAD_CAST prefix, strlen(prefix));
        if (node->prefix == NULL)
            return(NULL);
    }
    if (uri != NULL) {
        node->uri = xp3AstStrndup(ast, BAD_CAST uri, strlen(uri));
        if (node->uri == NULL)
            return(NULL);
    }
    return(node);
}

/* Append the NULL-terminated list of children. Returns parent. */
static xp3AstNode *
add(xp3AstNode *parent, ...) {
    va_list args;
    xp3AstNode *child;

    va_start(args, parent);
    while ((child = va_arg(args, xp3AstNode *)) != NULL)
        xp3AstAppend(parent, child);
    va_end(args);
    return(parent);
}

/* Shorthands for frequent nodes */
#define INT(v)     mk(ast, XP3_AST_INTEGER, 0, v)
#define VAR(v)     mkName(ast, XP3_AST_VARREF, 0, NULL, NULL, v)
#define ATOMIC(p, l) mkName(ast, XP3_AST_ATOMIC_TYPE, 0, p, NULL, l)
#define TYPE(occ, item) add(mk(ast, XP3_AST_SEQUENCE_TYPE, occ, NULL), item, NULL)

static void
expectTree(const xp3AstNode *node, const char *expected, const char *what) {
    xmlChar *actual = xp3AstToString(node);
    char msg[1024];

    snprintf(msg, sizeof(msg), "%s:\n   got      %s\n   expected %s", what,
             actual ? (const char *) actual : "(null)", expected);
    check((actual != NULL) && (strcmp((const char *) actual, expected) == 0),
          msg);
    xmlFree(actual);
}

static void
testPrinter(void) {
    xp3Ast *ast = xp3AstNew();
    xp3AstNode *n;

    /* a[1] + 2 */
    n = add(mk(ast, XP3_AST_BINARY, XP3_OP_ADD, NULL),
            add(mk(ast, XP3_AST_PATH, XP3_PATH_RELATIVE, NULL),
                add(mk(ast, XP3_AST_STEP, XP3_AXIS_CHILD, NULL),
                    mkName(ast, XP3_AST_NAME_TEST, XP3_NAMETEST_NAME,
                           NULL, NULL, "a"),
                    add(mk(ast, XP3_AST_PREDICATE, 0, NULL), INT("1"), NULL),
                    NULL),
                NULL),
            INT("2"), NULL);
    expectTree(n, "(binary + (path (step child (name a) (predicate "
                  "(integer 1)))) (integer 2))", "a[1] + 2");

    /* Literals, with escaping in strings */
    n = add(mk(ast, XP3_AST_SEQUENCE, 0, NULL),
            INT("42"), mk(ast, XP3_AST_DECIMAL, 0, ".5"),
            mk(ast, XP3_AST_DOUBLE, 0, "1e3"),
            mk(ast, XP3_AST_STRING, 0, "it's \"q\" \\"),
            mk(ast, XP3_AST_EMPTY_SEQUENCE, 0, NULL),
            mk(ast, XP3_AST_CONTEXT_ITEM, 0, NULL), NULL);
    expectTree(n, "(sequence (integer 42) (decimal .5) (double 1e3) "
                  "(string \"it's \\\"q\\\" \\\\\") (empty) (context))",
               "literals");

    /* Names and name tests */
    n = add(mk(ast, XP3_AST_SEQUENCE, 0, NULL),
            mkName(ast, XP3_AST_VARREF, 0, "p", NULL, "x"),
            mkName(ast, XP3_AST_VARREF, 0, NULL, "urn:x", "y"),
            mk(ast, XP3_AST_NAME_TEST, XP3_NAMETEST_ANY, NULL),
            mkName(ast, XP3_AST_NAME_TEST, XP3_NAMETEST_ANY_LOCAL, "p",
                   NULL, NULL),
            mkName(ast, XP3_AST_NAME_TEST, XP3_NAMETEST_ANY_LOCAL, NULL,
                   "urn:x", NULL),
            mk(ast, XP3_AST_NAME_TEST, XP3_NAMETEST_ANY_NS, "local"),
            mkName(ast, XP3_AST_NAMED_FUNCTION_REF, 3, "fn", NULL,
                   "concat"),
            add(mkName(ast, XP3_AST_FUNCTION_CALL, 0, NULL, NULL, "f"),
                INT("1"), mk(ast, XP3_AST_ARG_PLACEHOLDER, 0, NULL), NULL),
            NULL);
    expectTree(n, "(sequence (var p:x) (var Q{urn:x}y) (name *) (name p:*) "
                  "(name Q{urn:x}*) (name *:local) (function-ref "
                  "fn:concat#3) (call f (integer 1) (placeholder)))",
               "names");

    /* //b: root, explicit descendant-or-self::node() step, child step */
    n = add(mk(ast, XP3_AST_PATH, XP3_PATH_ROOT, NULL),
            add(mk(ast, XP3_AST_STEP, XP3_AXIS_DESCENDANT_OR_SELF, NULL),
                mk(ast, XP3_AST_KIND_TEST, XP3_KIND_ANY, NULL), NULL),
            add(mk(ast, XP3_AST_STEP, XP3_AXIS_CHILD, NULL),
                mkName(ast, XP3_AST_NAME_TEST, XP3_NAMETEST_NAME, NULL,
                       NULL, "b"), NULL),
            NULL);
    expectTree(n, "(path root (step descendant-or-self (kind node)) "
                  "(step child (name b)))", "//b");
    expectTree(mk(ast, XP3_AST_PATH, XP3_PATH_ROOT, NULL), "(path root)",
               "lone /");

    /* Kind tests */
    n = add(mk(ast, XP3_AST_SEQUENCE, 0, NULL),
            add(mk(ast, XP3_AST_KIND_TEST, XP3_KIND_ELEMENT, NULL),
                mkName(ast, XP3_AST_NAME_TEST, XP3_NAMETEST_NAME, NULL,
                       NULL, "a"),
                TYPE(XP3_OCC_OPTIONAL, ATOMIC("xs", "string")), NULL),
            mk(ast, XP3_AST_KIND_TEST, XP3_KIND_PI, "target"),
            add(mk(ast, XP3_AST_KIND_TEST, XP3_KIND_DOCUMENT, NULL),
                mk(ast, XP3_AST_KIND_TEST, XP3_KIND_ELEMENT, NULL), NULL),
            mk(ast, XP3_AST_KIND_TEST, XP3_KIND_NAMESPACE_NODE, NULL),
            NULL);
    expectTree(n, "(sequence (kind element (name a) (type ? (atomic "
                  "xs:string))) (kind processing-instruction target) "
                  "(kind document-node (kind element)) "
                  "(kind namespace-node))", "kind tests");

    /* Control */
    n = add(mk(ast, XP3_AST_FOR, 0, NULL),
            add(mkName(ast, XP3_AST_BINDING, 0, NULL, NULL, "x"),
                INT("1"), NULL),
            VAR("x"), NULL);
    expectTree(n, "(for (bind x (integer 1)) (var x))", "for");
    n = add(mk(ast, XP3_AST_IF, 0, NULL), VAR("c"), INT("1"),
            mk(ast, XP3_AST_EMPTY_SEQUENCE, 0, NULL), NULL);
    expectTree(n, "(if (var c) (integer 1) (empty))", "if");

    /* Types */
    n = add(mk(ast, XP3_AST_INSTANCE_OF, 0, NULL), VAR("x"),
            TYPE(XP3_OCC_ZERO_OR_MORE,
                 mk(ast, XP3_AST_ANY_ITEM_TYPE, 0, NULL)), NULL);
    expectTree(n, "(instance-of (var x) (type * (item)))", "instance of");
    n = add(mk(ast, XP3_AST_CAST_AS, 0, NULL),
            mk(ast, XP3_AST_STRING, 0, "1"),
            TYPE(XP3_OCC_OPTIONAL, ATOMIC("xs", "integer")), NULL);
    expectTree(n, "(cast-as (string \"1\") (type ? (atomic xs:integer)))",
               "cast as");
    n = add(mk(ast, XP3_AST_SEQUENCE, 0, NULL),
            mk(ast, XP3_AST_FUNCTION_TEST, 0, NULL),
            add(mk(ast, XP3_AST_MAP_TEST, 1, NULL), ATOMIC("xs", "string"),
                TYPE(XP3_OCC_ONE, mk(ast, XP3_AST_ANY_ITEM_TYPE, 0, NULL)),
                NULL),
            mk(ast, XP3_AST_ARRAY_TEST, 0, NULL),
            mk(ast, XP3_AST_EMPTY_SEQUENCE_TYPE, 0, NULL), NULL);
    expectTree(n, "(sequence (function-test *) (map-test (atomic xs:string) "
                  "(type (item))) (array-test *) (empty-sequence))",
               "item types");

    /* Operators, arrows and functions */
    n = add(mk(ast, XP3_AST_UNARY, XP3_OP_SUB, NULL), INT("1"), NULL);
    expectTree(n, "(unary - (integer 1))", "unary minus");
    n = add(mkName(ast, XP3_AST_ARROW, XP3_ARROW_NAME, NULL, NULL, "f"),
            VAR("x"), INT("2"), NULL);
    expectTree(n, "(arrow f (var x) (integer 2))", "arrow with name");
    n = add(mk(ast, XP3_AST_ARROW, XP3_ARROW_EXPR, NULL), VAR("x"),
            VAR("f"), NULL);
    expectTree(n, "(arrow (var x) (var f))", "arrow with expression");
    n = add(mk(ast, XP3_AST_INLINE_FUNCTION, 0, NULL),
            add(mkName(ast, XP3_AST_PARAM, 0, NULL, NULL, "x"),
                TYPE(XP3_OCC_ONE, ATOMIC("xs", "integer")), NULL),
            TYPE(XP3_OCC_ONE, ATOMIC("xs", "integer")),
            add(mk(ast, XP3_AST_BINARY, XP3_OP_ADD, NULL), VAR("x"),
                INT("1"), NULL),
            NULL);
    expectTree(n, "(function (param x (type (atomic xs:integer))) "
                  "(type (atomic xs:integer)) (binary + (var x) "
                  "(integer 1)))", "inline function");

    /* Constructors and lookups */
    n = add(mk(ast, XP3_AST_SEQUENCE, 0, NULL),
            add(mk(ast, XP3_AST_MAP, 0, NULL),
                add(mk(ast, XP3_AST_MAP_ENTRY, 0, NULL),
                    mk(ast, XP3_AST_STRING, 0, "a"), INT("1"), NULL),
                NULL),
            add(mk(ast, XP3_AST_ARRAY_SQUARE, 0, NULL), INT("1"), INT("2"),
                NULL),
            mk(ast, XP3_AST_ARRAY_CURLY, 0, NULL),
            mk(ast, XP3_AST_UNARY_LOOKUP, XP3_LOOKUP_WILDCARD, NULL),
            add(mk(ast, XP3_AST_FILTER, 0, NULL), VAR("m"),
                mk(ast, XP3_AST_LOOKUP, XP3_LOOKUP_NAME, "key"),
                mk(ast, XP3_AST_LOOKUP, XP3_LOOKUP_INTEGER, "2"),
                add(mk(ast, XP3_AST_LOOKUP, XP3_LOOKUP_EXPR, NULL),
                    VAR("k"), NULL),
                add(mk(ast, XP3_AST_DYNAMIC_CALL, 0, NULL), INT("1"), NULL),
                NULL),
            NULL);
    expectTree(n, "(sequence (map (entry (string \"a\") (integer 1))) "
                  "(array-square (integer 1) (integer 2)) (array-curly) "
                  "(unary-lookup *) (filter (var m) (lookup key) "
                  "(lookup 2) (lookup (var k)) (dynamic-call "
                  "(integer 1))))", "constructors and lookups");

    /* Out-of-range values print as "?" instead of crashing */
    n = add(mk(ast, XP3_AST_BINARY, 999, NULL), INT("1"), INT("2"), NULL);
    expectTree(n, "(binary ? (integer 1) (integer 2))", "unknown operator");
    check(xp3AstToString(NULL) == NULL, "NULL node prints as NULL");

    xp3AstFree(ast);
}

static void
testNodes(void) {
    xp3Ast *ast = xp3AstNew();
    xp3AstNode *parent = mk(ast, XP3_AST_SEQUENCE, 0, NULL);
    xp3AstNode *a = INT("1");
    xp3AstNode *b = INT("2");
    int kind;

    check(parent->line == 1 && parent->column == 1, "position stored");
    check(parent->first == NULL && parent->text == NULL &&
          parent->prefix == NULL && parent->uri == NULL,
          "new node is zeroed");

    xp3AstAppend(parent, a);
    xp3AstAppend(parent, b);
    xp3AstAppend(parent, NULL);
    xp3AstAppend(NULL, a);
    check(xp3AstChildCount(parent) == 2, "two children");
    check(parent->first == a && parent->last == b && a->next == b &&
          b->next == NULL, "children in order");
    check(xp3AstChildCount(NULL) == 0, "child count of NULL");

    check(xp3AstNewNode(NULL, XP3_AST_INTEGER, 1, 1) == NULL,
          "node without a tree");
    check(xp3AstStrndup(ast, NULL, 3) == NULL, "NULL string with length");
    check(xp3AstStrndup(ast, NULL, 0) != NULL &&
          xp3AstStrndup(ast, NULL, 0)[0] == 0, "empty string");
    check(strcmp((const char *) xp3AstStrndup(ast, BAD_CAST "abcdef", 3),
                 "abc") == 0, "string copied up to length");

    for (kind = XP3_AST_NONE; kind <= XP3_AST_ARRAY_TEST; kind++)
        check(xp3AstKindName((xp3AstKind) kind) != NULL,
              "every kind has a name");
    check(xp3AstKindName((xp3AstKind) 9999) == NULL,
          "unknown kind has no name");

    xp3AstFree(ast);
    xp3AstFree(NULL);
}

static void
testArena(void) {
    xp3Ast *ast = xp3AstNew();
    xp3AstNode *root = mk(ast, XP3_AST_SEQUENCE, 0, NULL);
    static char big[2 * 1024 * 1024 + 7];   /* larger than a block */
    const xmlChar *bigCopy;
    const xmlChar *small;
    xp3AstNode *child;
    int i;
    int ok;

    /* Many nodes spread over several blocks */
    for (i = 0; i < 10000; i++) {
        char text[16];
        xp3AstNode *node;

        snprintf(text, sizeof(text), "%d", i);
        node = INT(text);
        check(node != NULL, "node allocated");
        check(((uintptr_t) node % sizeof(void *)) == 0, "node aligned");
        xp3AstAppend(root, node);
    }
    check(xp3AstChildCount(root) == 10000, "10000 children");
    ok = 1;
    for (child = root->first, i = 0; child != NULL; child = child->next, i++) {
        char text[16];

        snprintf(text, sizeof(text), "%d", i);
        if (strcmp((const char *) child->text, text) != 0)
            ok = 0;
    }
    check(ok, "all nodes keep their values across blocks");

    /* A string larger than the block limit gets a block of its own */
    memset(big, 'x', sizeof(big));
    bigCopy = xp3AstStrndup(ast, BAD_CAST big, sizeof(big));
    check(bigCopy != NULL && bigCopy[0] == 'x' &&
          bigCopy[sizeof(big) - 1] == 'x' && bigCopy[sizeof(big)] == 0,
          "large string copied");
    small = xp3AstStrndup(ast, BAD_CAST "after", 5);
    check(small != NULL && strcmp((const char *) small, "after") == 0,
          "allocation after a large block");
    check(strcmp((const char *) root->first->text, "0") == 0,
          "earlier nodes unaffected");

    xp3AstFree(ast);
}

/* Build a small tree with a large string and print it. Returns the
 * printed tree, or NULL if any allocation failed. */
static xmlChar *
buildAndPrint(xp3Ast *ast) {
    static char long1[5000];
    xp3AstNode *root;
    xp3AstNode *str;

    memset(long1, 'y', sizeof(long1) - 1);
    root = add(mk(ast, XP3_AST_BINARY, XP3_OP_ADD, NULL),
               add(mk(ast, XP3_AST_PATH, XP3_PATH_ROOT, NULL),
                   add(mk(ast, XP3_AST_STEP, XP3_AXIS_CHILD, NULL),
                       mkName(ast, XP3_AST_NAME_TEST, XP3_NAMETEST_NAME,
                              "p", NULL, "a"), NULL),
                   NULL),
               INT("2"), NULL);
    str = mk(ast, XP3_AST_STRING, 0, long1);
    if ((root == NULL) || (str == NULL) || (xp3AstChildCount(root) != 2) ||
        (root->first->first == NULL) || (root->first->first->first == NULL))
        return(NULL);
    xp3AstAppend(root, str);
    ast->root = root;
    return(xp3AstToString(root));
}

static void
testAllocationFailures(void) {
    xmlChar *expected;
    xp3Ast *ast;
    int i;

    ast = xp3AstNew();
    expected = buildAndPrint(ast);
    xp3AstFree(ast);
    check(expected != NULL, "reference tree printed");

    if (!xp3TestAllocInstall()) {
        printf("testast: allocation-failure tests skipped: this libxml2 "
               "ignores xmlMemSetup()\n");
        xp3TestAllocRestore();
        xmlFree(expected);
        return;
    }

    /* Fail each allocation in turn: creating the tree, the arena
     * blocks and the printer's buffer. Every run must either fail
     * cleanly or produce the full result, without leaks. */
    for (i = 0; i < 1000; i++) {
        xmlChar *actual = NULL;

        xp3TestAllocFailAt(i);
        ast = xp3AstNew();
        if (ast != NULL)
            actual = buildAndPrint(ast);
        if (!xp3TestAllocFailed()) {
            check(actual != NULL && expected != NULL &&
                  xmlStrEqual(actual, expected),
                  "tree complete after allocation failures");
            xp3TestAllocFailAt(-1);
            xmlFree(actual);
            xp3AstFree(ast);
            break;
        }
        check(actual == NULL, "failed run returns no result");
        xp3TestAllocFailAt(-1);
        xmlFree(actual);
        xp3AstFree(ast);
    }
    check(i > 0 && i < 1000, "allocation failure points exercised");

    xp3TestAllocRestore();
    xmlFree(expected);
}

int
main(void) {
    xmlInitParser();

    testPrinter();
    testNodes();
    testArena();
    testAllocationFailures();

    if (failures == 0)
        printf("testast: OK\n");
    return(failures == 0 ? 0 : 1);
}
