/*
 * testparser.c: tests for the XPath parser
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/xmlmemory.h>

#include <libxpath3/xpath3.h>
#include "private/xp3ast.h"
#include "private/xp3context.h"
#include "private/xp3error.h"
#include "private/xp3parser.h"
#include "testalloc.h"

static int failures = 0;

static void
check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

/*
 * Parse expr and print the tree. Returns the printed tree, to be freed
 * with xmlFree(), or NULL with *err set.
 */
static xmlChar *
parseToString(const xp3StaticContext *ctx, const char *expr,
              xp3Error **err) {
    xp3Ast *ast;
    xmlChar *ret;

    ast = xp3Parse(ctx, BAD_CAST expr, strlen(expr), err);
    if (ast == NULL)
        return(NULL);
    ret = xp3AstToString(ast->root);
    xp3AstFree(ast);
    return(ret);
}

/* Shorthand for the tree of a one-step child path */
#define P(name) "(path (step child (name " name ")))"

static const struct {
    const char *expr;
    const char *tree;
} treeCases[] = {
    /* Literals and primaries */
    { "42", "(integer 42)" },
    { "1.5", "(decimal 1.5)" },
    { "1e3", "(double 1e3)" },
    { "'it''s'", "(string \"it's\")" },
    { "\"a\"\"b\"", "(string \"a\\\"b\")" },
    { "''", "(string \"\")" },
    { "$x", "(var x)" },
    { "$p:x", "(var p:x)" },
    { "$Q{urn:x}y", "(var Q{urn:x}y)" },
    { ".", "(context)" },
    { "()", "(empty)" },
    { "((1))", "(integer 1)" },
    { "1, 2, 3", "(sequence (integer 1) (integer 2) (integer 3))" },
    { "(1, (2, 3))",
      "(sequence (integer 1) (sequence (integer 2) (integer 3)))" },

    /* Precedence and associativity */
    { "1 or 2 and 3",
      "(binary or (integer 1) (binary and (integer 2) (integer 3)))" },
    { "1 or 2 or 3",
      "(binary or (binary or (integer 1) (integer 2)) (integer 3))" },
    { "1 + 2 * 3",
      "(binary + (integer 1) (binary * (integer 2) (integer 3)))" },
    { "1 - 2 - 3",
      "(binary - (binary - (integer 1) (integer 2)) (integer 3))" },
    { "8 div 2 idiv 3 mod 2",
      "(binary mod (binary idiv (binary div (integer 8) (integer 2)) "
      "(integer 3)) (integer 2))" },
    { "1 to 3 = 2",
      "(binary = (binary to (integer 1) (integer 3)) (integer 2))" },
    { "1 + 2 to 3",
      "(binary to (binary + (integer 1) (integer 2)) (integer 3))" },
    { "1 * 2 | 3",
      "(binary * (integer 1) (binary union (integer 2) (integer 3)))" },
    { "a | b union c", "(binary union (binary union " P("a") " " P("b")
      ") " P("c") ")" },
    { "a | b intersect c except d",
      "(binary union " P("a") " (binary except (binary intersect " P("b")
      " " P("c") ") " P("d") "))" },
    { "1 (: comment :) + 2", "(binary + (integer 1) (integer 2))" },

    /* Comparisons */
    { "$a = $b, $a != $b, $a < $b, $a <= $b, $a > $b, $a >= $b",
      "(sequence (binary = (var a) (var b)) (binary != (var a) (var b)) "
      "(binary < (var a) (var b)) (binary <= (var a) (var b)) "
      "(binary > (var a) (var b)) (binary >= (var a) (var b)))" },
    { "$a eq $b, $a ne $b, $a lt $b, $a le $b, $a gt $b, $a ge $b",
      "(sequence (binary eq (var a) (var b)) (binary ne (var a) (var b)) "
      "(binary lt (var a) (var b)) (binary le (var a) (var b)) "
      "(binary gt (var a) (var b)) (binary ge (var a) (var b)))" },
    { "$a is $b, $a << $b, $a >> $b",
      "(sequence (binary is (var a) (var b)) (binary << (var a) (var b)) "
      "(binary >> (var a) (var b)))" },

    /* Unary operators */
    { "-1", "(unary - (integer 1))" },
    { "-+-1", "(unary - (unary + (unary - (integer 1))))" },
    { "1 - -1", "(binary - (integer 1) (unary - (integer 1)))" },
    { "-a/b",
      "(unary - (path (step child (name a)) (step child (name b))))" },

    /* Paths */
    { "/", "(path root)" },
    { "/a", "(path root (step child (name a)))" },
    { "//a", "(path root (step descendant-or-self (kind node)) "
             "(step child (name a)))" },
    { "a/b//c", "(path (step child (name a)) (step child (name b)) "
                "(step descendant-or-self (kind node)) "
                "(step child (name c)))" },
    { "(/) * 5", "(binary * (path root) (integer 5))" },
    { "/ - 1", "(binary - (path root) (integer 1))" },
    { "/div", "(path root (step child (name div)))" },
    { "$x/a", "(path (var x) (step child (name a)))" },
    { "f()/a", "(path (call f) (step child (name a)))" },
    { "a/(b, c)", "(path (step child (name a)) (sequence " P("b") " "
                  P("c") "))" },
    { "ancestor::a/ancestor-or-self::b/attribute::c/child::d/"
      "descendant::e/descendant-or-self::f/following::g/"
      "following-sibling::h/namespace::i/parent::j/preceding::k/"
      "preceding-sibling::l/self::m",
      "(path (step ancestor (name a)) (step ancestor-or-self (name b)) "
      "(step attribute (name c)) (step child (name d)) "
      "(step descendant (name e)) (step descendant-or-self (name f)) "
      "(step following (name g)) (step following-sibling (name h)) "
      "(step namespace (name i)) (step parent (name j)) "
      "(step preceding (name k)) (step preceding-sibling (name l)) "
      "(step self (name m)))" },
    { "@a", "(path (step attribute (name a)))" },
    { "@*", "(path (step attribute (name *)))" },
    { "../a", "(path (step parent (kind node)) (step child (name a)))" },
    { "..[1]", "(path (step parent (kind node) (predicate (integer 1))))" },
    { "*/p:*/*:a/p:a/Q{urn:x}a/Q{urn:x}*/Q{}a",
      "(path (step child (name *)) (step child (name p:*)) "
      "(step child (name *:a)) (step child (name p:a)) "
      "(step child (name Q{urn:x}a)) (step child (name Q{urn:x}*)) "
      "(step child (name Q{}a)))" },

    /* Kind tests, and their default axes */
    { "node()", "(path (step child (kind node)))" },
    { "text ( )", "(path (step child (kind text)))" },
    { "text()|comment()", "(binary union (path (step child (kind text))) "
                          "(path (step child (kind comment))))" },
    { "processing-instruction()",
      "(path (step child (kind processing-instruction)))" },
    { "processing-instruction(t)",
      "(path (step child (kind processing-instruction t)))" },
    { "processing-instruction('t')",
      "(path (step child (kind processing-instruction t)))" },
    { "document-node()", "(path (step child (kind document-node)))" },
    { "document-node(element(a))",
      "(path (step child (kind document-node (kind element (name a)))))" },
    { "document-node(schema-element(a))",
      "(path (step child (kind document-node (kind schema-element "
      "(name a)))))" },
    { "element()", "(path (step child (kind element)))" },
    { "element(*)", "(path (step child (kind element (name *))))" },
    { "element(a, xs:string?)",
      "(path (step child (kind element (name a) (type ? (atomic "
      "xs:string)))))" },
    { "attribute()", "(path (step attribute (kind attribute)))" },
    { "attribute(*, xs:int)",
      "(path (step attribute (kind attribute (name *) (type (atomic "
      "xs:int)))))" },
    { "child::attribute()", "(path (step child (kind attribute)))" },
    { "@node()", "(path (step attribute (kind node)))" },
    { "schema-attribute(a)",
      "(path (step attribute (kind schema-attribute (name a))))" },
    { "schema-element(p:a)",
      "(path (step child (kind schema-element (name p:a))))" },
    { "namespace-node()", "(path (step namespace (kind namespace-node)))" },
    { "self::node()", "(path (step self (kind node)))" },

    /* Predicates and filters */
    { "a[1]", "(path (step child (name a) (predicate (integer 1))))" },
    { "a[1][b]", "(path (step child (name a) (predicate (integer 1)) "
                 "(predicate " P("b") ")))" },
    { "a[b[c]]", "(path (step child (name a) (predicate (path (step child "
                 "(name b) (predicate " P("c") "))))))" },
    { "$x[1]", "(filter (var x) (predicate (integer 1)))" },
    { "(1, 2)[. = 1]", "(filter (sequence (integer 1) (integer 2)) "
                       "(predicate (binary = (context) (integer 1))))" },

    /* Function calls */
    { "f()", "(call f)" },
    { "f (1)", "(call f (integer 1))" },
    { "fn:concat('a', 'b')", "(call fn:concat (string \"a\") "
                             "(string \"b\"))" },
    { "Q{urn:x}f(1)", "(call Q{urn:x}f (integer 1))" },
    { "f(1, (2, 3))",
      "(call f (integer 1) (sequence (integer 2) (integer 3)))" },
    { "fn:if(1)", "(call fn:if (integer 1))" },

    /* Types */
    { "$x instance of xs:integer",
      "(instance-of (var x) (type (atomic xs:integer)))" },
    { "$x instance of item()*", "(instance-of (var x) (type * (item)))" },
    { "$x instance of element(a)+",
      "(instance-of (var x) (type + (kind element (name a))))" },
    { "$x instance of empty-sequence()",
      "(instance-of (var x) (empty-sequence))" },
    { "$x treat as node()?", "(treat-as (var x) (type ? (kind node)))" },
    { "$x castable as xs:integer?",
      "(castable-as (var x) (type ? (atomic xs:integer)))" },
    { "$x cast as xs:integer", "(cast-as (var x) (type (atomic "
                               "xs:integer)))" },
    { "-$x cast as xs:integer",
      "(cast-as (unary - (var x)) (type (atomic xs:integer)))" },
    { "$x cast as xs:integer castable as xs:string",
      "(castable-as (cast-as (var x) (type (atomic xs:integer))) "
      "(type (atomic xs:string)))" },
    { "$x instance of xs:integer and $y",
      "(binary and (instance-of (var x) (type (atomic xs:integer))) "
      "(var y))" },
    /* An occurrence indicator binds to the type (A.1.2) */
    { "4 treat as item() + - 5",
      "(binary - (treat-as (integer 4) (type + (item))) (integer 5))" },

    /* Keywords are names where an operand is expected */
    { "div div div", "(binary div " P("div") " " P("div") ")" },
    { "or or or", "(binary or " P("or") " " P("or") ")" },
    { "* * *", "(binary * " P("*") " " P("*") ")" },
    { "if/for", "(path (step child (name if)) (step child (name for)))" },
    { "child::child", P("child") },
    { "text", P("text") },
    { "instance instance of instance",
      "(instance-of " P("instance") " (type (atomic instance)))" },
    { "a-b", P("a-b") },
    { "a - b", "(binary - " P("a") " " P("b") ")" },

    /* for, let, some, every, if */
    { "for $x in 1 to 3 return $x * 2",
      "(for (bind x (binary to (integer 1) (integer 3))) "
      "(binary * (var x) (integer 2)))" },
    { "for $x in $a, $y in $b return ($x, $y)",
      "(for (bind x (var a)) (bind y (var b)) "
      "(sequence (var x) (var y)))" },
    { "for $x in 1 return for $y in 2 return $x",
      "(for (bind x (integer 1)) (for (bind y (integer 2)) (var x)))" },
    { "let $x := 1, $y := $x + 1 return $y",
      "(let (bind x (integer 1)) (bind y (binary + (var x) (integer 1))) "
      "(var y))" },
    { "some $x in $s satisfies $x > 1",
      "(some (bind x (var s)) (binary > (var x) (integer 1)))" },
    { "every $x in $s, $y in $t satisfies $x = $y",
      "(every (bind x (var s)) (bind y (var t)) "
      "(binary = (var x) (var y)))" },
    { "if ($a) then 1 else 2", "(if (var a) (integer 1) (integer 2))" },
    { "if (1, 2) then () else 3",
      "(if (sequence (integer 1) (integer 2)) (empty) (integer 3))" },
    { "if ($a) then 1 else if ($b) then 2 else 3",
      "(if (var a) (integer 1) (if (var b) (integer 2) (integer 3)))" },
    { "1 + (if ($a) then 1 else 2)",
      "(binary + (integer 1) (if (var a) (integer 1) (integer 2)))" },
    { "for $x in in return return",
      "(for (bind x " P("in") ") " P("return") ")" },
    { "some + every", "(binary + " P("some") " " P("every") ")" },
    { "if/then", "(path (step child (name if)) (step child (name then)))" },
    { "let", P("let") },

    /* ||, ! and => */
    { "'a' || 'b' || 'c'",
      "(binary || (binary || (string \"a\") (string \"b\")) "
      "(string \"c\"))" },
    { "1 to 2 || 3",
      "(binary || (binary to (integer 1) (integer 2)) (integer 3))" },
    { "$a || $b = 'ab'",
      "(binary = (binary || (var a) (var b)) (string \"ab\"))" },
    { "a ! b ! c",
      "(binary ! (binary ! " P("a") " " P("b") ") " P("c") ")" },
    { "a/b ! c/d",
      "(binary ! (path (step child (name a)) (step child (name b))) "
      "(path (step child (name c)) (step child (name d))))" },
    { "-a!b", "(unary - (binary ! " P("a") " " P("b") "))" },
    { "$x => f()", "(arrow f (var x))" },
    { "$x => f(1) => g()", "(arrow g (arrow f (var x) (integer 1)))" },
    { "$x => $f(1)", "(arrow (var x) (var f) (integer 1))" },
    { "$x => (f#1)()", "(arrow (var x) (function-ref f#1))" },
    { "$x => fn:f(?)", "(arrow fn:f (var x) (placeholder))" },
    { "-$x => abs()", "(arrow abs (unary - (var x)))" },
    { "$x => f() cast as xs:int",
      "(cast-as (arrow f (var x)) (type (atomic xs:int)))" },

    /* Function items and dynamic calls */
    { "f#2", "(function-ref f#2)" },
    { "fn:concat#3", "(function-ref fn:concat#3)" },
    { "Q{urn:x}f#0", "(function-ref Q{urn:x}f#0)" },
    { "f(?, 1)", "(call f (placeholder) (integer 1))" },
    { "f(?a)", "(call f (unary-lookup a))" },
    { "function($x as xs:integer) as xs:integer { $x + 1 }",
      "(function (param x (type (atomic xs:integer))) "
      "(type (atomic xs:integer)) (binary + (var x) (integer 1)))" },
    { "function($a, $b) { $a }", "(function (param a) (param b) (var a))" },
    { "function() {}", "(function (empty))" },
    { "$f(1)", "(filter (var f) (dynamic-call (integer 1)))" },
    { "(1)(2)", "(filter (integer 1) (dynamic-call (integer 2)))" },
    { "f(1)(2)[3]", "(filter (call f (integer 1)) (dynamic-call "
                    "(integer 2)) (predicate (integer 3)))" },

    /* Maps, arrays and lookups */
    { "map {}", "(map)" },
    { "map{'a': 1, 'b': 2}", "(map (entry (string \"a\") (integer 1)) "
                             "(entry (string \"b\") (integer 2)))" },
    { "map{a : b}", "(map (entry " P("a") " " P("b") "))" },
    { "map{1: map{2: 3}}",
      "(map (entry (integer 1) (map (entry (integer 2) (integer 3)))))" },
    { "[]", "(array-square)" },
    { "[1, (2, 3)]",
      "(array-square (integer 1) (sequence (integer 2) (integer 3)))" },
    { "array{}", "(array-curly)" },
    { "array{1, 2}", "(array-curly (sequence (integer 1) (integer 2)))" },
    { "$m?a", "(filter (var m) (lookup a))" },
    { "$m?1?*?($k)",
      "(filter (var m) (lookup 1) (lookup *) (lookup (var k)))" },
    { "[1, 2]?2",
      "(filter (array-square (integer 1) (integer 2)) (lookup 2))" },
    { "$a[?x = 1]", "(filter (var a) (predicate (binary = (unary-lookup x) "
                    "(integer 1))))" },
    { "?*", "(unary-lookup *)" },
    { "map/array", "(path (step child (name map)) (step child (name array)))" },

    /* Function, map and array types */
    { "$f instance of function(*)",
      "(instance-of (var f) (type (function-test *)))" },
    { "$f instance of function(xs:int, item()*) as xs:string?",
      "(instance-of (var f) (type (function-test (type (atomic xs:int)) "
      "(type * (item)) (type ? (atomic xs:string)))))" },
    { "$f instance of function() as item()",
      "(instance-of (var f) (type (function-test (type (item)))))" },
    { "$m instance of map(*)", "(instance-of (var m) (type (map-test *)))" },
    { "$m instance of map(xs:string, item()*)+",
      "(instance-of (var m) (type + (map-test (atomic xs:string) "
      "(type * (item)))))" },
    { "$a instance of array(*)",
      "(instance-of (var a) (type (array-test *)))" },
    { "$a instance of array(xs:int)",
      "(instance-of (var a) (type (array-test (type (atomic xs:int)))))" },
    { "$x instance of (xs:int)?",
      "(instance-of (var x) (type ? (atomic xs:int)))" },
    { "$x treat as (function(*))*",
      "(treat-as (var x) (type * (function-test *)))" },

    /* Corner cases: string and number literals */
    { "''''", "(string \"'\")" },
    { "\"\"\"\"", "(string \"\\\"\")" },
    { "'a\"\"b'", "(string \"a\\\"\\\"b\")" },
    { "'(: x :)'", "(string \"(: x :)\")" },
    { "99999999999999999999999", "(integer 99999999999999999999999)" },

    /* Corner cases: names next to operators */
    { "$x-1", "(var x-1)" },
    { "$x - 1", "(binary - (var x) (integer 1))" },
    { "--1", "(unary - (unary - (integer 1)))" },
    { "1!2", "(binary ! (integer 1) (integer 2))" },
    { "for(1)", "(call for (integer 1))" },
    { "let(1), some(2)", "(sequence (call let (integer 1)) "
                         "(call some (integer 2)))" },
    { "function", P("function") },
    { "fn:item(), fn:map(1)",
      "(sequence (call fn:item) (call fn:map (integer 1)))" },
    { "xs:integer('1')", "(call xs:integer (string \"1\"))" },

    /* Corner cases: comments inside constructs */
    { "for (: c :) $i in 1 return $i", "(for (bind i (integer 1)) (var i))" },
    { "if (: c :) (1) then 2 else 3", "(if (integer 1) (integer 2) "
                                      "(integer 3))" },
    { "$x instance (: c :) of xs:int",
      "(instance-of (var x) (type (atomic xs:int)))" },
    { "child (: c :) :: a", P("a") },

    /* Corner cases: "*", "+" and "?" after types */
    { "$x cast as xs:int * 2",
      "(binary * (cast-as (var x) (type (atomic xs:int))) (integer 2))" },
    { "$x instance of xs:int? and $y",
      "(binary and (instance-of (var x) (type ? (atomic xs:int))) "
      "(var y))" },

    /* Corner cases: slashes */
    { "/ | /", "(binary union (path root) (path root))" },
    { "/*", "(path root (step child (name *)))" },
    { "/@a", "(path root (step attribute (name a)))" },
    { "//*", "(path root (step descendant-or-self (kind node)) "
             "(step child (name *)))" },
    { "a//@b", "(path (step child (name a)) (step descendant-or-self "
               "(kind node)) (step attribute (name b)))" },
    { "a/..", "(path (step child (name a)) (step parent (kind node)))" },
    { "/ ?a", "(path root (unary-lookup a))" },
    { "attribute::attribute()", "(path (step attribute (kind attribute)))" },
    { "namespace::*", "(path (step namespace (name *)))" },

    /* Corner cases: postfixes, placeholders and lookups */
    { "$x[1](2)?a[3]", "(filter (var x) (predicate (integer 1)) "
                       "(dynamic-call (integer 2)) (lookup a) "
                       "(predicate (integer 3)))" },
    { "f(?,?)", "(call f (placeholder) (placeholder))" },
    { "f( ? )", "(call f (placeholder))" },
    { "f(?(1), ?*)", "(call f (unary-lookup (integer 1)) "
                     "(unary-lookup *))" },
    { "$m ? a", "(filter (var m) (lookup a))" },
    { "$m?()", "(filter (var m) (lookup (empty)))" },
    { "function($x){$x}(1)", "(filter (function (param x) (var x)) "
                             "(dynamic-call (integer 1)))" },
    { "f#0, f#00001, f#2147483647",
      "(sequence (function-ref f#0) (function-ref f#1) "
      "(function-ref f#2147483647))" },
    /* Braced URIs are whitespace normalized (section 2 and 2.4.5) */
    { "Q{ urn:x }a", P("Q{urn:x}a") },
    { "Q{urn:\n  a\t\r\nb}x", P("Q{urn: a b}x") },
    { "Q{   }a", P("Q{}a") },
    { "Q{ urn:x }*", P("Q{urn:x}*") },
    { "$Q{ urn:x }v", "(var Q{urn:x}v)" },
    { "Q{ urn:x }f(), Q{ urn:x }f#1",
      "(sequence (call Q{urn:x}f) (function-ref Q{urn:x}f#1))" },
    { "$x instance of Q{ urn:x }t",
      "(instance-of (var x) (type (atomic Q{urn:x}t)))" },
    { "a[for $x in 1 return $x]",
      "(path (step child (name a) (predicate (for (bind x (integer 1)) "
      "(var x)))))" }
};

static void
testTrees(const xp3StaticContext *ctx) {
    size_t i;

    for (i = 0; i < sizeof(treeCases) / sizeof(treeCases[0]); i++) {
        xp3Error *err = NULL;
        xmlChar *actual;
        char what[1024];

        actual = parseToString(ctx, treeCases[i].expr, &err);
        if (actual == NULL) {
            snprintf(what, sizeof(what), "'%s': unexpected error %s: %s",
                     treeCases[i].expr, (const char *) xp3ErrorCode(err),
                     (const char *) xp3ErrorMessage(err));
            check(0, what);
            xp3ErrorFree(err);
            continue;
        }
        snprintf(what, sizeof(what), "'%s':\n   got      %s\n   expected %s",
                 treeCases[i].expr, (const char *) actual,
                 treeCases[i].tree);
        check(strcmp((const char *) actual, treeCases[i].tree) == 0, what);
        xmlFree(actual);
    }
}

static const struct {
    const char *expr;
    const char *code;
    int line;
    int column;
} errorCases[] = {
    /* Missing or unexpected tokens */
    { "",                     "XPST0003", 1, 1 },
    { "1 +",                  "XPST0003", 1, 4 },
    { "(1",                   "XPST0003", 1, 3 },
    { "1 2",                  "XPST0003", 1, 3 },
    { "a[1",                  "XPST0003", 1, 4 },
    { "f(1,)",                "XPST0003", 1, 5 },
    { "$",                    "XPST0003", 1, 2 },
    { "$1",                   "XPST0003", 1, 2 },
    { "@",                    "XPST0003", 1, 2 },
    { "1 +\n  )",             "XPST0003", 2, 3 },
    { "'größe' +",            "XPST0003", 1, 10 },

    /* Comparisons and ranges do not chain */
    { "1 = 2 = 3",            "XPST0003", 1, 7 },
    { "1 to 2 to 3",          "XPST0003", 1, 8 },

    /* Paths */
    { "/ * 5",                "XPST0003", 1, 5 },
    { "//",                   "XPST0003", 1, 3 },
    { "a/",                   "XPST0003", 1, 3 },
    { "foo::a",               "XPST0003", 1, 1 },
    { "child::",              "XPST0003", 1, 8 },
    { "element(p:*)",         "XPST0003", 1, 9 },
    { "schema-element()",     "XPST0003", 1, 16 },
    { "processing-instruction(1)", "XPST0003", 1, 24 },
    { "element(a, xs:string", "XPST0003", 1, 21 },
    { "attribute(a, xs:int?)", "XPST0003", 1, 20 },

    /* Reserved function names */
    { "item()",               "XPST0003", 1, 1 },
    { "item()",               "XPST0003", 1, 1 },
    { "a/empty-sequence()",   "XPST0003", 1, 3 },

    /* Types */
    { "$x instance of xs:integer+ 1", "XPST0003", 1, 28 },
    { "$x cast as item()",    "XPST0003", 1, 16 },
    { "$x instance xs:integer", "XPST0003", 1, 13 },
    { "1 instance of",        "XPST0003", 1, 14 },

    /* for, let, some, every, if */
    { "if(1)",                "XPST0003", 1, 6 },
    { "if (1) then 2",        "XPST0003", 1, 14 },
    { "for $x return 1",      "XPST0003", 1, 8 },
    { "for $x in 1, 2 return $x", "XPST0003", 1, 14 },
    { "let $x = 1 return $x", "XPST0003", 1, 8 },
    { "some $x in 1 return 2", "XPST0003", 1, 14 },

    /* Function items, maps, arrays and lookups */
    { "f#",                   "XPST0003", 1, 3 },
    { "f#99999999999",        "XPST0017", 1, 3 },
    { "if#1",                 "XPST0003", 1, 1 },
    { "$x => 1",              "XPST0003", 1, 7 },
    { "$x => f",              "XPST0003", 1, 8 },
    { "function($x) $x",      "XPST0003", 1, 14 },
    { "function(1) {1}",      "XPST0003", 1, 10 },
    { "map{a:b}",             "XPST0003", 1, 8 },
    { "map{1: 2",             "XPST0003", 1, 9 },
    { "[1, 2",                "XPST0003", 1, 6 },
    { "$m?",                  "XPST0003", 1, 4 },
    { "$m?1.5",               "XPST0003", 1, 4 },
    { "a?b",                  "XPST0003", 1, 2 },
    { "$f instance of function(xs:int)", "XPST0003", 1, 32 },
    { "$m instance of map(xs:int)", "XPST0003", 1, 26 },

    /* Corner cases: punctuation */
    { ",",                    "XPST0003", 1, 1 },
    { ")",                    "XPST0003", 1, 1 },
    { "]",                    "XPST0003", 1, 1 },
    { "}",                    "XPST0003", 1, 1 },
    { "-",                    "XPST0003", 1, 2 },
    { "(1,)",                 "XPST0003", 1, 4 },
    { "((1)",                 "XPST0003", 1, 5 },
    { "f(,)",                 "XPST0003", 1, 3 },
    { "f(1,,2)",              "XPST0003", 1, 5 },
    { "a[]",                  "XPST0003", 1, 3 },
    { "[1,]",                 "XPST0003", 1, 4 },
    { "map{1:2,}",            "XPST0003", 1, 9 },
    { "map{'a':1 'b':2}",     "XPST0003", 1, 11 },
    { "function($a,){}",      "XPST0003", 1, 13 },
    { "(: only a comment :)", "XPST0003", 1, 21 },
    { "  \n  ",               "XPST0003", 2, 3 },

    /* Corner cases: operators that do not repeat */
    { "1 eq 2 eq 3",          "XPST0003", 1, 8 },
    { "a is b is c",          "XPST0003", 1, 8 },
    { "a instance of b instance of c", "XPST0003", 1, 17 },
    { "a treat as b treat as c", "XPST0003", 1, 14 },
    { "a cast as b cast as c", "XPST0003", 1, 13 },
    { "$x cast as xs:int+",   "XPST0003", 1, 19 },

    /* Corner cases: steps and names */
    { "a/-1",                 "XPST0003", 1, 3 },
    { "@@a",                  "XPST0003", 1, 2 },
    { "@..",                  "XPST0003", 1, 2 },
    { "..a",                  "XPST0003", 1, 3 },
    { "p:child::a",           "XPST0003", 1, 8 },
    { "document-node(text())", "XPST0003", 1, 15 },
    { "! a",                  "XPST0003", 1, 1 },
    { "a !",                  "XPST0003", 1, 4 },
    { "if $x then 1 else 2",  "XPST0003", 1, 4 },
    { "let $x : = 1 return $x", "XPST0003", 1, 8 },
    { "f#2147483648",         "XPST0017", 1, 3 },
    { "a\r\n+ )",             "XPST0003", 2, 3 },

    /* Lexical errors, also when found while peeking */
    { "1 + 'abc",             "XPST0003", 1, 5 },
    { "a 10div 3",            "XPST0003", 1, 3 }
};

static void
testErrors(const xp3StaticContext *ctx) {
    size_t i;

    for (i = 0; i < sizeof(errorCases) / sizeof(errorCases[0]); i++) {
        xp3Error *err = NULL;
        xmlChar *actual;
        char what[1024];

        actual = parseToString(ctx, errorCases[i].expr, &err);
        snprintf(what, sizeof(what),
                 "'%s': expected %s at %d:%d, got %s at %d:%d (%s)",
                 errorCases[i].expr, errorCases[i].code, errorCases[i].line,
                 errorCases[i].column,
                 err ? (const char *) xp3ErrorCode(err) : "no error",
                 xp3ErrorLine(err), xp3ErrorColumn(err),
                 actual ? (const char *) actual :
                 err ? (const char *) xp3ErrorMessage(err) : "");
        check((actual == NULL) && (err != NULL) &&
              (strcmp((const char *) xp3ErrorCode(err),
                      errorCases[i].code) == 0) &&
              (xp3ErrorLine(err) == errorCases[i].line) &&
              (xp3ErrorColumn(err) == errorCases[i].column),
              what);
        xmlFree(actual);
        xp3ErrorFree(err);
    }
}

/* Check the message of the error for expr. */
static void
expectMessage(const xp3StaticContext *ctx, const char *expr,
              const char *message) {
    xp3Error *err = NULL;
    xmlChar *actual;
    char what[512];

    actual = parseToString(ctx, expr, &err);
    snprintf(what, sizeof(what), "'%s': message \"%s\", expected \"%s\"",
             expr, err ? (const char *) xp3ErrorMessage(err) : "(none)",
             message);
    check((err != NULL) &&
          (strcmp((const char *) xp3ErrorMessage(err), message) == 0),
          what);
    xmlFree(actual);
    xp3ErrorFree(err);
}

static void
testMessages(const xp3StaticContext *ctx) {
    expectMessage(ctx, "1 2", "unexpected '2'");
    expectMessage(ctx, "(1", "expected ')', found end of expression");
    expectMessage(ctx, "foo::a", "unknown axis 'foo'");
    expectMessage(ctx, "item()",
                  "'item' is reserved and cannot be a function name");
    expectMessage(ctx, "if (1) then 2",
                  "expected 'else', found end of expression");
    expectMessage(ctx, "1 abcdefghijklmnopqrstuvwxyzabcdefghij",
                  "unexpected 'abcdefghijklmnopqrstuvwxyzabcdef...'");
    /* Shortened at a character boundary: "ä" is two bytes */
    expectMessage(ctx, "1 abcdefghijklmnopqrstuvwxyzabcdeä",
                  "unexpected 'abcdefghijklmnopqrstuvwxyzabcde...'");
}

static const struct {
    xp3LanguageLevel level;
    const char *expr;
    const char *tree;       /* NULL: XPST0003 expected */
} levelCases[] = {
    /* Names reserved in later versions are function names before */
    { XP3_XPATH_20, "namespace-node()", "(call namespace-node)" },
    { XP3_XPATH_30, "namespace-node()",
      "(path (step namespace (kind namespace-node)))" },
    { XP3_XPATH_20, "function(1)", "(call function (integer 1))" },
    { XP3_XPATH_30, "function(1)", NULL },
    { XP3_XPATH_20, "switch(1)", "(call switch (integer 1))" },
    { XP3_XPATH_30, "switch(1)", NULL },
    { XP3_XPATH_30, "map(1)", "(call map (integer 1))" },
    { XP3_XPATH_31, "map(1)", NULL },
    { XP3_XPATH_30, "array(1)", "(call array (integer 1))" },
    { XP3_XPATH_31, "array(1)", NULL },

    /* XPath 3.0 syntax */
    { XP3_XPATH_20, "for $x in 1 return $x", "(for (bind x (integer 1)) (var x))" },
    { XP3_XPATH_20, "let $x := 1 return $x", NULL },
    { XP3_XPATH_30, "let $x := 1 return $x", "(let (bind x (integer 1)) (var x))" },
    { XP3_XPATH_20, "'a' || 'b'", NULL },
    { XP3_XPATH_30, "'a' || 'b'", "(binary || (string \"a\") (string \"b\"))" },
    { XP3_XPATH_20, "a ! b", NULL },
    { XP3_XPATH_20, "f#1", NULL },
    { XP3_XPATH_30, "f#1", "(function-ref f#1)" },
    { XP3_XPATH_20, "function($x) {$x}", NULL },
    { XP3_XPATH_30, "function($x) {$x}", "(function (param x) (var x))" },
    { XP3_XPATH_20, "f(?)", NULL },
    { XP3_XPATH_30, "f(?)", "(call f (placeholder))" },
    { XP3_XPATH_20, "$f(1)", NULL },
    { XP3_XPATH_30, "$f(1)", "(filter (var f) (dynamic-call (integer 1)))" },
    { XP3_XPATH_20, "$x instance of function(*)", NULL },
    { XP3_XPATH_30, "$x instance of function(*)",
      "(instance-of (var x) (type (function-test *)))" },
    { XP3_XPATH_20, "$x instance of (xs:int)", NULL },

    /* XPath 3.1 syntax */
    { XP3_XPATH_30, "$x => f()", NULL },
    { XP3_XPATH_30, "function() {}", NULL },
    { XP3_XPATH_30, "map{}", NULL },
    { XP3_XPATH_30, "[1]", NULL },
    { XP3_XPATH_30, "array{1}", NULL },
    { XP3_XPATH_30, "$m?a", NULL },
    { XP3_XPATH_30, "?a", NULL },
    { XP3_XPATH_30, "$x instance of map(*)", NULL },
    { XP3_XPATH_30, "$x instance of array(*)", NULL },

    /* Q{} names are XPath 3.0 */
    { XP3_XPATH_20, "Q{urn:x}a", NULL },
    { XP3_XPATH_20, "Q{urn:x}*", NULL },
    { XP3_XPATH_20, "$Q{urn:x}v", NULL },
    { XP3_XPATH_30, "Q{urn:x}a", P("Q{urn:x}a") }
};

static void
testLevels(void) {
    xp3StaticContext *ctx = xp3NewStaticContext();
    size_t i;

    for (i = 0; i < sizeof(levelCases) / sizeof(levelCases[0]); i++) {
        xp3Error *err = NULL;
        xmlChar *actual;
        char what[512];

        xp3SetStaticContextLanguageLevel(ctx, levelCases[i].level);
        actual = parseToString(ctx, levelCases[i].expr, &err);
        snprintf(what, sizeof(what), "level %d '%s': got %s, expected %s",
                 (int) levelCases[i].level, levelCases[i].expr,
                 actual ? (const char *) actual :
                 err ? (const char *) xp3ErrorCode(err) : "(null)",
                 levelCases[i].tree ? levelCases[i].tree : "XPST0003");
        if (levelCases[i].tree != NULL)
            check((actual != NULL) &&
                  (strcmp((const char *) actual, levelCases[i].tree) == 0),
                  what);
        else
            check((actual == NULL) && (err != NULL) &&
                  (strcmp((const char *) xp3ErrorCode(err), "XPST0003")
                   == 0), what);
        xmlFree(actual);
        xp3ErrorFree(err);
    }

    xp3SetStaticContextLanguageLevel(ctx, XP3_XPATH_20);
    expectMessage(ctx, "let $x := 1 return $x",
                  "XPath 3.0 is required for let expressions");
    xp3SetStaticContextLanguageLevel(ctx, XP3_XPATH_30);
    expectMessage(ctx, "$x => f()", "XPath 3.1 is required for '=>'");

    xp3FreeStaticContext(ctx);
}

static void
testPositions(const xp3StaticContext *ctx) {
    static const char expr[] = "1 +\n  foo[2]";
    xp3Ast *ast;
    xp3AstNode *path;

    ast = xp3Parse(ctx, BAD_CAST expr, strlen(expr), NULL);
    check(ast != NULL, "positions: parsed");
    if (ast == NULL)
        return;
    path = ast->root->last;
    check(ast->root->line == 1 && ast->root->column == 1,
          "binary at its left operand");
    check(path->line == 2 && path->column == 3, "path at its first step");
    check(path->first->first->line == 2 && path->first->first->column == 3,
          "name test at its token");
    check(path->first->last->line == 2 && path->first->last->column == 6,
          "predicate at '['");
    xp3AstFree(ast);
}

static void
testArguments(const xp3StaticContext *ctx) {
    xp3Error *err = NULL;
    xp3Ast *ast;
    xmlChar *tree;

    /* The length can end inside a token */
    ast = xp3Parse(ctx, BAD_CAST "'abc'", 3, &err);
    check(ast == NULL && err != NULL &&
          strcmp((const char *) xp3ErrorCode(err), "XPST0003") == 0,
          "length ends inside a string literal");
    xp3ErrorFree(err);

    /* Only length bytes are parsed */
    ast = xp3Parse(ctx, BAD_CAST "1 + 2junk", 5, &err);
    tree = ast ? xp3AstToString(ast->root) : NULL;
    check(tree != NULL &&
          strcmp((const char *) tree, "(binary + (integer 1) (integer 2))")
          == 0, "expression length respected");
    xmlFree(tree);
    xp3AstFree(ast);

    ast = xp3Parse(NULL, BAD_CAST "Q{urn:x}a", 9, &err);
    check(ast != NULL && err == NULL, "NULL context uses XPath 3.1");
    xp3AstFree(ast);

    ast = xp3Parse(ctx, NULL, 0, &err);
    check(ast == NULL && err != NULL &&
          strcmp((const char *) xp3ErrorCode(err), "XPST0003") == 0,
          "NULL expression is empty");
    xp3ErrorFree(err);

    check(xp3Parse(ctx, BAD_CAST "1 +", 3, NULL) == NULL,
          "error without an error pointer");
}

/* Repeat str n times followed by tail. Returns a malloc'ed string. */
static char *
repeat(const char *str, int n, const char *tail) {
    size_t len = strlen(str);
    size_t tailLen = strlen(tail);
    char *ret = malloc(len * (size_t) n + tailLen + 1);
    int i;

    if (ret == NULL)
        return(NULL);
    for (i = 0; i < n; i++)
        memcpy(ret + len * (size_t) i, str, len);
    memcpy(ret + len * (size_t) n, tail, tailLen + 1);
    return(ret);
}

/* Put pre before and post after a malloc'ed string. Frees str. */
static char *
wrap(const char *pre, char *str, const char *post) {
    char *ret;

    if (str == NULL)
        return(NULL);
    ret = malloc(strlen(pre) + strlen(str) + strlen(post) + 1);
    if (ret != NULL) {
        strcpy(ret, pre);
        strcat(ret, str);
        strcat(ret, post);
    }
    free(str);
    return(ret);
}

/* Parse expr and check for success, or for a LIMIT error at column. */
static void
expectLimit(const xp3StaticContext *ctx, char *expr, int column,
            const char *what) {
    xp3Error *err = NULL;
    xp3Ast *ast;
    char msg[256];

    check(expr != NULL, "test expression allocated");
    if (expr == NULL)
        return;
    ast = xp3Parse(ctx, BAD_CAST expr, strlen(expr), &err);
    snprintf(msg, sizeof(msg), "%s: got %s at %d", what,
             err ? (const char *) xp3ErrorCode(err) : "success",
             xp3ErrorColumn(err));
    if (column == 0)
        check(ast != NULL, msg);
    else
        check(ast == NULL && err != NULL &&
              strcmp((const char *) xp3ErrorCode(err), "LIMIT") == 0 &&
              xp3ErrorColumn(err) == column, msg);
    xp3AstFree(ast);
    xp3ErrorFree(err);
    free(expr);
}

static void
testLimits(const xp3StaticContext *defaultCtx) {
    xp3StaticContext *ctx = xp3NewStaticContext();

    /* With a limit of 30, three expressions can nest (cost 10 each),
     * and 20 signs or chained operators fit inside one. */
    ctx->maxParseDepth = 30;
    expectLimit(ctx, repeat("(", 2, "1))"), 0, "two nested parentheses");
    expectLimit(ctx, repeat("(", 3, "1)))"), 4, "three nested parentheses");
    expectLimit(ctx, repeat("-", 20, "1"), 0, "20 signs");
    expectLimit(ctx, repeat("-", 21, "1"), 21, "21 signs");
    expectLimit(ctx, repeat("1+", 20, "1"), 0, "20 additions");
    expectLimit(ctx, repeat("1+", 21, "1"), 42, "21 additions");
    expectLimit(ctx, repeat("a!", 20, "a"), 0, "20 simple maps");
    expectLimit(ctx, repeat("a!", 21, "a"), 42, "21 simple maps");
    expectLimit(ctx, wrap("$x", repeat("=>f()", 20, ""), ""), 0,
                "20 arrows");
    expectLimit(ctx, wrap("$x", repeat("=>f()", 21, ""), ""), 103,
                "21 arrows");
    /* Each item type counts 1: 19 parentheses and the name make 20 */
    expectLimit(ctx, wrap("$x instance of ", repeat("(", 19, "xs:int"),
                          ")))))))))))))))))))"),
                0, "19 parenthesized item types");
    expectLimit(ctx, wrap("$x instance of ", repeat("(", 20, "xs:int"), ""),
                36, "20 parenthesized item types");
    expectLimit(ctx, wrap("", repeat("[", 3, "1"), "]]]"), 4,
                "nested arrays");
    expectLimit(ctx, wrap("", repeat("map{1:", 3, "1"), "}}}"), 17,
                "nested maps");
    expectLimit(ctx, wrap("", repeat("function(){", 3, "1"), "}}}"), 34,
                "nested inline functions");
    /* 19 function tests and item() make 20 item types */
    expectLimit(ctx, wrap("$x instance of ",
                          repeat("function() as ", 19, "item()"), ""),
                0, "function types nested 19 deep");
    expectLimit(ctx, wrap("$x instance of ",
                          repeat("function() as ", 20, "item()"), ""),
                296, "function types nested 20 deep");
    expectLimit(ctx, repeat("1,", 1000, "1"), 0, "sequences are flat");
    expectLimit(ctx, repeat("a/", 1000, "a"), 0, "paths are flat");
    xp3FreeStaticContext(ctx);

    /* The default limit stops deep nesting long before the stack runs
     * out. */
    expectLimit(defaultCtx, repeat("(", 100000, "1"), 501,
                "deeply nested parentheses");
    expectLimit(defaultCtx, repeat("a[", 100000, "1"), 1001,
                "deeply nested predicates");
    expectLimit(defaultCtx, repeat("-", 100000, "1"), 4991,
                "many unary signs");
}

/* Parse and print a larger expression. Returns NULL with *err set on
 * failure. */
static xmlChar *
parseLarge(const xp3StaticContext *ctx, xp3Error **err) {
    static const char expr[] =
        "fn:concat('it''s', $p:x)[1]/child::a[@b = 1] | "
        "//c instance of element(d, xs:string?)*, "
        "-Q{urn:x}e cast as xs:integer?, document-node(element(*)), "
        "let $m := map{'k': [1, ?a]} return $m?k => f(?), "
        "function($x as map(*)) as item()* { $x ! . || 'z' }, "
        "if (1) then some $v in () satisfies true() else 0";

    return(parseToString(ctx, expr, err));
}

static void
testAllocationFailures(const xp3StaticContext *ctx) {
    xmlChar *expected;
    xp3Error *err = NULL;
    int i;

    expected = parseLarge(ctx, &err);
    check(expected != NULL, "reference tree parsed");
    xp3ErrorFree(err);

    if (!xp3TestAllocInstall()) {
        printf("testparser: allocation-failure tests skipped: this "
               "libxml2 ignores xmlMemSetup()\n");
        xp3TestAllocRestore();
        xmlFree(expected);
        return;
    }

    /* Fail each allocation in turn. Every run must either fail with
     * NO_MEMORY or produce the full tree, without leaks. */
    for (i = 0; i < 1000; i++) {
        xmlChar *actual;

        err = NULL;
        xp3TestAllocFailAt(i);
        actual = parseLarge(ctx, &err);
        if (!xp3TestAllocFailed()) {
            check(actual != NULL && expected != NULL &&
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
        check(err == NULL ||
              strcmp((const char *) xp3ErrorCode(err), "NO_MEMORY") == 0,
              "failed run reports NO_MEMORY");
        xmlFree(actual);
        xp3ErrorFree(err);
    }
    check(i > 0 && i < 1000, "allocation failure points exercised");

    xp3TestAllocRestore();
    xmlFree(expected);
}

int
main(void) {
    xp3StaticContext *ctx;

    xmlInitParser();
    ctx = xp3NewStaticContext();

    testTrees(ctx);
    testErrors(ctx);
    testMessages(ctx);
    testLevels();
    testPositions(ctx);
    testArguments(ctx);
    testLimits(ctx);
    testAllocationFailures(ctx);

    xp3FreeStaticContext(ctx);

    if (failures == 0)
        printf("testparser: OK\n");
    return(failures == 0 ? 0 : 1);
}
