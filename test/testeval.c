/*
 * testeval.c: tests for the dynamic context and evaluation
 *
 * Each case evaluates an expression and compares a description of the
 * result: "e:name#id" for elements (with their id attribute), "a:name"
 * for attributes, "t:text", "c:text", "p:target" and "d" for the other
 * nodes, "type:value" for atomic values, and "error:CODE" for errors.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <locale.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

#include <libxpath3/xpath3.h>
#include "private/xp3context.h"
#include "private/xp3decimal.h"
#include "private/xp3double.h"
#include "private/xp3eval.h"
#include "private/xp3nodes.h"
#include "private/xp3ops.h"
#include "private/xp3static.h"
#include "private/xp3types.h"
#include "private/xp3value.h"
#include "testalloc.h"

static int failures = 0;

static void
check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static const char xp3TestDoc[] =
    "<r a='1'>"
    "<x id='x1'>one</x>"
    "<x id='x2'>two<y/></x>"
    "<!--c--><?pi d?>tail"
    "<z><x id='x3'/></z>"
    "</r>";

/*
 * Describing results
 */

static size_t
appendText(char *out, size_t size, size_t used, const char *text) {
    if (used < size)
        snprintf(out + used, size - used, "%s%s", (used > 0) ? " " : "",
                 text);
    return(used + strlen(text) + ((used > 0) ? 1 : 0));
}

static void
describeItem(const xp3Sequence *seq, size_t index, char *out, size_t size) {
    xmlNodePtr node = xp3SequenceNode(seq, index);
    xmlChar *value = xp3SequenceStringValue(seq, index);
    xmlChar *id;
    xp3Item item;

    out[0] = 0;
    if (node == NULL) {
        if ((xp3ValueItemAt(seq, index, &item) > 0) &&
            (item.kind == XP3_ITEM_ATOMIC)) {
            snprintf(out, size, "%s:%s",
                     (const char *) xp3TypeLocalName(item.u.atomic->type),
                     (value != NULL) ? (const char *) value : "?");
            xp3ItemUnref(item);
        } else {
            snprintf(out, size, "?");
        }
        xmlFree(value);
        return;
    }
    switch (xp3NodeGetKind(node)) {
        case XP3_NODE_DOCUMENT:
            snprintf(out, size, "d");
            break;
        case XP3_NODE_ELEMENT:
            id = xmlGetProp(node, BAD_CAST "id");
            snprintf(out, size, "e:%s%s%s", (const char *) node->name,
                     (id != NULL) ? "#" : "",
                     (id != NULL) ? (const char *) id : "");
            xmlFree(id);
            break;
        case XP3_NODE_ATTRIBUTE:
            snprintf(out, size, "a:%s", (const char *) node->name);
            break;
        case XP3_NODE_TEXT:
            snprintf(out, size, "t:%s", (const char *) value);
            break;
        case XP3_NODE_COMMENT:
            snprintf(out, size, "c:%s", (const char *) value);
            break;
        case XP3_NODE_PI:
            snprintf(out, size, "p:%s", (const char *) node->name);
            break;
        default:
            snprintf(out, size, "none");
            break;
    }
    xmlFree(value);
}

static void
describe(const xp3Sequence *seq, char *out, size_t size) {
    size_t used = 0;
    size_t i;

    out[0] = 0;
    for (i = 0; i < xp3SequenceLength(seq); i++) {
        char item[128];

        describeItem(seq, i, item, sizeof(item));
        used = appendText(out, size, used, item);
    }
}

/* Compile and evaluate, and describe the result or the error */
static void
run(const xp3StaticContext *sctx, xp3DynamicContext *ctx, const char *expr,
    char *out, size_t size) {
    xp3Error *err = NULL;
    xp3Expr *compiled = xp3Compile(sctx, BAD_CAST expr, &err);
    xp3Sequence *result = NULL;

    if (compiled != NULL)
        result = xp3Evaluate(compiled, ctx, &err);
    if (result != NULL)
        describe(result, out, size);
    else
        snprintf(out, size, "error:%s",
                 (const char *) xp3ErrorCode(err));
    xp3FreeSequence(result);
    xp3FreeExpr(compiled);
    xp3ErrorFree(err);
}

static void
checkEval(const xp3StaticContext *sctx, xp3DynamicContext *ctx,
          const char *expr, const char *expected) {
    char actual[1024];

    run(sctx, ctx, expr, actual, sizeof(actual));
    if (strcmp(actual, expected) != 0) {
        fprintf(stderr, "FAIL: %s\n  expected: %s\n  actual:   %s\n",
                expr, expected, actual);
        failures++;
    }
}

/*
 * Cases
 */

typedef struct {
    const char *expr;
    const char *expected;
} xp3EvalCase;

/* Evaluated with the element r as the context node */
static const xp3EvalCase xp3Cases[] = {
    /* Literals and sequences */
    { "1", "integer:1" },
    { "1.50", "decimal:1.5" },
    { "1e2", "double:100" },
    { "'a''b'", "string:a'b" },
    { "()", "" },
    { "(1, 'a', (), (2, 3))", "integer:1 string:a integer:2 integer:3" },
    { "123456789012345678901234567890",
      "integer:123456789012345678901234567890" },
    { ".", "e:r" },

    /* Steps and node tests */
    { "x", "e:x#x1 e:x#x2" },
    { "child::x", "e:x#x1 e:x#x2" },
    { "*", "e:x#x1 e:x#x2 e:z" },
    { "node()", "e:x#x1 e:x#x2 c:c p:pi t:tail e:z" },
    { "text()", "t:tail" },
    { "comment()", "c:c" },
    { "processing-instruction()", "p:pi" },
    { "processing-instruction(pi)", "p:pi" },
    { "processing-instruction('pi')", "p:pi" },
    { "processing-instruction(other)", "" },
    { "@*", "a:a" },
    { "@a", "a:a" },
    { "@b", "" },
    { "attribute::attribute(a)", "a:a" },
    { "@attribute(*, xs:untypedAtomic)", "a:a" },
    { "@attribute(*, xs:anyAtomicType)", "a:a" },
    { "@attribute(*, xs:integer)", "" },
    { "element()", "e:x#x1 e:x#x2 e:z" },
    { "element(x)", "e:x#x1 e:x#x2" },
    { "element(*, xs:untyped)", "e:x#x1 e:x#x2 e:z" },
    { "element(x, xs:anyType)", "e:x#x1 e:x#x2" },
    { "element(*, xs:string)", "" },
    { "*:x", "e:x#x1 e:x#x2" },
    { "Q{}*", "e:x#x1 e:x#x2 e:z" },
    { "Q{urn:other}*", "" },
    { "child::namespace-node()", "" },
    { "self::document-node()", "" },
    { "self::document-node(element(r))", "" },
    { "Q{}x", "e:x#x1 e:x#x2" },
    { "Q{urn:other}x", "" },
    { "self::r", "e:r" },
    { "self::x", "" },
    { "parent::node()", "d" },
    { "..", "d" },
    { "../..", "" },
    { "x/@id", "a:id a:id" },
    { "x/text()", "t:one t:two" },
    { "x[2]/y/ancestor::*", "e:r e:x#x2" },
    { "x[2]/y/ancestor::*[1]", "e:x#x2" },
    { "x[2]/y/ancestor-or-self::node()", "d e:r e:x#x2 e:y" },
    { "z/x/preceding::x", "e:x#x1 e:x#x2" },
    { "z/x/preceding::x[1]", "e:x#x2" },
    { "x[1]/following-sibling::*", "e:x#x2 e:z" },
    { "z/preceding-sibling::node()[2]", "p:pi" },
    { "x[1]/following::x", "e:x#x2 e:x#x3" },
    { "descendant::x", "e:x#x1 e:x#x2 e:x#x3" },
    { "descendant-or-self::*[@id = 'x3']", "e:x#x3" },

    /* Paths */
    { "/", "d" },
    { "/r", "e:r" },
    { "/self::document-node()", "d" },
    { "/self::document-node(element(r))", "d" },
    { "/self::document-node(element(q))", "" },
    { "//x", "e:x#x1 e:x#x2 e:x#x3" },
    { "//x/@id", "a:id a:id a:id" },
    { "//@id/..", "e:x#x1 e:x#x2 e:x#x3" },
    { "//x[1]", "e:x#x1 e:x#x3" },
    { "(//x)[1]", "e:x#x1" },
    { "(//x)[3]", "e:x#x3" },
    { "//y/../..", "e:r" },
    { "x/..", "e:r" },
    { "(x[2], x[1])/.", "e:x#x1 e:x#x2" },
    { "(x, x)", "e:x#x1 e:x#x2 e:x#x1 e:x#x2" },
    { "(x, x)/.", "e:x#x1 e:x#x2" },
    { "x/(*, @id)", "a:id a:id e:y" },
    { "x/1", "integer:1 integer:1" },
    { "x/'a'", "string:a string:a" },
    { "x/(., 1)", "error:XPTY0018" },
    { "(1, 2)/x", "error:XPTY0019" },
    { "1/x", "error:XPTY0019" },
    { "x/1/x", "error:XPTY0019" },
    { "(1, 2)[x]", "error:XPTY0020" },
    { "(1)[/]", "error:XPTY0020" },

    /* Predicates */
    { "x[2]", "e:x#x2" },
    { "x[3]", "" },
    { "x[0]", "" },
    { "x[1.0]", "e:x#x1" },
    { "x[1.5]", "" },
    { "x[2e0]", "e:x#x2" },
    { "x[y]", "e:x#x2" },
    { "x[@id][2]", "e:x#x2" },
    { "x[2][1]", "e:x#x2" },
    { "x[1][2]", "" },
    { "(1, 2, 3)[2]", "integer:2" },
    { "('a', 'b')['x']", "string:a string:b" },
    { "('a', 'b')['']", "" },
    { "(1, 2)[(1, 2)]", "error:FORG0006" },
    { "x[(1, 2)]", "error:FORG0006" },
    { "x[(y, 1)]", "e:x#x1 e:x#x2" },

    /* for, let, some, every, if, and, or */
    { "for $i in (1, 2) return ($i, 'x')",
      "integer:1 string:x integer:2 string:x" },
    { "for $a in x, $b in $a/@id return $b", "a:id a:id" },
    { "for $a in x return $a/y", "e:y" },
    { "for $a in () return 1", "" },
    { "let $a := x return $a[2]", "e:x#x2" },
    { "let $a := 1, $b := ($a, 2) return $b", "integer:1 integer:2" },
    { "let $a := 1 return let $a := 2 return $a", "integer:2" },
    { "for $a in 1 return ((for $a in 2 return $a), $a)",
      "integer:2 integer:1" },
    { "let $a := 1, $b := (let $a := 2 return $a) return ($a, $b)",
      "integer:1 integer:2" },
    { "for $a in 1, $a in ($a + 1) return $a", "integer:2" },
    { "some $a in 1 satisfies ((every $a in 0 satisfies $a), $a)[2]",
      "boolean:true" },
    { "some $i in (0, 1) satisfies $i", "boolean:true" },
    { "some $i in (0, '') satisfies $i", "boolean:false" },
    { "every $i in (1, 'a') satisfies $i", "boolean:true" },
    { "every $i in (1, 0) satisfies $i", "boolean:false" },
    { "every $i in () satisfies $i", "boolean:true" },
    { "some $i in () satisfies $i", "boolean:false" },
    { "some $a in x, $b in $a/* satisfies $b", "boolean:true" },
    { "some $i in (1, 2) satisfies ($i, $i)", "error:FORG0006" },
    { "if (x) then 1 else 2", "integer:1" },
    { "if (q) then 1 else 2", "integer:2" },
    { "if ('') then 1 else 2", "integer:2" },
    { "if (0.0) then 1 else 2", "integer:2" },
    { "if (1e0) then 1 else 2", "integer:1" },
    { "if (x/@id) then 1 else 2", "integer:1" },
    { "if ((1, 2)) then 1 else 2", "error:FORG0006" },
    { "if (some $i in 1 satisfies $i) then 1 else 2", "integer:1" },
    { "if (every $i in 1 satisfies 0) then 1 else 2", "integer:2" },
    { "(1, 2) and x", "error:FORG0006" },
    { "(x and q) or (x and x)", "boolean:true" },
    { "x and q", "boolean:false" },
    { "x or q", "boolean:true" },
    { "'' or 0", "boolean:false" },
    { "x and 'a'", "boolean:true" },
    { "q and (1, 2)", "boolean:false" },
    { "x or (1, 2)", "boolean:true" },
    { "q or (1, 2)", "error:FORG0006" },

    /* Arithmetic */
    { "1 + 2", "integer:3" },
    { "5 - 7", "integer:-2" },
    { "2 * 3.5", "decimal:7" },
    { "7 div 2", "decimal:3.5" },
    { "6 div 2", "decimal:3" },
    { "7 idiv 2", "integer:3" },
    { "-7 idiv 2", "integer:-3" },
    { "7 mod -2", "integer:1" },
    { "-7 mod 2", "integer:-1" },
    { "1.5 + 1", "decimal:2.5" },
    { "0.1 + 0.2", "decimal:0.3" },
    { "0.1e0 + 0.2e0", "double:0.30000000000000004" },
    { "1e0 + 1", "double:2" },
    { "1e0 - 2", "double:-1" },
    { "2e0 * 3", "double:6" },
    { "xs:float(1.5) + 1", "float:2.5" },
    { "xs:float(1) + 1e0", "double:2" },
    { "99999999999999999999 * 99999999999999999999",
      "integer:9999999999999999999800000000000000000001" },
    { "1 div 0", "error:FOAR0001" },
    { "1 idiv 0", "error:FOAR0001" },
    { "1.0 mod 0", "error:FOAR0001" },
    { "1e0 div 0", "double:INF" },
    { "-1e0 div 0", "double:-INF" },
    { "0e0 div 0", "double:NaN" },
    { "1e0 idiv 0", "error:FOAR0001" },
    { "xs:double('INF') idiv 1", "error:FOAR0002" },
    { "xs:double('NaN') idiv 1", "error:FOAR0002" },
    { "7.5e0 idiv 2", "integer:3" },
    { "-7.5e0 idiv 2", "integer:-3" },
    { "1e300 idiv 1e-10", "error:FOCA0002" },
    { "xs:float(3e38) idiv xs:float(0.1)", "error:FOCA0002" },
    { "5.5e0 mod 2", "double:1.5" },
    { "-5.5e0 mod 2", "double:-1.5" },
    { "xs:float(5.5) idiv 2", "integer:2" },
    { "() + 1", "" },
    { "1 + ()", "" },
    { "(1, 2) + 1", "error:XPTY0004" },
    { "1 + (1, 2)", "error:XPTY0004" },
    { "'a' + 1", "error:XPTY0004" },
    { "xs:boolean('1') + 1", "error:XPTY0004" },
    { "@a + 1", "double:2" },
    { "x[1] + 1", "error:FORG0001" },
    { "1 + x[1]", "error:FORG0001" },
    { "for $a in x return $a + 1", "error:FORG0001" },
    { "-1", "integer:-1" },
    { "-(1.5)", "decimal:-1.5" },
    { "-xs:byte(1)", "integer:-1" },
    { "-xs:float(1)", "float:-1" },
    { "-1e0", "double:-1" },
    { "+xs:byte(1)", "integer:1" },
    { "+xs:unsignedLong(18446744073709551615)",
      "integer:18446744073709551615" },
    { "+1", "integer:1" },
    { "+(1.5)", "decimal:1.5" },
    { "+xs:float(1)", "float:1" },
    { "--1", "integer:1" },
    { "-()", "" },
    { "-@a", "double:-1" },
    { "-x[1]", "error:FORG0001" },
    { "+'a'", "error:XPTY0004" },
    { "-(1, 2)", "error:XPTY0004" },

    /* Value comparisons */
    { "1 eq 1", "boolean:true" },
    { "1 eq 1.0", "boolean:true" },
    { "1 eq 1e0", "boolean:true" },
    { "1 lt 2", "boolean:true" },
    { "2 le 1", "boolean:false" },
    { "2 gt 1.5", "boolean:true" },
    { "2 ge 2e0", "boolean:true" },
    { "1 ne 1", "boolean:false" },
    { "'a' lt 'b'", "boolean:true" },
    { "'b' eq 'b'", "boolean:true" },
    { "'a' eq 1", "error:XPTY0004" },
    { "@a eq '1'", "boolean:true" },
    { "@a eq 1", "error:XPTY0004" },
    { "() eq 1", "" },
    { "1 eq ()", "" },
    { "(1, 2) eq 1", "error:XPTY0004" },
    { "xs:double('NaN') eq xs:double('NaN')", "boolean:false" },
    { "xs:double('NaN') ne xs:double('NaN')", "boolean:true" },
    { "xs:double('NaN') lt 1", "boolean:false" },
    { "xs:double('NaN') ge 1", "boolean:false" },
    { "xs:float(0.1) eq 0.1", "boolean:true" },
    { "xs:float(0.1) eq 0.1e0", "boolean:false" },
    { "xs:boolean('true') gt xs:boolean('false')", "boolean:true" },
    { "xs:boolean('1') eq xs:boolean('true')", "boolean:true" },
    { "xs:hexBinary('0a') eq xs:hexBinary('0A')", "boolean:true" },
    { "xs:hexBinary('01') lt xs:hexBinary('02')", "boolean:true" },
    { "xs:hexBinary('01') lt xs:hexBinary('0100')", "boolean:true" },
    { "xs:hexBinary('01') eq xs:base64Binary('AQ==')", "error:XPTY0004" },
    { "xs:anyURI('a') eq 'a'", "boolean:true" },
    { "xs:boolean('1') lt 1", "error:XPTY0004" },

    /* General comparisons */
    { "x/@id = 'x2'", "boolean:true" },
    { "x/@id = ('q', 'x1')", "boolean:true" },
    { "x/@id = 'q'", "boolean:false" },
    { "@a = 1", "boolean:true" },
    { "@a = 2", "boolean:false" },
    { "@a = 'x'", "boolean:false" },
    { "@a != 1", "boolean:false" },
    { "@a < 1.5", "boolean:true" },
    { "@a = xs:boolean('1')", "boolean:true" },
    { "(1, 2) = (2, 3)", "boolean:true" },
    { "(1, 2) != (1, 2)", "boolean:true" },
    { "(1, 2) = (3, 4)", "boolean:false" },
    { "() = 1", "boolean:false" },
    { "1 = ()", "boolean:false" },
    { "x[1] = 1", "error:FORG0001" },
    { "1 = 'a'", "error:XPTY0004" },
    { "x = x", "boolean:true" },
    { "x[1] < x[2]", "boolean:true" },
    { "x[1] > x[2]", "boolean:false" },
    { "x[1] >= x[1]", "boolean:true" },
    { "x[1] <= 'one'", "boolean:true" },
    /* An untyped value is cast to xs:anyURI, which collapses
     * whitespace, but compared as a string with a string */
    { "xs:untypedAtomic(' 1 ') = xs:anyURI('1')", "boolean:true" },
    { "xs:anyURI('1') != xs:untypedAtomic(' 1 ')", "boolean:false" },
    { "xs:untypedAtomic(' 1 ') = '1'", "boolean:false" },
    { "xs:untypedAtomic(' 1 ') = xs:token('1')", "boolean:false" },

    /* Node comparisons */
    { "x[1] is x[1]", "boolean:true" },
    { "x[1] is x[2]", "boolean:false" },
    { "x[1] << x[2]", "boolean:true" },
    { "x[1] >> x[2]", "boolean:false" },
    { "x[2] >> x[1]", "boolean:true" },
    { "@a << x[1]", "boolean:true" },
    { ". << @a", "boolean:true" },
    { "() is x[1]", "" },
    { "x[1] is ()", "" },
    { "x is x[1]", "error:XPTY0004" },
    { "1 is x[1]", "error:XPTY0004" },
    { "x[1] << 1", "error:XPTY0004" },

    /* Ranges */
    { "1 to 3", "integer:1 integer:2 integer:3" },
    { "-1 to 1", "integer:-1 integer:0 integer:1" },
    { "3 to 1", "" },
    { "2 to 2", "integer:2" },
    { "() to 3", "" },
    { "1 to ()", "" },
    { "@a to 2", "integer:1 integer:2" },
    { "1.5 to 2", "error:XPTY0004" },
    { "1 to 2e0", "error:XPTY0004" },
    { "x[1] to 2", "error:FORG0001" },
    { "(1, 2) to 3", "error:XPTY0004" },
    { "(1 to 3)[2]", "integer:2" },
    { "xs:byte(1) to xs:byte(2)", "integer:1 integer:2" },
    { "18446744073709551616 to 18446744073709551617",
      "integer:18446744073709551616 integer:18446744073709551617" },
    { "-18446744073709551617 to -18446744073709551616",
      "integer:-18446744073709551617 integer:-18446744073709551616" },
    { "9223372036854775806 to 9223372036854775808",
      "integer:9223372036854775806 integer:9223372036854775807 "
      "integer:9223372036854775808" },
    { "18446744073709551616 to 18446744073709551616000", "error:LIMIT" },
    { "18446744073709551617 to 18446744073709551616", "" },

    /* union, intersect, except */
    { "x | z", "e:x#x1 e:x#x2 e:z" },
    { "z | x", "e:x#x1 e:x#x2 e:z" },
    { "x union x", "e:x#x1 e:x#x2" },
    { "@a | x", "a:a e:x#x1 e:x#x2" },
    { "(x, z) intersect x", "e:x#x1 e:x#x2" },
    { "(x, z) except x", "e:z" },
    { "x except x[1]", "e:x#x2" },
    { "//x intersect z/x", "e:x#x3" },
    { "//x except z/x", "e:x#x1 e:x#x2" },
    { "x intersect ()", "" },
    { "() except x", "" },
    { "x except ()", "e:x#x1 e:x#x2" },
    { "z intersect x", "" },
    { "1 | x", "error:XPTY0004" },
    { "x except 1", "error:XPTY0004" },

    /* instance of, treat as */
    { "1 instance of xs:integer", "boolean:true" },
    { "1 instance of xs:decimal", "boolean:true" },
    { "1.0 instance of xs:integer", "boolean:false" },
    { "1 instance of xs:numeric", "boolean:true" },
    { "1e0 instance of xs:numeric", "boolean:true" },
    { "'a' instance of xs:numeric", "boolean:false" },
    { "'a' instance of xs:anyAtomicType", "boolean:true" },
    { "1 instance of xs:error", "boolean:false" },
    { "x instance of element()+", "boolean:true" },
    { "x instance of element()*", "boolean:true" },
    { "x instance of element()", "boolean:false" },
    { "x instance of element()?", "boolean:false" },
    { "x[1] instance of element(x)", "boolean:true" },
    { "x[1] instance of element(z)", "boolean:false" },
    { "@a instance of attribute()", "boolean:true" },
    { "@a instance of xs:untypedAtomic", "boolean:false" },
    { "() instance of empty-sequence()", "boolean:true" },
    { "1 instance of empty-sequence()", "boolean:false" },
    { "(1, 'a') instance of xs:anyAtomicType*", "boolean:true" },
    { "(1, x) instance of xs:anyAtomicType*", "boolean:false" },
    { "(1, x) instance of item()+", "boolean:true" },
    { "() instance of item()?", "boolean:true" },
    { "() instance of item()", "boolean:false" },
    { "() instance of item()+", "boolean:false" },
    { "1 instance of function(*)", "boolean:false" },
    { "1 instance of map(*)", "boolean:false" },
    { "1 treat as xs:integer", "integer:1" },
    { "1 treat as xs:string", "error:XPDY0050" },
    { "x treat as element()+", "e:x#x1 e:x#x2" },
    { "() treat as empty-sequence()", "" },

    /* cast as, castable as */
    { "'12' cast as xs:integer", "integer:12" },
    { "' 12 ' cast as xs:integer", "integer:12" },
    { "'+12' cast as xs:integer", "integer:12" },
    { "'1.5' cast as xs:integer", "error:FORG0001" },
    { "'abc' cast as xs:integer", "error:FORG0001" },
    { "1.9 cast as xs:integer", "integer:1" },
    { "-1.9 cast as xs:integer", "integer:-1" },
    { "1e3 cast as xs:integer", "integer:1000" },
    { "-2.5e0 cast as xs:integer", "integer:-2" },
    { "xs:double('NaN') cast as xs:integer", "error:FOCA0002" },
    { "xs:double('-INF') cast as xs:decimal", "error:FOCA0002" },
    { "(1e300 cast as xs:integer) eq 1e300", "boolean:true" },
    { "(1e19 cast as xs:integer) eq 10000000000000000000",
      "boolean:true" },
    { "1e0 cast as xs:decimal", "decimal:1" },
    { "0.1e0 cast as xs:decimal", "decimal:0.1" },
    { "1.5e-7 cast as xs:decimal", "decimal:0.00000015" },
    { "-1.5e-7 cast as xs:decimal", "decimal:-0.00000015" },
    { "1e20 cast as xs:decimal", "decimal:100000000000000000000" },
    { "123.456e1 cast as xs:decimal", "decimal:1234.56" },
    { "1234567.5e0 cast as xs:decimal", "decimal:1234567.5" },
    { "-1234567.5e0 cast as xs:decimal", "decimal:-1234567.5" },
    { "xs:float('1234567.5') cast as xs:decimal", "decimal:1234567.5" },
    { "xs:float('0.1') cast as xs:decimal", "decimal:0.1" },
    { "-0e0 cast as xs:decimal", "decimal:0" },
    { "1 cast as xs:decimal", "decimal:1" },
    { "1.5 cast as xs:double", "double:1.5" },
    { "xs:double('-1.75e-3') cast as xs:float", "float:-0.00175" },
    { "1e300 cast as xs:float", "float:INF" },
    { "xs:float(1.5) cast as xs:double", "double:1.5" },
    { "'true' cast as xs:boolean", "boolean:true" },
    { "' 1 ' cast as xs:boolean", "boolean:true" },
    { "'0' cast as xs:boolean", "boolean:false" },
    { "'yes' cast as xs:boolean", "error:FORG0001" },
    { "0 cast as xs:boolean", "boolean:false" },
    { "0.5 cast as xs:boolean", "boolean:true" },
    { "xs:double('NaN') cast as xs:boolean", "boolean:false" },
    { "xs:float('0') cast as xs:boolean", "boolean:false" },
    { "xs:boolean('true') cast as xs:integer", "integer:1" },
    { "xs:boolean('false') cast as xs:decimal", "decimal:0" },
    { "xs:boolean('true') cast as xs:double", "double:1" },
    { "xs:boolean('true') cast as xs:float", "float:1" },
    { "300 cast as xs:byte", "error:FORG0001" },
    { "127 cast as xs:byte", "byte:127" },
    { "-128 cast as xs:byte", "byte:-128" },
    { "65535 cast as xs:unsignedShort", "unsignedShort:65535" },
    { "65536 cast as xs:unsignedShort", "error:FORG0001" },
    { "255 cast as xs:unsignedByte", "unsignedByte:255" },
    { "256 cast as xs:unsignedByte", "error:FORG0001" },
    { "-32769 cast as xs:short", "error:FORG0001" },
    { "1.5 cast as xs:short", "short:1" },
    { "-1 cast as xs:unsignedInt", "error:FORG0001" },
    { "4294967295 cast as xs:unsignedInt", "unsignedInt:4294967295" },
    { "18446744073709551615 cast as xs:unsignedLong",
      "unsignedLong:18446744073709551615" },
    { "18446744073709551616 cast as xs:unsignedLong", "error:FORG0001" },
    { "9223372036854775808 cast as xs:long", "error:FORG0001" },
    { "2147483648 cast as xs:int", "error:FORG0001" },
    { "0 cast as xs:positiveInteger", "error:FORG0001" },
    { "0 cast as xs:nonNegativeInteger", "nonNegativeInteger:0" },
    { "0 cast as xs:negativeInteger", "error:FORG0001" },
    { "-0 cast as xs:nonPositiveInteger", "nonPositiveInteger:0" },
    { "xs:byte(1) cast as xs:integer", "integer:1" },
    { "1.5 cast as xs:string", "string:1.5" },
    { "1e6 cast as xs:string", "string:1.0E6" },
    { "1 cast as xs:untypedAtomic", "untypedAtomic:1" },
    { "xs:boolean('1') cast as xs:string", "string:true" },
    { "' a  b ' cast as xs:token", "token:a b" },
    { "' a  b ' cast as xs:normalizedString", "normalizedString: a  b " },
    { "' a  b ' cast as xs:string", "string: a  b " },
    { "'' cast as xs:token", "token:" },
    { "'en-US' cast as xs:language", "language:en-US" },
    { "'x-1a' cast as xs:language", "language:x-1a" },
    { "'toolonglang' cast as xs:language", "error:FORG0001" },
    { "'en-' cast as xs:language", "error:FORG0001" },
    { "'1en' cast as xs:language", "error:FORG0001" },
    { "'a:b' cast as xs:NCName", "error:FORG0001" },
    { "'a:b' cast as xs:Name", "Name:a:b" },
    { "'::' cast as xs:Name", "Name:::" },
    { "'1a' cast as xs:NMTOKEN", "NMTOKEN:1a" },
    { "':.' cast as xs:NMTOKEN", "NMTOKEN::." },
    { "'1a' cast as xs:Name", "error:FORG0001" },
    { "'' cast as xs:NCName", "error:FORG0001" },
    { "'a b' cast as xs:NMTOKEN", "error:FORG0001" },
    { "'id1' cast as xs:ID", "ID:id1" },
    { "'r1' cast as xs:IDREF", "IDREF:r1" },
    { "'e1' cast as xs:ENTITY", "ENTITY:e1" },
    { "12 cast as xs:NCName", "error:FORG0001" },
    { "12 cast as xs:token", "token:12" },
    { "'0aFF' cast as xs:hexBinary", "hexBinary:0AFF" },
    { "'abc' cast as xs:hexBinary", "error:FORG0001" },
    { "'zz' cast as xs:hexBinary", "error:FORG0001" },
    { "'' cast as xs:hexBinary", "hexBinary:" },
    { "'TWFu' cast as xs:base64Binary", "base64Binary:TWFu" },
    { "'TQ==' cast as xs:base64Binary", "base64Binary:TQ==" },
    { "'TWE=' cast as xs:base64Binary", "base64Binary:TWE=" },
    { "' T Q = = ' cast as xs:base64Binary", "base64Binary:TQ==" },
    { "'TR==' cast as xs:base64Binary", "error:FORG0001" },
    { "'TWF=' cast as xs:base64Binary", "error:FORG0001" },
    { "'TQ=' cast as xs:base64Binary", "error:FORG0001" },
    { "'T===' cast as xs:base64Binary", "error:FORG0001" },
    { "'TQ==TQ==' cast as xs:base64Binary", "error:FORG0001" },
    { "'TW*u' cast as xs:base64Binary", "error:FORG0001" },
    { "'' cast as xs:base64Binary", "base64Binary:" },
    { "'+/09' cast as xs:base64Binary", "base64Binary:+/09" },
    { "xs:hexBinary('4d616e') cast as xs:base64Binary", "base64Binary:TWFu" },
    { "xs:base64Binary('TWFu') cast as xs:hexBinary", "hexBinary:4D616E" },
    { "1 cast as xs:hexBinary", "error:XPTY0004" },
    { "xs:hexBinary('01') cast as xs:double", "error:XPTY0004" },
    { "xs:hexBinary('01') cast as xs:decimal", "error:XPTY0004" },
    { "xs:hexBinary('01') cast as xs:boolean", "error:XPTY0004" },
    { "'' cast as xs:NMTOKEN", "error:FORG0001" },
    { "'1e2' cast as xs:double", "double:100" },
    { "'INF' cast as xs:double", "double:INF" },
    { "'+INF' cast as xs:double", "double:INF" },
    { "'-INF' cast as xs:float", "float:-INF" },
    { "'NaN' cast as xs:double", "double:NaN" },
    { "'inf' cast as xs:double", "error:FORG0001" },
    { "'nan' cast as xs:double", "error:FORG0001" },
    { "'0x10' cast as xs:double", "error:FORG0001" },
    { "'1e' cast as xs:double", "error:FORG0001" },
    { "'1e+' cast as xs:double", "error:FORG0001" },
    { "'.' cast as xs:double", "error:FORG0001" },
    { "'' cast as xs:double", "error:FORG0001" },
    { "'.5' cast as xs:double", "double:0.5" },
    { "'5.' cast as xs:double", "double:5" },
    { "'-1.5E+2' cast as xs:double", "double:-150" },
    { "'1e400' cast as xs:double", "double:INF" },
    { "'1.5' cast as xs:float", "float:1.5" },
    { "'.5' cast as xs:decimal", "decimal:0.5" },
    { "'5.' cast as xs:decimal", "decimal:5" },
    { "'1e2' cast as xs:decimal", "error:FORG0001" },
    { "'http://x' cast as xs:anyURI", "anyURI:http://x" },
    { "xs:anyURI('u') cast as xs:string", "string:u" },
    { "xs:anyURI('u') cast as xs:untypedAtomic", "untypedAtomic:u" },
    { "1 cast as xs:anyURI", "error:XPTY0004" },
    { "xs:anyURI('1') cast as xs:integer", "error:XPTY0004" },
    { "'a' cast as xs:QName", "error:UNSUPPORTED" },
    { "'2020-01-01' cast as xs:date", "error:UNSUPPORTED" },
    { "1 cast as xs:date", "error:XPTY0004" },
    { "() cast as xs:integer?", "" },
    { "() cast as xs:integer", "error:XPTY0004" },
    { "(1, 2) cast as xs:integer", "error:XPTY0004" },
    { "@a cast as xs:integer", "integer:1" },
    { "x[1] cast as xs:string", "string:one" },
    { "'1' castable as xs:integer", "boolean:true" },
    { "'a' castable as xs:integer", "boolean:false" },
    { "() castable as xs:integer", "boolean:false" },
    { "() castable as xs:integer?", "boolean:true" },
    { "(1, 2) castable as xs:integer", "boolean:false" },
    { "1 castable as xs:date", "boolean:false" },
    { "xs:double('NaN') castable as xs:integer", "boolean:false" },
    { "'2020-01-01' castable as xs:date", "error:UNSUPPORTED" },
    { "'1' cast as xs:numeric", "double:1" },
    { "1 cast as xs:numeric", "integer:1" },
    { "'a' cast as xs:numeric", "error:FORG0001" },
    { "'1' castable as xs:numeric", "boolean:true" },
    { "xs:boolean('1') cast as xs:numeric", "double:1" },
    { "'a b  c' cast as xs:NMTOKENS", "NMTOKEN:a NMTOKEN:b NMTOKEN:c" },
    { "' a ' cast as xs:IDREFS", "IDREF:a" },
    { "'' cast as xs:NMTOKENS", "error:FORG0001" },
    { "1 cast as xs:NMTOKENS", "error:XPTY0004" },
    { "'a 1:' cast as xs:IDREFS", "error:FORG0001" },
    { "'a b' cast as xs:ENTITIES", "ENTITY:a ENTITY:b" },
    { "1 cast as xs:error", "error:FORG0001" },
    { "() cast as xs:error?", "" },
    { "1 castable as xs:error", "boolean:false" },
    { "xs:integer('5')", "integer:5" },
    { "xs:byte(300)", "error:FORG0001" },
    { "xs:integer(())", "" },

    /* Booleans (F&O 3.1, 7) */
    { "true(), fn:false()", "boolean:true boolean:false" },
    { "not(()), not(x), not(0), not(''), not('a')",
      "boolean:true boolean:false boolean:true boolean:true boolean:false" },
    { "not((1, 2))", "error:FORG0006" },
    { "not(1 to 3)", "error:FORG0006" },
    { "boolean(x), boolean(0.0), boolean(xs:double('NaN')), boolean('a')",
      "boolean:true boolean:false boolean:false boolean:true" },
    { "boolean((x, 1))", "boolean:true" },
    { "boolean((1, x))", "error:FORG0006" },
    { "boolean(xs:QName('xs:x'))", "error:UNSUPPORTED" },

    /* Accessors (2) */
    { "string(), string(x[1]), string(@a), string(())",
      "string:onetwotail string:one string:1 string:" },
    { "string(1.0e0), string(1e7), string(xs:untypedAtomic('u'))",
      "string:1 string:1.0E7 string:u" },
    { "string(comment()), string(processing-instruction())",
      "string:c string:d" },
    { "string((1, 2))", "error:XPTY0004" },
    { "data(x), data(()), data(@a), data((1, 'a'))",
      "untypedAtomic:one untypedAtomic:two untypedAtomic:1 integer:1 "
      "string:a" },
    { "data(), data(comment())", "untypedAtomic:onetwotail string:c" },
    { "data(1 to 3)", "integer:1 integer:2 integer:3" },

    /* Functions on numeric values (4.4) */
    { "abs(-1), abs(2), abs(-1.5), abs(1.5), abs(-0.0), abs(())",
      "integer:1 integer:2 decimal:1.5 decimal:1.5 decimal:0" },
    { "abs(-1.5e0), abs(xs:float('-2.5')), abs(-0e0), abs(xs:float('-0'))",
      "double:1.5 float:2.5 double:0 float:0" },
    { "abs(xs:double('-INF')), abs(xs:float('-INF')), "
      "abs(xs:double('NaN')), abs(xs:float('NaN'))",
      "double:INF float:INF double:NaN float:NaN" },
    { "abs(xs:byte(-4)), abs(xs:positiveInteger(4)), "
      "abs(xs:negativeInteger(-99999999999999999999))",
      "integer:4 integer:4 integer:99999999999999999999" },
    { "abs(xs:byte(-4)) instance of xs:byte, "
      "abs(xs:unsignedShort(4)) instance of xs:unsignedShort, "
      "abs(xs:short(4)) instance of xs:integer, "
      "abs(xs:decimal(-1)) instance of xs:integer",
      "boolean:false boolean:false boolean:true boolean:false" },
    { "abs(@a), abs(xs:untypedAtomic('-3'))", "double:1 double:3" },
    { "abs('1')", "error:XPTY0004" },
    { "abs((1, 2))", "error:XPTY0004" },
    { "abs(xs:untypedAtomic('a'))", "error:FORG0001" },
    { "ceiling(10.5), ceiling(-10.5), ceiling(0.001), ceiling(-0.001), "
      "ceiling(3), ceiling(xs:byte(-3)), ceiling(())",
      "decimal:11 decimal:-10 decimal:1 decimal:0 integer:3 integer:-3" },
    { "floor(10.5), floor(-10.5), floor(0.001), floor(-0.001), "
      "floor(xs:long(7)), floor(())",
      "decimal:10 decimal:-11 decimal:0 decimal:-1 integer:7" },
    { "ceiling(-0.5e0), ceiling(0.5e0), ceiling(-0e0), floor(-0.5e0), "
      "floor(-0e0), floor(0.5e0), ceiling(1e300), floor(xs:float('-1.5'))",
      "double:-0 double:1 double:-0 double:-1 double:-0 double:0 "
      "double:1.0E300 float:-2" },
    { "ceiling(xs:double('-INF')), floor(xs:float('INF')), "
      "ceiling(xs:double('NaN')), floor(xs:float('NaN'))",
      "double:-INF float:INF double:NaN float:NaN" },
    { "ceiling(1.5) instance of xs:decimal, floor(xs:int(1)) instance of "
      "xs:int, floor(xs:int(1)) instance of xs:integer",
      "boolean:true boolean:false boolean:true" },
    { "round(2.5), round(2.4999), round(-2.5), round(-2.51), round(xs:int(7))",
      "decimal:3 decimal:2 decimal:-2 decimal:-3 integer:7" },
    { "round(1.125, 2), round(xs:decimal('12.345'), 2), round(8452, -2), "
      "round(1234, -2), round(-1250, -2), round(5, -1), round(-5, -1)",
      "decimal:1.13 decimal:12.35 integer:8500 integer:1200 integer:-1200 "
      "integer:10 integer:0" },
    { "round(1.5, 1000000), round(1.5, 99999999999999999999), "
      "round(123, -1000000), round(-5.5, -99999999999999999999), "
      "round(0.05, 1), round(-0.05, 1), round(0.0001, 3)",
      "decimal:1.5 decimal:1.5 integer:0 decimal:0 decimal:0.1 decimal:0 "
      "decimal:0" },
    { "round(2.5e0), round(-2.5e0), round(-0.4e0), round(-0.5e0), "
      "round(-0.6e0), round(0.49999999999999994e0), round(xs:float('-1.5'))",
      "double:3 double:-2 double:-0 double:-0 double:-1 double:0 float:-1" },
    { "round(35.425e0, 2), round(3.1415e0, 2), round(-0.04e0, 1), "
      "round(1.5e0, 1000000), round(1e300, -1000000), "
      "round(-1e300, -1000000), round(1.7976931348623157E308, -308)",
      "double:35.42 double:3.14 double:-0 double:1.5 double:0 double:-0 "
      "double:INF" },
    { "round(4.9e-324, 323), round(4.9e-324, 324), round(4.9e-324, 1074)",
      "double:0 double:4.9E-324 double:4.9E-324" },
    { "round(xs:float('1.25'), 1), round(xs:float('0.05'), 1), "
      "round(xs:float(12345.6), -2), round(xs:float('-0.04'), 1), "
      "round(xs:float('3.4028235E38'), -35)",
      "float:1.3 float:0.1 float:12300 float:-0 float:INF" },
    { "round(()), round((), 2), round(xs:double('NaN'), 2), "
      "round(xs:double('-INF')), round(xs:float('INF'), -3), round(0e0, 2), "
      "round(-0e0, -2)",
      "double:NaN double:-INF float:INF double:0 double:-0" },
    { "round(@a, 1), round(xs:untypedAtomic('2.5'))", "double:1 double:3" },
    { "round(1.5, 1.0)", "error:XPTY0004" },
    { "round(1.5, ())", "error:XPTY0004" },
    { "round((1.5, 2))", "error:XPTY0004" },
    { "round('1')", "error:XPTY0004" },
    { "round(1.5, 1 to 1)", "decimal:1.5" },
    { "round-half-to-even(0.5), round-half-to-even(1.5), "
      "round-half-to-even(2.5), round-half-to-even(-2.5), "
      "round-half-to-even(2.51), round-half-to-even(xs:short(-3))",
      "decimal:0 decimal:2 decimal:2 decimal:-2 decimal:3 integer:-3" },
    { "round-half-to-even(35612.25, -2), round-half-to-even(1.125, 2), "
      "round-half-to-even(1.135, 2), round-half-to-even(xs:byte(-25), -1), "
      "round-half-to-even(-35, -1), round-half-to-even(3.567812, 4294967296)",
      "decimal:35600 decimal:1.12 decimal:1.14 integer:-20 integer:-40 "
      "decimal:3.567812" },
    { "round-half-to-even(2.5e0), round-half-to-even(3.5e0), "
      "round-half-to-even(-0.5e0), round-half-to-even(-1.5e0), "
      "round-half-to-even(3.567812e+3, 2), round-half-to-even(4.7564e-3, 2), "
      "round-half-to-even(12350e0, -2), round-half-to-even(-12450e0, -2)",
      "double:2 double:4 double:-0 double:-2 double:3567.81 double:0 "
      "double:12400 double:-12400" },
    { "round-half-to-even(xs:float('0.05'), 1), "
      "round-half-to-even(xs:float('3.75'), 1), "
      "round-half-to-even(xs:float('-0'), 1), "
      "round-half-to-even(xs:float('150.015'), 2), "
      "round-half-to-even(xs:float('-0.5'))",
      "float:0.1 float:3.8 float:-0 float:150.01 float:-0" },
    { "round-half-to-even(()), round-half-to-even((), -1), "
      "round-half-to-even(xs:double('NaN')), "
      "round-half-to-even(xs:double('-INF'), 2)",
      "double:NaN double:-INF" },
    { "round-half-to-even(1, '1')", "error:XPTY0004" },

    /* Trigonometric and exponential functions (4.8) */
    { "math:pi(), 2 * math:pi()",
      "double:3.141592653589793 double:6.283185307179586" },
    { "math:exp(()), math:exp(0), math:exp(-1), math:exp(xs:double('INF')), "
      "math:exp(xs:double('-INF')), math:exp(1000)",
      "double:1 double:0.36787944117144233 double:INF double:0 double:INF" },
    { "math:exp10(()), math:exp10(0), math:exp10(1), math:exp10(-1), "
      "math:exp10(0.5), math:exp10(xs:double('-INF')), math:exp10(400)",
      "double:1 double:10 double:0.1 double:3.1622776601683795 double:0 "
      "double:INF" },
    { "math:log(()), math:log(0), math:log(-0e0), math:log(-1), math:log(2), "
      "math:log(xs:double('INF')), math:log(xs:double('-INF'))",
      "double:-INF double:-INF double:NaN double:0.6931471805599453 "
      "double:INF double:NaN" },
    { "math:log10(()), math:log10(1e3), math:log10(1e-3), math:log10(0), "
      "math:log10(-1), math:log10(2)",
      "double:3 double:-3 double:-INF double:NaN double:0.3010299956639812" },
    { "math:sqrt(()), math:sqrt(0e0), math:sqrt(-0e0), math:sqrt(2), "
      "math:sqrt(-2), math:sqrt(xs:double('INF')), "
      "math:sqrt(xs:double('-INF')), math:sqrt(1e6)",
      "double:0 double:-0 double:1.4142135623730951 double:NaN double:INF "
      "double:NaN double:1000" },
    { "math:sin(()), math:sin(-0e0), math:sin(xs:double('INF')), "
      "math:cos(()), math:cos(-0e0), math:cos(xs:double('NaN')), "
      "math:tan(()), math:tan(-0e0), math:tan(xs:double('-INF'))",
      "double:-0 double:NaN double:1 double:NaN double:-0 double:NaN" },
    { "math:asin(()), math:asin(-0e0), math:asin(1), math:asin(2), "
      "math:acos(()), math:acos(1), math:acos(-1), math:acos(-1.5), "
      "math:atan(()), math:atan(-0e0), math:atan(xs:double('INF'))",
      "double:-0 double:1.5707963267948966 double:NaN double:0 "
      "double:3.141592653589793 double:NaN double:-0 "
      "double:1.5707963267948966" },
    { "math:atan2(0e0, 0e0), math:atan2(-0e0, 0e0), math:atan2(0e0, -0e0), "
      "math:atan2(-0e0, -1), math:atan2(1, 0), math:atan2(-0e0, 1), "
      "math:atan2(xs:double('NaN'), 1)",
      "double:0 double:-0 double:3.141592653589793 "
      "double:-3.141592653589793 double:1.5707963267948966 double:-0 "
      "double:NaN" },
    { "math:atan2((), 1)", "error:XPTY0004" },
    { "math:sqrt('4')", "error:XPTY0004" },
    { "math:sqrt(xs:untypedAtomic('a'))", "error:FORG0001" },
    { "math:pow((), 93.7), math:pow(2, 3), math:pow(-2, 3), math:pow(2, -3), "
      "math:pow(-2, -3), math:pow(0, 0), math:pow(xs:double('NaN'), 0), "
      "math:pow(-0e0, 3), math:pow(-0e0, -3), math:pow(0, -4)",
      "double:8 double:-8 double:0.125 double:-0.125 double:1 double:1 "
      "double:-0 double:-INF double:INF" },
    { "math:pow(-0e0, -3.0e0), math:pow(-0e0, -3.1e0), "
      "math:pow(-0e0, 3.1e0), math:pow(-1, xs:double('INF')), "
      "math:pow(1, xs:double('NaN')), math:pow(-2.5e0, 2.00000001e0), "
      "math:pow(16, 0.25e0), math:pow(-2.5e0, 2.0e0)",
      "double:-INF double:INF double:0 double:1 double:1 double:NaN "
      "double:2 double:6.25" },
    { "math:pow(-1, 9007199254740993), math:pow(-1, 9007199254740992), "
      "math:pow(-1, 99999999999999999999999), "
      "math:pow(-1, -99999999999999999998), "
      "math:pow(2, 99999999999999999999), "
      "math:pow(-2, 99999999999999999999), "
      "math:pow(-0.5e0, 99999999999999999999), "
      "math:pow(-0e0, -99999999999999999999)",
      "double:-1 double:1 double:-1 double:1 double:INF double:-INF "
      "double:-0 double:-INF" },
    { "math:pow(xs:double('-INF'), 3), math:pow(xs:double('-INF'), -3), "
      "math:pow(xs:double('-INF'), 2), "
      "math:pow(xs:double('NaN'), 99999999999999999999)",
      "double:-INF double:-0 double:INF double:NaN" },
    { "math:pow(-2, xs:byte(3)), math:pow(-2, 3.0), "
      "math:pow(2, xs:float(0.5)), math:pow(@a, 2), "
      "math:pow(2, xs:untypedAtomic('3'))",
      "double:-8 double:-8 double:1.4142135623730951 double:1 double:8" },
    { "math:pow(2, ())", "error:XPTY0004" },
    { "math:pow(2, 'a')", "error:XPTY0004" },

    /* Numbers (4.5.1) */
    { "number('12'), number(' 1.5e1 '), number(true()), number(@a)",
      "double:12 double:15 double:1 double:1" },
    { "number('a'), number(()), number(), number(xs:anyURI('1'))",
      "double:NaN double:NaN double:NaN double:NaN" },
    { "number(x[1]), number(1 to 1), number(xs:float(1.5))",
      "double:NaN double:1 double:1.5" },
    { "number((1, 2))", "error:XPTY0004" },
    { "number(x)", "error:XPTY0004" },

    /* Errors (3.1.1) */
    { "error()", "error:FOER0000" },
    { "error(())", "error:FOER0000" },
    { "error((), 'boom')", "error:FOER0000" },
    { "error((), 'boom', (1, x))", "error:FOER0000" },
    { "if (x) then 1 else error()", "integer:1" },
    { "error(1)", "error:XPTY0004" },
    { "error(@a)", "error:XPTY0117" },
    { "error((), 1)", "error:XPTY0004" },
    { "error((), ())", "error:XPTY0004" },

    /* Sequences (14.1) */
    { "empty(()), empty(y), empty(x), exists(()), exists(x)",
      "boolean:true boolean:true boolean:false boolean:false boolean:true" },
    { "exists(1 to 100000000000), empty(1 to 100000000000)",
      "boolean:true boolean:false" },
    { "head((1, 2, 3)), head(()), head(x), head(1 to 100000000000)",
      "integer:1 e:x#x1 integer:1" },
    { "tail((1, 2, 3)), tail(5), tail(()), tail(1 to 3)",
      "integer:2 integer:3 integer:2 integer:3" },
    { "count(tail(1 to 10000000000)), tail(x)", "integer:9999999999 e:x#x2" },
    { "reverse((1, 'a', x[1])), reverse(()), reverse(1 to 3), reverse(7)",
      "e:x#x1 string:a integer:1 integer:3 integer:2 integer:1 integer:7" },
    { "reverse(x)", "e:x#x2 e:x#x1" },
    { "reverse(1 to 100000000000)", "error:LIMIT" },
    { "subsequence((1, 2, 3, 4, 5), 2), subsequence((1, 2, 3, 4, 5), 2, 2)",
      "integer:2 integer:3 integer:4 integer:5 integer:2 integer:3" },
    { "subsequence(1 to 5, 1.5, 2), subsequence(1 to 5, 2.5, 1.5)",
      "integer:2 integer:3 integer:3 integer:4" },
    { "subsequence((1, 2, 3), 0), subsequence((1, 2, 3), -1, 3)",
      "integer:1 integer:2 integer:3 integer:1" },
    { "subsequence((1, 2, 3), -0.5), subsequence((1, 2, 3), 2.4999)",
      "integer:1 integer:2 integer:3 integer:2 integer:3" },
    { "subsequence((1, 2, 3), xs:double('NaN')), "
      "subsequence((1, 2, 3), 1, xs:double('NaN')), "
      "subsequence((1, 2, 3), xs:double('INF')), "
      "subsequence((1, 2, 3), xs:double('-INF'), xs:double('INF')), "
      "subsequence((1, 2, 3), 4), subsequence((1, 2, 3), 2, 0), "
      "subsequence((1, 2, 3), 2, -1), subsequence((), 1)", "" },
    { "subsequence((1, 2, 3), xs:double('-INF')), "
      "subsequence((1, 2, 3), 2, xs:double('INF'))",
      "integer:1 integer:2 integer:3 integer:2 integer:3" },
    { "subsequence((1, 2, 3), -1e300, 1e300), subsequence((1, 2), 1e300)",
      "" },
    { "subsequence(1 to 10000000000, 9999999999)",
      "integer:9999999999 integer:10000000000" },
    { "count(subsequence(1 to 10000000000, 2, 5000000000))",
      "integer:5000000000" },
    { "subsequence(x, @a), subsequence(x, 2, xs:float(1))",
      "e:x#x1 e:x#x2 e:x#x2" },
    { "subsequence((1, 2), '1')", "error:XPTY0004" },
    { "subsequence((1, 2), ())", "error:XPTY0004" },
    { "subsequence((1, 2), (1, 2))", "error:XPTY0004" },
    { "subsequence((1, 2), xs:untypedAtomic('a'))", "error:FORG0001" },
    { "remove((1, 2, 3), 2), remove((1, 2), 0), remove((1, 2), 3)",
      "integer:1 integer:3 integer:1 integer:2 integer:1 integer:2" },
    { "remove(1 to 3, 1), remove(1 to 3, 3), remove(1 to 3, 2)",
      "integer:2 integer:3 integer:1 integer:2 integer:1 integer:3" },
    { "remove((), 1), remove((1, 2), 99999999999999999999), "
      "remove((1, 2), -99999999999999999999)",
      "integer:1 integer:2 integer:1 integer:2" },
    { "remove(x, @a), count(remove(1 to 10000000000, 1))",
      "e:x#x2 integer:9999999999" },
    { "remove((1, 2), 1.0)", "error:XPTY0004" },
    { "insert-before((1, 2, 3), 2, ('a', 'b'))",
      "integer:1 string:a string:b integer:2 integer:3" },
    { "insert-before((1, 2), 0, 'a'), insert-before((1, 2), 3, 'b')",
      "string:a integer:1 integer:2 integer:1 integer:2 string:b" },
    { "insert-before((1, 2), 99999999999999999999, 'a'), "
      "insert-before(1 to 2, -99999999999999999999, 0)",
      "integer:1 integer:2 string:a integer:0 integer:1 integer:2" },
    { "insert-before((), 5, x), insert-before(x, 1, ())",
      "e:x#x1 e:x#x2 e:x#x1 e:x#x2" },
    { "insert-before((1, 2), (), 3)", "error:XPTY0004" },

    /* Cardinality and counting (14.3, 14.4.1) */
    { "exactly-one(1), zero-or-one(()), zero-or-one(x[1]), "
      "one-or-more(1 to 2)",
      "integer:1 e:x#x1 integer:1 integer:2" },
    { "exactly-one(())", "error:FORG0005" },
    { "exactly-one(x)", "error:FORG0005" },
    { "zero-or-one(1 to 2)", "error:FORG0003" },
    { "one-or-more(())", "error:FORG0004" },
    { "count(()), count(x), count(//x), count((1, 2, 3)[. > 1])",
      "integer:0 integer:2 integer:3 integer:2" },
    { "count(1 to 10000000000)", "integer:10000000000" },
    { "count(-9223372036854775808 to 9223372036854775806)",
      "integer:18446744073709551615" },

    /* Aggregate functions (14.4) */
    { "sum((1, 2, 3)), sum(()), sum((), ()), sum((), 'z'), sum((1, 2), 'z')",
      "integer:6 integer:0 string:z integer:3" },
    { "sum((1, 2.5)), sum((1, 2e0)), sum((1, xs:float(2))), "
      "sum((xs:float(1), 2e0))",
      "decimal:3.5 double:3 float:3 double:3" },
    { "sum(xs:byte(1)), sum(xs:byte(1)) instance of xs:byte, "
      "sum((xs:byte(1), xs:byte(2))) instance of xs:byte",
      "byte:1 boolean:true boolean:false" },
    { "sum((@a, 2)), sum(xs:untypedAtomic('1.5')), sum(@a)",
      "double:3 double:1.5 double:1" },
    { "sum((1, xs:double('NaN'))), sum((xs:double('INF'), "
      "xs:double('-INF'))), sum((-0e0, -0e0)), sum(-0e0), "
      "sum((xs:float('NaN'), 1))",
      "double:NaN double:NaN double:-0 double:-0 float:NaN" },
    { "sum(1 to 10), sum(1 to 10000000000), sum(-5 to 5), "
      "sum(9223372036854775806 to 9223372036854775807), "
      "sum(-9223372036854775808 to 9223372036854775806), sum(7 to 7)",
      "integer:55 integer:50000000005000000000 integer:0 "
      "integer:18446744073709551613 integer:-18446744073709551615 "
      "integer:7" },
    { "sum(('a', 1))", "error:FORG0006" },
    { "sum((1, 'a'))", "error:FORG0006" },
    { "sum('a')", "error:FORG0006" },
    { "sum((true(), 1))", "error:FORG0006" },
    { "sum(xs:untypedAtomic('a'))", "error:FORG0001" },
    { "sum(x)", "error:FORG0001" },
    { "avg((3, 4, 5)), avg(()), avg((1, 2)), avg(xs:byte(4)), "
      "avg((1, 2, 2)) eq 5 div 3",
      "decimal:4 decimal:1.5 decimal:4 boolean:true" },
    { "avg(1 to 4), avg(1 to 10000000000), "
      "avg(-9223372036854775808 to 9223372036854775806), avg(-3 to 4), "
      "avg(3 to 3)",
      "decimal:2.5 decimal:5000000000.5 decimal:-1 decimal:0.5 "
      "decimal:3" },
    { "avg((1, 2e0)), avg((xs:float('INF'), xs:float('-INF'))), "
      "avg((3, 4, xs:float('NaN'))), avg((@a, 2)), avg(xs:float(1))",
      "double:1.5 float:NaN float:NaN double:1.5 float:1" },
    { "avg(('a', 1))", "error:FORG0006" },
    { "avg(true())", "error:FORG0006" },
    { "avg(xs:untypedAtomic('a'))", "error:FORG0001" },
    { "min((3, 4, 5)), max((3, 4, 5)), min(()), max(()), "
      "min(1 to 10000000000), max(1 to 10000000000), min(5 to 5)",
      "integer:3 integer:5 integer:1 integer:10000000000 integer:5" },
    { "max((xs:integer(5), xs:float(5.0), xs:double(0))), "
      "min((xs:integer(5), xs:float(5), xs:double(10))), "
      "min((1, xs:float(2))), max((2.5, xs:float(1))), "
      "max((1, 2.5)), min((1, 2.5))",
      "double:5 double:5 float:1 float:2.5 decimal:2.5 integer:1" },
    { "min((xs:byte(3), 5)) instance of xs:byte, max((@a, 0)), "
      "min(xs:untypedAtomic('2')), max(xs:short(4))",
      "boolean:true double:1 double:2 short:4" },
    { "min((1, xs:double('NaN'))), max((xs:double('NaN'), 1)), "
      "min((xs:float('NaN'), 1)), max((xs:float('NaN'), 1e0)), "
      "min(xs:double('NaN'))",
      "double:NaN double:NaN float:NaN double:NaN double:NaN" },
    { "min((true(), false())), max((false(), true())), max(false())",
      "boolean:false boolean:true boolean:false" },
    { "max((xs:hexBinary('0A'), xs:hexBinary('0B'))), "
      "min((xs:base64Binary('AQ=='), xs:base64Binary('AA==')))",
      "hexBinary:0B base64Binary:AA==" },
    { "min((1, 'a'))", "error:FORG0006" },
    { "max(('a', 1))", "error:FORG0006" },
    { "min((true(), 1))", "error:FORG0006" },
    { "max((xs:hexBinary('0A'), xs:base64Binary('AA==')))",
      "error:FORG0006" },
    { "min(('a', 'b'))", "error:UNSUPPORTED" },
    { "max((xs:anyURI('a'), 'b'))", "error:UNSUPPORTED" },
    { "min(xs:untypedAtomic('a'))", "error:FORG0001" },
    { "max(x)", "error:FORG0001" },

    /* The focus (15) */
    { "position(), last()", "integer:1 integer:1" },
    { "(5, 6, 7)[position() = last()], x[last()], x[position() < 2]",
      "integer:7 e:x#x2 e:x#x1" },
    { "(1 to 5)[position() > 3], *[last() - 1]",
      "integer:4 integer:5 e:x#x2" },
    { "x/position(), x/last()", "integer:1 integer:2 integer:2 integer:2" },

    /* String concatenation (XPath 3.1, 3.6) */
    { "'a' || 'b', () || 'b', 1 || 2.5, x[1] || x[2], 1e0 || ''",
      "string:ab string:b string:12.5 string:onetwo string:1" },
    { "'a' || (1, 2)", "error:XPTY0004" },
    { "'a' || x", "error:XPTY0004" },
    { "'' || ()", "string:" },

    /* Simple map (3.15) */
    { "(1, 2, 3) ! (. * 2)", "integer:2 integer:4 integer:6" },
    { "x ! @id", "a:id a:id" },
    { "(x[2], x[1], x[2]) ! .", "e:x#x2 e:x#x1 e:x#x2" },
    { "x ! (y, .)", "e:x#x1 e:y e:x#x2" },
    { "(1 to 3) ! position(), (1 to 3) ! last()",
      "integer:1 integer:2 integer:3 integer:3 integer:3 integer:3" },
    { "() ! 1, 1 ! . ! (. + 1), x ! 1", "integer:2 integer:1 integer:1" },
    { "(1, 2) ! x", "error:XPTY0020" },

    /* Arrows (3.16) */
    { "(1, 2, 3) => count(), (1, 2, 3) => subsequence(2)",
      "integer:3 integer:2 integer:3" },
    { "x => reverse(), 'a' => string(), 2 => xs:double()",
      "e:x#x2 e:x#x1 string:a double:2" },
    { "(1, 2) => exactly-one()", "error:FORG0005" },

    /* Not implemented yet */
    { "upper-case('a')", "error:UNSUPPORTED" },
    { "'a' => upper-case()", "error:UNSUPPORTED" },
    { "subsequence(?, 1)", "error:UNSUPPORTED" },
    { "count#1", "error:UNSUPPORTED" },
    { "map { 1: 2 }", "error:UNSUPPORTED" },
    { "(1)?*", "error:UNSUPPORTED" },
    { "(1)(2)", "error:UNSUPPORTED" },
};

static xmlDocPtr
parseDoc(const char *xml) {
    xmlDocPtr doc = xmlReadMemory(xml, (int) strlen(xml), "test.xml", NULL,
                                  XML_PARSE_NOENT);

    check(doc != NULL, "test document parsed");
    return(doc);
}

/* Every case optimized, as by default, and without optimizing: the
 * results must be the same */
static void
testCases(void) {
    xmlDocPtr doc = parseDoc(xp3TestDoc);
    xp3DynamicContext *ctx = xp3NewDynamicContext();
    xp3StaticContext *plain = xp3NewStaticContext();
    size_t i;

    if ((doc == NULL) || (ctx == NULL) || (plain == NULL) ||
        (xp3SetContextNode(ctx, xmlDocGetRootElement(doc)) < 0)) {
        check(0, "dynamic context set up");
    } else {
        plain->noOptimize = 1;
        for (i = 0; i < sizeof(xp3Cases) / sizeof(xp3Cases[0]); i++) {
            checkEval(NULL, ctx, xp3Cases[i].expr, xp3Cases[i].expected);
            checkEval(plain, ctx, xp3Cases[i].expr, xp3Cases[i].expected);
        }
    }
    xp3FreeStaticContext(plain);
    xp3FreeDynamicContext(ctx);
    xmlFreeDoc(doc);
}

/*
 * Constants are shared by every evaluation of a compiled expression,
 * which may run in several threads: their counts must never change,
 * and results must outlive the expression.
 */
static void
testConstants(void) {
    xp3Expr *expr = xp3Compile(NULL, BAD_CAST "(1, 'a', 2.5)", NULL);
    xp3Sequence *kept = NULL;
    xp3Item item;
    char actual[128];
    int i;

    if ((expr == NULL) || (expr->constantCount != 1) ||
        (expr->ast->root->kind != XP3_AST_CONSTANT)) {
        check(0, "the expression is one constant");
        xp3FreeExpr(expr);
        return;
    }
    for (i = 0; i < 1000; i++) {
        xp3Sequence *result = xp3Evaluate(expr, NULL, NULL);

        if (result == NULL) {
            check(0, "a constant evaluates");
            break;
        }
        if (kept == NULL)
            kept = result;
        else
            xp3FreeSequence(result);
    }
    check(expr->constants[0]->refs == XP3_REFS_CONSTANT,
          "the count of a constant never changes");
    check((xp3ValueItemAt(expr->constants[0], 1, &item) > 0) &&
          (item.u.atomic->refs == XP3_REFS_CONSTANT),
          "nor those of its atomic values");
    check((kept != NULL) && (kept != expr->constants[0]),
          "a result is not the constant itself");
    xp3FreeExpr(expr);
    describe(kept, actual, sizeof(actual));
    check(strcmp(actual, "integer:1 string:a decimal:2.5") == 0,
          "a result outlives its expression");
    xp3FreeSequence(kept);
}

/*
 * Effective boolean values of types expressions can't produce yet
 */

static void
checkBoolean(xp3Atomic *atomic, xp3ErrCode code, int expected,
             const char *what) {
    xp3Value *value;
    int result = -1;

    if (atomic == NULL) {
        check(0, what);
        return;
    }
    value = xp3ValueSingle(xp3ItemFromAtomic(atomic));
    check((value != NULL) && (xp3EffectiveBoolean(value, &result) == code) &&
          ((code != XP3_ERR_OK) || (result == expected)), what);
    xp3ValueUnref(value);
}

static void
testEffectiveBoolean(void) {
    checkBoolean(xp3AtomicNewBoolean(1), XP3_ERR_OK, 1, "true");
    checkBoolean(xp3AtomicNewBoolean(0), XP3_ERR_OK, 0, "false");
    checkBoolean(xp3AtomicNewFloat(0.5f), XP3_ERR_OK, 1, "float");
    checkBoolean(xp3AtomicNewFloat(0.0f), XP3_ERR_OK, 0, "float zero");
    checkBoolean(xp3AtomicNewFloat(xp3DoubleNaN()), XP3_ERR_OK, 0,
                 "float NaN");
    checkBoolean(xp3AtomicNewDouble(xp3DoubleNaN()), XP3_ERR_OK, 0,
                 "double NaN");
    checkBoolean(xp3AtomicNewDouble(-0.0), XP3_ERR_OK, 0, "double -0");
    checkBoolean(xp3AtomicNewString(XP3_TYPE_ANY_URI, BAD_CAST "u", 1),
                 XP3_ERR_OK, 1, "anyURI");
    checkBoolean(xp3AtomicNewString(XP3_TYPE_UNTYPED_ATOMIC, BAD_CAST "", 0),
                 XP3_ERR_OK, 0, "empty untypedAtomic");
    checkBoolean(xp3AtomicNewString(XP3_TYPE_TOKEN, BAD_CAST "t", 1),
                 XP3_ERR_OK, 1, "a type derived from string");
    checkBoolean(xp3AtomicNewBinary(XP3_TYPE_HEX_BINARY,
                                    (const unsigned char *) "a", 1),
                 XP3_ERR_FORG0006, 0, "hexBinary has none");
    checkBoolean(xp3AtomicNewQName(NULL, NULL, BAD_CAST "q"),
                 XP3_ERR_FORG0006, 0, "QName has none");
}

/* Build an expression from a head, a part repeated and a tail */
static char *
repeatExpr(const char *head, const char *part, size_t count,
           const char *tail) {
    size_t size = strlen(head) + strlen(part) * count + strlen(tail) + 1;
    char *expr = xmlMalloc(size);
    size_t used;
    size_t i;

    if (expr == NULL)
        return(NULL);
    used = (size_t) snprintf(expr, size, "%s", head);
    for (i = 0; i < count; i++)
        used += (size_t) snprintf(expr + used, size - used, "%s", part);
    snprintf(expr + used, size - used, "%s", tail);
    return(expr);
}

static void
checkRepeated(const char *head, const char *part, size_t count,
              const char *tail, const char *expected, const char *what) {
    char *expr = repeatExpr(head, part, count, tail);
    char actual[64];

    if (expr == NULL) {
        check(0, what);
        return;
    }
    run(NULL, NULL, expr, actual, sizeof(actual));
    if (strcmp(actual, expected) != 0) {
        fprintf(stderr, "FAIL: %s: %s\n", what, actual);
        failures++;
    }
    xmlFree(expr);
}

/* Values past the limit of decimal digits */
static void
testHugeNumbers(void) {
    char *digits = repeatExpr("", "9", 60000, "");
    char *expr;
    char actual[64];
    size_t size;

    if (digits == NULL) {
        check(0, "digits allocated");
        return;
    }
    size = 2 * strlen(digits) + 4;
    expr = xmlMalloc(size);
    if (expr != NULL) {
        snprintf(expr, size, "%s * %s", digits, digits);
        run(NULL, NULL, expr, actual, sizeof(actual));
        check(strcmp(actual, "error:FOAR0002") == 0,
              "a product with too many digits overflows");
        xmlFree(expr);
    }
    xmlFree(digits);
    checkRepeated("'", "7", XP3_DECIMAL_MAX_DIGITS + 10,
                  "' cast as xs:integer", "error:FOCA0003",
                  "a string with too many digits for an integer");
    checkRepeated("'", "7", XP3_DECIMAL_MAX_DIGITS + 10,
                  "' cast as xs:decimal", "error:FOCA0001",
                  "a string with too many digits for a decimal");
    checkRepeated("sum((1, ", "9", XP3_DECIMAL_MAX_DIGITS, "))",
                  "error:FOAR0002", "a sum past the limit of digits");
    checkRepeated("avg((1, ", "9", XP3_DECIMAL_MAX_DIGITS, "))",
                  "error:FOAR0002", "an average past the limit of digits");
    checkRepeated("round(", "9", XP3_DECIMAL_MAX_DIGITS, ", -1)",
                  "error:FOAR0002", "rounding up past the limit of digits");
    checkRepeated("round(", "9", XP3_DECIMAL_MAX_DIGITS,
                  ", -99999999999)", "integer:0",
                  "rounding to a unit beyond the limit of digits");
    checkRepeated("floor(-0.", "9", XP3_DECIMAL_MAX_DIGITS - 1, ")",
                  "decimal:-1", "the floor of a long fraction");
}

/*
 * Operations on values no expression can make yet
 */
static void
testQNameComparison(void) {
    xp3Atomic *first = xp3AtomicNewQName(BAD_CAST "urn:a", BAD_CAST "p",
                                         BAD_CAST "l");
    xp3Atomic *same = xp3AtomicNewQName(BAD_CAST "urn:a", BAD_CAST "q",
                                        BAD_CAST "l");
    xp3Atomic *other = xp3AtomicNewQName(NULL, NULL, BAD_CAST "l");
    xp3Atomic *negated = NULL;
    int result = -1;

    if ((first == NULL) || (same == NULL) || (other == NULL)) {
        check(0, "QNames created");
    } else {
        check((xp3CompareAtomics(XP3_OP_VAL_EQ, first, same, XP3_XPATH_31,
                                 &result) == XP3_ERR_OK) && (result == 1),
              "QNames with the same URI and local name are equal");
        check((xp3CompareAtomics(XP3_OP_GEN_NE, first, other, XP3_XPATH_31,
                                 &result) == XP3_ERR_OK) && (result == 1),
              "QNames in different namespaces differ");
        check(xp3CompareAtomics(XP3_OP_VAL_LT, first, same, XP3_XPATH_31,
                                &result) == XP3_ERR_XPTY0004,
              "QNames have no order");
        check((xp3NegateNumber(first, &negated) == XP3_ERR_XPTY0004) &&
              (negated == NULL), "a QName can't be negated");
    }
    xp3AtomicUnref(first);
    xp3AtomicUnref(same);
    xp3AtomicUnref(other);
}

/* A literal with more digits than decimals have */
static void
testHugeLiteral(void) {
    size_t digits = XP3_DECIMAL_MAX_DIGITS + 10;
    char *expr = xmlMalloc(digits + 1);
    char actual[64];

    if (expr == NULL) {
        check(0, "huge literal allocated");
        return;
    }
    memset(expr, '7', digits);
    expr[digits] = 0;
    run(NULL, NULL, expr, actual, sizeof(actual));
    check(strcmp(actual, "error:LIMIT") == 0, "an integer too long");
    xmlFree(expr);
}

/*
 * Long lists of bindings are not nesting, so the parser's depth limit
 * doesn't bound them; evaluating them must not recurse per binding.
 */
static void
checkManyBindings(const char *head, const char *part, const char *tail,
                  const char *expected) {
    size_t count = 50000;
    size_t size = strlen(head) + strlen(part) * count + strlen(tail) + 1;
    char *expr = xmlMalloc(size);
    char actual[64];
    size_t used;
    size_t i;

    if (expr == NULL) {
        check(0, "long expression allocated");
        return;
    }
    used = (size_t) snprintf(expr, size, "%s", head);
    for (i = 0; i < count; i++)
        used += (size_t) snprintf(expr + used, size - used, "%s", part);
    snprintf(expr + used, size - used, "%s", tail);
    run(NULL, NULL, expr, actual, sizeof(actual));
    if (strcmp(actual, expected) != 0) {
        fprintf(stderr, "FAIL: %s with %lu bindings: %s\n", head,
                (unsigned long) count, actual);
        failures++;
    }
    xmlFree(expr);
}

static void
testManyBindings(void) {
    checkManyBindings("for $a in 1", ", $b in (1, 2)[1]", " return $b",
                      "integer:1");
    checkManyBindings("let $a := 1", ", $a := ($a, 1)[1]", " return $a",
                      "integer:1");
    checkManyBindings("some $a in 1", ", $b in 1", " satisfies $b",
                      "boolean:true");
    checkManyBindings("every $a in 1", ", $b in 1", " satisfies 0",
                      "boolean:false");
    /* Each binding refers back to the first, which static analysis
     * once found by walking back through all the others */
    checkManyBindings("for $first in 1", ", $b in $first", " return $b",
                      "integer:1");
}

/*
 * The focus
 */

static void
testFocus(void) {
    xmlDocPtr doc = parseDoc("<r xmlns:p='urn:p'>a<![CDATA[b]]>c</r>");
    xmlNodePtr loose = xmlNewNode(NULL, BAD_CAST "loose");
    xp3DynamicContext *ctx = xp3NewDynamicContext();
    xmlNodePtr root;

    if ((doc == NULL) || (ctx == NULL) || (loose == NULL)) {
        check(0, "focus test set up");
        goto done;
    }
    root = xmlDocGetRootElement(doc);

    checkEval(NULL, ctx, "x", "error:XPDY0002");
    checkEval(NULL, ctx, ".", "error:XPDY0002");
    checkEval(NULL, ctx, "/", "error:XPDY0002");
    checkEval(NULL, ctx, "1", "integer:1");
    checkEval(NULL, NULL, "(1, 2)[2]", "integer:2");
    checkEval(NULL, NULL, "x", "error:XPDY0002");

    check(xp3SetContextNode(ctx, root) == 0, "context node set");
    checkEval(NULL, ctx, "text()", "t:abc");
    check(xp3SetContextNode(ctx, root->last) == 0,
          "a later node of a text run as the context node");
    checkEval(NULL, ctx, ".", "t:abc");
    check(xp3SetContextNode(ctx, root->children->next) == 0,
          "a CDATA section as the context node");
    checkEval(NULL, ctx, "..", "e:r");
    check(xp3SetContextNode(ctx, (xmlNodePtr) root->nsDef) == -1,
          "no namespace declaration as the context node");
    check(xp3SetContextNode(NULL, root) == -1, "no context");
    check(xp3SetContextNode(ctx, NULL) == 0, "context node cleared");
    checkEval(NULL, ctx, ".", "error:XPDY0002");

    check(xp3SetContextNode(ctx, loose) == 0, "a node without document");
    checkEval(NULL, ctx, ".", "e:loose");
    checkEval(NULL, ctx, "/", "error:XPDY0050");
    checkEval(NULL, ctx, "//x", "error:XPDY0050");

done:
    xp3FreeDynamicContext(ctx);
    xmlFreeNode(loose);
    xmlFreeDoc(doc);
}

/*
 * Functions that read the focus raise XPDY0002 without one, also in a
 * folded subexpression; the others don't need it.
 */
static void
testFunctionFocus(void) {
    checkEval(NULL, NULL, "position()", "error:XPDY0002");
    checkEval(NULL, NULL, "last()", "error:XPDY0002");
    checkEval(NULL, NULL, "string()", "error:XPDY0002");
    checkEval(NULL, NULL, "data()", "error:XPDY0002");
    checkEval(NULL, NULL, "number()", "error:XPDY0002");
    checkEval(NULL, NULL, "(1, 2)[position() = 2]", "integer:2");
    checkEval(NULL, NULL, "(1, 2) ! string()", "string:1 string:2");
    checkEval(NULL, NULL, "('1', 'a') ! number()", "double:1 double:NaN");
    checkEval(NULL, NULL, "count((1, 2))", "integer:2");
}

/* Binary values are ordered from XPath 3.1 on, so fn:max can't
 * compare them before */
static void
testAggregateLevels(void) {
    xp3StaticContext *sctx = xp3NewStaticContext();

    if ((sctx == NULL) ||
        (xp3SetStaticContextLanguageLevel(sctx, XP3_XPATH_30) < 0)) {
        check(0, "static context at level 3.0");
    } else {
        checkEval(sctx, NULL, "max((xs:hexBinary('0A'), "
                  "xs:hexBinary('0B')))", "error:FORG0006");
        checkEval(sctx, NULL, "max(xs:hexBinary('0A'))", "error:FORG0006");
        checkEval(sctx, NULL, "max((true(), false()))", "boolean:true");
    }
    xp3FreeStaticContext(sctx);
}

/*
 * fn:error: the code with its namespace, and the description as the
 * message
 */
static void
testUserErrors(void) {
    static const struct {
        const char *expr;
        const char *ns;
        const char *message;
    } cases[] = {
        { "error()", XP3_ERR_NS, NULL },
        { "error((), 'boom')", XP3_ERR_NS, "boom" },
        { "error((), xs:anyURI('urn:x'), 1)", XP3_ERR_NS, "urn:x" },
        { "error((), ' ' || 1)", XP3_ERR_NS, " 1" }
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        xp3Expr *expr = xp3Compile(NULL, BAD_CAST cases[i].expr, NULL);
        xp3Error *err = NULL;
        const xmlChar *message;

        check(xp3Evaluate(expr, NULL, &err) == NULL, cases[i].expr);
        message = xp3ErrorMessage(err);
        check((err != NULL) &&
              xmlStrEqual(xp3ErrorCode(err), BAD_CAST "FOER0000") &&
              xmlStrEqual(xp3ErrorNamespace(err), BAD_CAST cases[i].ns) &&
              (xp3ErrorLine(err) == 1) && (xp3ErrorColumn(err) == 1) &&
              (message != NULL) &&
              ((cases[i].message == NULL) ||
               xmlStrEqual(message, BAD_CAST cases[i].message)),
              cases[i].expr);
        xp3ErrorFree(err);
        xp3FreeExpr(expr);
    }
}

/* The context node is mapped to its XDM node when an evaluation
 * starts, so the tree may change after it is set. libxml2 merges a
 * text node into an adjacent one, but not a CDATA section, so these
 * build runs of more than one node. */
static void
testTreeChange(void) {
    xp3DynamicContext *ctx = xp3NewDynamicContext();
    xmlDocPtr doc = parseDoc("<r><a/>tx<b/></r>");
    xmlNodePtr root = xmlDocGetRootElement(doc);

    if ((ctx == NULL) || (root == NULL)) {
        check(0, "tree change test set up");
    } else {
        xmlNodePtr text = root->children->next;

        /* Text added before the context node starts its run */
        check(xp3SetContextNode(ctx, text) == 0, "text as the context node");
        checkEval(NULL, ctx, ".", "t:tx");
        check(xmlAddPrevSibling(text, xmlNewCDataBlock(doc, BAD_CAST "pre",
                                                       3)) != NULL,
              "CDATA section added before the context node");
        checkEval(NULL, ctx, ".", "t:pretx");
        checkEval(NULL, ctx, ". is ../text()[1]", "boolean:true");
        checkEval(NULL, ctx, "(. | ../text())[1] is .", "boolean:true");
        checkEval(NULL, ctx, "following-sibling::node()", "e:b");
        checkEval(NULL, ctx, "preceding-sibling::node()", "e:a");

        /* The node that started the run is removed */
        text = root->children->next;
        check(xp3SetContextNode(ctx, text->next) == 0,
              "a later node of the run as the context node");
        xmlUnlinkNode(text);
        xmlFreeNode(text);
        checkEval(NULL, ctx, ".", "t:tx");
        checkEval(NULL, ctx, ". is ../text()[1]", "boolean:true");

        /* The run loses its content, then gets it back */
        text = root->children->next;
        xmlNodeSetContent(text, BAD_CAST "");
        checkEval(NULL, ctx, ".", "error:XPDY0002");
        checkEval(NULL, ctx, "1", "error:XPDY0002");
        check(xp3SetContextNode(ctx, text) == -1,
              "no text without content as the context node");
        xmlNodeSetContent(text, BAD_CAST "back");
        checkEval(NULL, ctx, ".", "t:back");
    }
    xp3FreeDynamicContext(ctx);
    xmlFreeDoc(doc);
}

/* Text that libxml2 stored without checking it: a lead byte from 0xF8
 * is not UTF-8, so the text is no NCName or NMTOKEN, while the same
 * bytes with lead byte 0xF0 are U+10000, a name character. */
static void
testInvalidUtf8(void) {
    static const struct {
        const char *text;
        const char *expected;
    } cases[] = {
        { "\xf8\x90\x80\x80", "boolean:false" },
        { "\xf0\x90\x80\x80", "boolean:true" }
    };
    xp3DynamicContext *ctx = xp3NewDynamicContext();
    size_t i;

    check(ctx != NULL, "dynamic context created");
    for (i = 0; (ctx != NULL) && (i < sizeof(cases) / sizeof(cases[0]));
         i++) {
        xmlDocPtr doc = parseDoc("<r/>");
        xmlNodePtr root = xmlDocGetRootElement(doc);

        if ((root == NULL) || (xmlAddChild(root, xmlNewDocText(doc,
                BAD_CAST cases[i].text)) == NULL)) {
            check(0, "text node added");
            xmlFreeDoc(doc);
            break;
        }
        check(xp3SetContextNode(ctx, root) == 0, "context node set");
        checkEval(NULL, ctx, ". castable as xs:NCName", cases[i].expected);
        checkEval(NULL, ctx, ". castable as xs:NMTOKEN", cases[i].expected);
        xp3SetContextNode(ctx, NULL);
        xmlFreeDoc(doc);
    }
    xp3FreeDynamicContext(ctx);
}

/*
 * Variables
 */

static void
testVariables(void) {
    xmlDocPtr doc = parseDoc(xp3TestDoc);
    xp3StaticContext *sctx = xp3NewStaticContext();
    xp3DynamicContext *ctx = xp3NewDynamicContext();
    const xmlChar *hello = BAD_CAST "hello";
    xp3Sequence *nodes = NULL;
    xp3Sequence *text = xp3NewStringSequence(&hello, 1);
    xmlNodePtr list[2];

    if ((doc == NULL) || (sctx == NULL) || (ctx == NULL) ||
        (text == NULL)) {
        check(0, "variable test set up");
        goto done;
    }
    list[0] = xmlDocGetRootElement(doc)->last;              /* z */
    list[1] = xmlDocGetRootElement(doc)->children;          /* x1 */
    nodes = xp3NewNodeSequence(list, 2);
    check(nodes != NULL, "node sequence created");
    check((xp3DeclareVariable(sctx, NULL, BAD_CAST "v") == 0) &&
          (xp3DeclareVariable(sctx, BAD_CAST "urn:w", BAD_CAST "w") == 0) &&
          (xp3DeclareNamespace(sctx, BAD_CAST "w", BAD_CAST "urn:w") == 0),
          "variables declared");

    checkEval(sctx, ctx, "$v", "error:XPDY0002");
    checkEval(sctx, ctx, "1", "integer:1");
    check(xp3BindVariable(ctx, NULL, BAD_CAST "v", nodes) == 0,
          "variable bound");
    checkEval(sctx, ctx, "$v", "e:z e:x#x1");
    checkEval(sctx, ctx, "$v/.", "e:x#x1 e:z");
    checkEval(sctx, ctx, "$v[1]/x", "e:x#x3");
    checkEval(sctx, ctx, "$v/@id", "a:id");
    checkEval(sctx, ctx, "for $v in (1, 2) return $v",
              "integer:1 integer:2");
    checkEval(sctx, ctx, "$w:w", "error:XPDY0002");
    check(xp3BindVariable(ctx, BAD_CAST "urn:w", BAD_CAST "w", text) == 0,
          "variable in a namespace bound");
    checkEval(sctx, ctx, "$w:w", "string:hello");
    checkEval(sctx, ctx, "$Q{urn:w}w, $v", "string:hello e:z e:x#x1");
    check(xp3BindVariable(ctx, BAD_CAST "", BAD_CAST "v", text) == 0,
          "variable rebound, empty namespace");
    checkEval(sctx, ctx, "$v", "string:hello");
    check(xp3BindVariable(ctx, NULL, BAD_CAST "v", NULL) == 0,
          "variable unbound");
    checkEval(sctx, ctx, "$v", "error:XPDY0002");
    check(xp3BindVariable(ctx, NULL, BAD_CAST "never", NULL) == 0,
          "unbinding an unbound variable");
    check(xp3BindVariable(ctx, NULL, NULL, text) == -1, "no name");
    check(xp3BindVariable(NULL, NULL, BAD_CAST "v", text) == -1,
          "no context");

done:
    xp3FreeSequence(nodes);
    xp3FreeSequence(text);
    xp3FreeDynamicContext(ctx);
    xp3FreeStaticContext(sctx);
    xmlFreeDoc(doc);
}

/*
 * Limits, xp3EvaluateBoolean and errors
 */

static void
testLimits(void) {
    xmlDocPtr doc = parseDoc(xp3TestDoc);
    xp3DynamicContext *ctx = xp3NewDynamicContext();

    if ((doc == NULL) || (ctx == NULL)) {
        check(0, "limit test set up");
        goto done;
    }
    xp3SetContextNode(ctx, xmlDocGetRootElement(doc));
    check(xp3SetMaxSequenceLength(ctx, 0) == -1, "a limit of 0");
    check(xp3SetMaxSequenceLength(NULL, 5) == -1, "no context");
    check(xp3SetMaxSequenceLength(ctx, 2) == 0, "limit set");
    checkEval(NULL, ctx, "x", "e:x#x1 e:x#x2");
    checkEval(NULL, ctx, "(1, 2)", "integer:1 integer:2");
    checkEval(NULL, ctx, "(1, 2, 3)", "error:LIMIT");
    checkEval(NULL, ctx, "*", "error:LIMIT");
    checkEval(NULL, ctx, "//x", "error:LIMIT");
    checkEval(NULL, ctx, "(1, 2, 3)", "error:LIMIT");
    /* Folded to one item, so no sequence of three is built */
    checkEval(NULL, ctx, "(1, 2, 3)[2]", "integer:2");
    checkEval(NULL, ctx, "(x, x)[1]", "error:LIMIT");
    checkEval(NULL, ctx, "for $i in (1, 2) return ($i, $i)", "error:LIMIT");

done:
    xp3FreeDynamicContext(ctx);
    xmlFreeDoc(doc);
}

static int
evalBoolean(xp3DynamicContext *ctx, const char *expr, const char **code) {
    xp3Error *err = NULL;
    xp3Expr *compiled = xp3Compile(NULL, BAD_CAST expr, NULL);
    int ret = xp3EvaluateBoolean(compiled, ctx, &err);

    *code = (err != NULL) ? (const char *) xp3ErrorCode(err) : "";
    if (err != NULL) {
        static char saved[32];

        snprintf(saved, sizeof(saved), "%s", *code);
        *code = saved;
    }
    xp3ErrorFree(err);
    xp3FreeExpr(compiled);
    return(ret);
}

static void
testBoolean(void) {
    xmlDocPtr doc = parseDoc(xp3TestDoc);
    xp3DynamicContext *ctx = xp3NewDynamicContext();
    const char *code;

    if ((doc == NULL) || (ctx == NULL)) {
        check(0, "boolean test set up");
        goto done;
    }
    xp3SetContextNode(ctx, xmlDocGetRootElement(doc));
    check(evalBoolean(ctx, "x", &code) == 1, "nodes are true");
    check(evalBoolean(ctx, "q", &code) == 0, "nothing is false");
    check(evalBoolean(ctx, "'a'", &code) == 1, "a string");
    check(evalBoolean(ctx, "0", &code) == 0, "zero");
    check((evalBoolean(ctx, "(1, 2)", &code) == -1) &&
          (strcmp(code, "FORG0006") == 0), "no effective boolean value");
    check((evalBoolean(ctx, "1 div 0", &code) == -1) &&
          (strcmp(code, "FOAR0001") == 0), "an evaluation error");
    check(xp3EvaluateBoolean(NULL, ctx, NULL) == -1, "no expression");
    check(xp3Evaluate(NULL, ctx, NULL) == NULL, "no expression");

done:
    xp3FreeDynamicContext(ctx);
    xmlFreeDoc(doc);
}

static void
testErrorPositions(void) {
    xp3Expr *expr = xp3Compile(NULL, BAD_CAST "(1, 2)/\n  child::x", NULL);
    xp3Error *err = NULL;

    check(xp3Evaluate(expr, NULL, &err) == NULL, "evaluation fails");
    check((err != NULL) &&
          xmlStrEqual(xp3ErrorCode(err), BAD_CAST "XPTY0019") &&
          (xp3ErrorLine(err) == 2) && (xp3ErrorColumn(err) == 3) &&
          (xp3ErrorNamespace(err) != NULL),
          "XPTY0019 at the step, line 2 column 3");
    xp3ErrorFree(err);
    err = NULL;
    check(xp3Evaluate(expr, NULL, NULL) == NULL, "no error wanted");
    xp3FreeExpr(expr);

    expr = xp3Compile(NULL, BAD_CAST "upper-case(())", NULL);
    err = NULL;
    check((xp3Evaluate(expr, NULL, &err) == NULL) && (err != NULL) &&
          xmlStrEqual(xp3ErrorCode(err), BAD_CAST "UNSUPPORTED") &&
          (xp3ErrorNamespace(err) == NULL) &&
          (strstr((const char *) xp3ErrorMessage(err), " upper-case#1 ") !=
           NULL), "UNSUPPORTED names a function without its namespace");
    xp3ErrorFree(err);
    xp3FreeExpr(expr);

    /* A conversion error is at the argument, a call's own at its name */
    expr = xp3Compile(NULL, BAD_CAST "subsequence(1,\n  'a')", NULL);
    err = NULL;
    check((xp3Evaluate(expr, NULL, &err) == NULL) && (err != NULL) &&
          xmlStrEqual(xp3ErrorCode(err), BAD_CAST "XPTY0004") &&
          (xp3ErrorLine(err) == 2) && (xp3ErrorColumn(err) == 3),
          "XPTY0004 at the argument, line 2 column 3");
    xp3ErrorFree(err);
    xp3FreeExpr(expr);
    expr = xp3Compile(NULL, BAD_CAST "1,\n  exactly-one(())", NULL);
    err = NULL;
    check((xp3Evaluate(expr, NULL, &err) == NULL) && (err != NULL) &&
          xmlStrEqual(xp3ErrorCode(err), BAD_CAST "FORG0005") &&
          (xp3ErrorLine(err) == 2) && (xp3ErrorColumn(err) == 3),
          "FORG0005 at the call, line 2 column 3");
    xp3ErrorFree(err);
    xp3FreeExpr(expr);
}

/*
 * Sequences
 */

static void
testSequences(void) {
    xmlDocPtr doc = parseDoc(xp3TestDoc);
    xp3Expr *expr = xp3Compile(NULL, BAD_CAST "(1, 2.5, 1e2, 'a', /r/@a)",
                               NULL);
    xp3Sequence *seq = NULL;
    xp3DynamicContext *ctx = xp3NewDynamicContext();
    const xmlChar *strs[2];
    xmlNodePtr list[1];
    xmlChar *str;

    if ((doc == NULL) || (expr == NULL) || (ctx == NULL)) {
        check(0, "sequence test set up");
        goto done;
    }
    xp3SetContextNode(ctx, (xmlNodePtr) doc);
    seq = xp3Evaluate(expr, ctx, NULL);
    check(xp3SequenceLength(seq) == 5, "five items");
    check(xp3SequenceItemKind(seq, 0) == XP3_ITEM_ATOMIC, "atomic kind");
    check(xp3SequenceItemKind(seq, 4) == XP3_ITEM_NODE, "node kind");
    check(xp3SequenceItemKind(seq, 5) == -1, "past the end");
    check(xp3SequenceNode(seq, 0) == NULL, "not a node");
    check(xp3SequenceNode(seq, 4) ==
          (xmlNodePtr) xmlDocGetRootElement(doc)->properties, "the node");
    check(xp3SequenceNode(seq, 9) == NULL, "no node past the end");
    str = xp3SequenceStringValue(seq, 1);
    check(xmlStrEqual(str, BAD_CAST "2.5"), "decimal string");
    xmlFree(str);
    str = xp3SequenceStringValue(seq, 2);
    check(xmlStrEqual(str, BAD_CAST "100"), "double string");
    xmlFree(str);
    str = xp3SequenceStringValue(seq, 4);
    check(xmlStrEqual(str, BAD_CAST "1"), "attribute string");
    xmlFree(str);
    check(xp3SequenceStringValue(seq, 5) == NULL, "no string past the end");
    check(xp3SequenceLength(NULL) == 0, "length of NULL");
    check(xp3SequenceItemKind(NULL, 0) == -1, "kind in NULL");

    list[0] = xmlNewReference(doc, BAD_CAST "&e;");
    check(xp3NewNodeSequence(list, 1) == NULL,
          "a node XDM doesn't have is rejected");
    xmlFreeNode(list[0]);
    check(xp3NewNodeSequence(NULL, 1) == NULL, "no nodes");
    xp3FreeSequence(xp3NewNodeSequence(NULL, 0));
    strs[0] = BAD_CAST "a";
    strs[1] = BAD_CAST "\xC3";
    check(xp3NewStringSequence(strs, 2) == NULL,
          "invalid UTF-8 is rejected");
    strs[1] = NULL;
    check(xp3NewStringSequence(strs, 2) == NULL, "no string");
    check(xp3NewStringSequence(NULL, 1) == NULL, "no strings");
    xp3FreeSequence(NULL);

done:
    xp3FreeSequence(seq);
    xp3FreeDynamicContext(ctx);
    xp3FreeExpr(expr);
    xmlFreeDoc(doc);
}

/*
 * Reading and building items, as language bindings do
 */

static xp3Sequence *
evaluate(xp3DynamicContext *ctx, const char *text) {
    xp3Expr *expr = xp3Compile(NULL, BAD_CAST text, NULL);
    xp3Sequence *seq = xp3Evaluate(expr, ctx, NULL);

    xp3FreeExpr(expr);
    return(seq);
}

/* Read an integer; value must stay unchanged unless ret is 0 */
static void
checkInteger(const xp3Sequence *seq, size_t index, int ret,
             int64_t expected, const char *what) {
    int64_t value = 7;
    int actual = xp3SequenceInteger(seq, index, &value);

    check((actual == ret) && (value == ((ret == 0) ? expected : 7)), what);
}

/* Read a double; value must stay unchanged unless ret is 0 */
static void
checkDouble(const xp3Sequence *seq, size_t index, int ret,
            double expected, const char *what) {
    double value = 7;
    int actual = xp3SequenceDouble(seq, index, &value);

    if (ret != 0)
        expected = 7;
    check((actual == ret) &&
          (isnan(expected) ? isnan(value) :
           ((value == expected) && (!signbit(value) == !signbit(expected)))),
          what);
}

/* Every reader gives -1 */
static void
checkNoValue(const xp3Sequence *seq, size_t index, const char *what) {
    check(xp3SequenceAtomicType(seq, index) == -1, what);
    checkInteger(seq, index, -1, 0, what);
    checkDouble(seq, index, -1, 0, what);
    check(xp3SequenceBoolean(seq, index) == -1, what);
}

static void
checkStringValue(const xp3Sequence *seq, size_t index, const char *expected,
                 const char *what) {
    xmlChar *str = xp3SequenceStringValue(seq, index);

    check(xmlStrEqual(str, BAD_CAST expected), what);
    xmlFree(str);
}

static void
testReadingItems(void) {
    xmlDocPtr doc = parseDoc(xp3TestDoc);
    xp3DynamicContext *ctx = xp3NewDynamicContext();
    xp3Sequence *seq = NULL;
    xp3Sequence *range = NULL;
    xp3Sequence *bigRange = NULL;
    int64_t integer;
    double number;

    if ((doc == NULL) || (ctx == NULL)) {
        check(0, "reading test set up");
        goto done;
    }
    xp3SetContextNode(ctx, xmlDocGetRootElement(doc));
    seq = evaluate(ctx, "(42, xs:int(5), (1 to 5)[3], "
                   "9223372036854775807, -9223372036854775808, "
                   "9223372036854775808, -9223372036854775809, "
                   "1000, 1000000000000000000, 1.5, 0.1, xs:float(0.1), "
                   "1e0 div 0, xs:double('NaN'), 1 eq 1, 1 eq 2, 'abc', "
                   "x[1])");
    check(xp3SequenceLength(seq) == 18, "items to read");

    check(xp3SequenceAtomicType(seq, 0) == XP3_TYPE_INTEGER, "42 type");
    checkInteger(seq, 0, 0, 42, "42 as an integer");
    checkDouble(seq, 0, 0, 42, "42 as a double");
    check(xp3SequenceBoolean(seq, 0) == -1, "42 is not a boolean");
    check(xp3SequenceAtomicType(seq, 1) == XP3_TYPE_INT, "xs:int type");
    checkInteger(seq, 1, 0, 5, "xs:int as an integer");
    checkDouble(seq, 1, 0, 5, "xs:int as a double");
    check(xp3SequenceAtomicType(seq, 2) == XP3_TYPE_INTEGER,
          "item of a range type");
    checkInteger(seq, 2, 0, 3, "item of a range as an integer");

    checkInteger(seq, 3, 0, INT64_MAX, "largest int64_t");
    checkInteger(seq, 4, 0, INT64_MIN, "smallest int64_t");
    checkInteger(seq, 5, 1, 0, "above int64_t");
    checkStringValue(seq, 5, "9223372036854775808", "above int64_t exactly");
    checkInteger(seq, 6, 1, 0, "below int64_t");
    checkStringValue(seq, 6, "-9223372036854775809",
                     "below int64_t exactly");
    checkDouble(seq, 5, 0, 9223372036854775808.0, "above int64_t as double");
    checkInteger(seq, 7, 0, 1000, "1000, trailing zeros");
    checkInteger(seq, 8, 0, INT64_C(1000000000000000000), "10^18");

    check(xp3SequenceAtomicType(seq, 9) == XP3_TYPE_DECIMAL, "decimal type");
    checkInteger(seq, 9, -1, 0, "a decimal is not an integer");
    checkDouble(seq, 9, 0, 1.5, "1.5 as a double");
    checkDouble(seq, 10, 0, 0.1, "0.1 as a double");
    check(xp3SequenceAtomicType(seq, 11) == XP3_TYPE_FLOAT, "float type");
    checkDouble(seq, 11, 0, (double) 0.1f, "xs:float(0.1) as a double");
    checkInteger(seq, 11, -1, 0, "a float is not an integer");
    checkDouble(seq, 12, 0, INFINITY, "infinity");
    checkDouble(seq, 13, 0, NAN, "NaN");

    check(xp3SequenceAtomicType(seq, 14) == XP3_TYPE_BOOLEAN,
          "boolean type");
    check(xp3SequenceBoolean(seq, 14) == 1, "true");
    check(xp3SequenceBoolean(seq, 15) == 0, "false");
    checkInteger(seq, 14, -1, 0, "a boolean is not an integer");
    checkDouble(seq, 14, -1, 0, "a boolean is not a number");

    check(xp3SequenceAtomicType(seq, 16) == XP3_TYPE_STRING, "string type");
    checkInteger(seq, 16, -1, 0, "a string is not an integer");
    checkDouble(seq, 16, -1, 0, "a string is not a number");
    check(xp3SequenceBoolean(seq, 16) == -1, "a string is not a boolean");

    check(xp3SequenceItemKind(seq, 17) == XP3_ITEM_NODE, "a node");
    checkNoValue(seq, 17, "a node is not atomic");
    checkNoValue(seq, 18, "no item past the end");
    checkNoValue(NULL, 0, "no item in NULL");
    check(xp3SequenceInteger(seq, 0, NULL) == -1, "integer to NULL");
    check(xp3SequenceDouble(seq, 0, NULL) == -1, "double to NULL");

    range = evaluate(ctx, "1 to 5");
    check((range != NULL) && (range->shape == XP3_SEQ_RANGE),
          "a range stays a range");
    check(xp3SequenceAtomicType(range, 2) == XP3_TYPE_INTEGER, "range type");
    checkInteger(range, 2, 0, 3, "range item as an integer");
    checkDouble(range, 4, 0, 5, "range item as a double");
    check(xp3SequenceBoolean(range, 0) == -1, "range item is no boolean");
    checkNoValue(range, 5, "no item past the end of a range");
    bigRange = evaluate(ctx, "9223372036854775806 to 9223372036854775807");
    check((bigRange != NULL) && (bigRange->shape == XP3_SEQ_RANGE),
          "a range up to the largest int64_t");
    checkInteger(bigRange, 1, 0, INT64_MAX, "last item of a range");

    integer = 7;
    number = 7;
    check((xp3SequenceInteger(NULL, 0, &integer) == -1) && (integer == 7) &&
          (xp3SequenceDouble(NULL, 0, &number) == -1) && (number == 7),
          "values unchanged without an item");

done:
    xp3FreeSequence(bigRange);
    xp3FreeSequence(range);
    xp3FreeSequence(seq);
    xp3FreeDynamicContext(ctx);
    xmlFreeDoc(doc);
}

static void
testBuildingSequences(void) {
    const int64_t integers[2] = { 1, INT64_MAX };
    const double doubles[3] = { 1.5, NAN, -0.0 };
    const int booleans[2] = { 0, 5 };
    const xmlChar *strs[2];
    const char *names[] = { "i", "d", "b", "s", "x" };
    xp3StaticContext *sctx = xp3NewStaticContext();
    xp3DynamicContext *ctx = xp3NewDynamicContext();
    xp3Sequence *empty[4];
    xp3Sequence *seq;
    int *many;
    size_t i;
    int ok;

    strs[0] = BAD_CAST "a";
    strs[1] = BAD_CAST "b";
    if ((sctx == NULL) || (ctx == NULL)) {
        check(0, "building test set up");
        goto done;
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        check(xp3DeclareVariable(sctx, NULL, BAD_CAST names[i]) == 0,
              "variable declared");

    seq = xp3NewIntegerSequence(integers, 2);
    check((xp3SequenceLength(seq) == 2) &&
          (xp3SequenceAtomicType(seq, 1) == XP3_TYPE_INTEGER),
          "integer sequence");
    checkInteger(seq, 1, 0, INT64_MAX, "integer read back");
    check(xp3BindVariable(ctx, NULL, BAD_CAST "i", seq) == 0, "$i bound");
    xp3FreeSequence(seq);
    checkEval(sctx, ctx, "$i", "integer:1 integer:9223372036854775807");
    checkEval(sctx, ctx, "$i[2] - 1", "integer:9223372036854775806");
    checkEval(sctx, ctx, "$i[2] + 1", "integer:9223372036854775808");
    checkEval(sctx, ctx, "$i instance of xs:integer+", "boolean:true");

    seq = xp3NewDoubleSequence(doubles, 3);
    check((xp3SequenceLength(seq) == 3) &&
          (xp3SequenceAtomicType(seq, 0) == XP3_TYPE_DOUBLE),
          "double sequence");
    checkDouble(seq, 1, 0, NAN, "NaN read back");
    checkDouble(seq, 2, 0, -0.0, "negative zero read back");
    check(xp3BindVariable(ctx, NULL, BAD_CAST "d", seq) == 0, "$d bound");
    xp3FreeSequence(seq);
    checkEval(sctx, ctx, "$d", "double:1.5 double:NaN double:-0");
    checkEval(sctx, ctx, "$d[2] ne $d[2]", "boolean:true");
    checkEval(sctx, ctx, "$d instance of xs:double+", "boolean:true");

    seq = xp3NewBooleanSequence(booleans, 2);
    check((xp3SequenceBoolean(seq, 0) == 0) &&
          (xp3SequenceBoolean(seq, 1) == 1), "boolean sequence");
    check(xp3BindVariable(ctx, NULL, BAD_CAST "b", seq) == 0, "$b bound");
    xp3FreeSequence(seq);
    checkEval(sctx, ctx, "$b", "boolean:false boolean:true");
    checkEval(sctx, ctx, "$b[2]", "boolean:true");

    seq = xp3NewStringSequence(strs, 2);
    check((xp3SequenceLength(seq) == 2) &&
          (xp3SequenceAtomicType(seq, 1) == XP3_TYPE_STRING),
          "string sequence");
    check(xp3BindVariable(ctx, NULL, BAD_CAST "s", seq) == 0, "$s bound");
    xp3FreeSequence(seq);
    checkEval(sctx, ctx, "$s", "string:a string:b");
    checkEval(sctx, ctx, "$s[2]", "string:b");

    empty[0] = xp3NewIntegerSequence(NULL, 0);
    empty[1] = xp3NewDoubleSequence(NULL, 0);
    empty[2] = xp3NewBooleanSequence(NULL, 0);
    empty[3] = xp3NewStringSequence(NULL, 0);
    for (i = 0; i < 4; i++) {
        check((empty[i] != NULL) && (xp3SequenceLength(empty[i]) == 0),
              "no values give the empty sequence");
        check(xp3BindVariable(ctx, NULL, BAD_CAST "x", empty[i]) == 0,
              "$x bound");
        checkEval(sctx, ctx, "$x instance of empty-sequence()",
                  "boolean:true");
        xp3FreeSequence(empty[i]);
    }

    check(xp3NewIntegerSequence(NULL, 1) == NULL, "no integers");
    check(xp3NewDoubleSequence(NULL, 1) == NULL, "no doubles");
    check(xp3NewBooleanSequence(NULL, 1) == NULL, "no booleans");
    check(xp3NewStringSequence(NULL, 1) == NULL, "no strings");

    many = calloc(XP3_MAX_SEQUENCE_ITEMS + 1, sizeof(*many));
    if (many != NULL) {
        seq = xp3NewBooleanSequence(many, XP3_MAX_SEQUENCE_ITEMS + 1);
        ok = (seq == NULL);
        xp3FreeSequence(seq);
        check(ok, "more than 10 million values are rejected");
        free(many);
    }

done:
    xp3FreeDynamicContext(ctx);
    xp3FreeStaticContext(sctx);
}

/*
 * Build every kind of sequence and read a decimal as a double, which
 * allocates. Returns 1 if all of it succeeded.
 */
static int
bindingWork(const xp3Sequence *decimal) {
    const int64_t integers[3] = { 1, -2, INT64_MAX };
    const double doubles[3] = { 1.5, NAN, -0.0 };
    const int booleans[3] = { 0, 1, 2 };
    const xmlChar *strs[3];
    xp3Sequence *seqs[4];
    double number = 0;
    size_t i;
    int ok;

    strs[0] = BAD_CAST "a";
    strs[1] = BAD_CAST "b";
    strs[2] = BAD_CAST "c";
    seqs[0] = xp3NewIntegerSequence(integers, 3);
    seqs[1] = xp3NewDoubleSequence(doubles, 3);
    seqs[2] = xp3NewBooleanSequence(booleans, 3);
    seqs[3] = xp3NewStringSequence(strs, 3);
    ok = (xp3SequenceDouble(decimal, 0, &number) == 0) && (number == 1.5);
    for (i = 0; i < 4; i++) {
        if (xp3SequenceLength(seqs[i]) != 3)
            ok = 0;
        xp3FreeSequence(seqs[i]);
    }
    return(ok);
}

static void
testBindingAllocationFailures(void) {
    xp3Sequence *decimal = evaluate(NULL, "1.5");
    int i;

    check(bindingWork(decimal), "binding work without failures");
    if (!xp3TestAllocInstall()) {
        printf("testeval: binding allocation-failure tests skipped: this "
               "libxml2 ignores xmlMemSetup()\n");
        xp3TestAllocRestore();
        xp3FreeSequence(decimal);
        return;
    }
    for (i = 0; i < 1000; i++) {
        int injected;
        int ok;

        xp3TestAllocFailAt(i);
        ok = bindingWork(decimal);
        injected = xp3TestAllocFailed();
        xp3TestAllocFailAt(-1);
        if (!injected) {
            check(ok, "binding work complete after allocation failures");
            break;
        }
        check(!ok, "a failed allocation fails the binding work");
    }
    check(i > 0 && i < 1000, "binding allocation failure points exercised");
    xp3TestAllocRestore();
    xp3FreeSequence(decimal);
}

/*
 * Random paths, optimized and not
 *
 * Constant folding, the sorted flags and the //x rewrite must never
 * change a result. Random documents and random path expressions from a
 * small grammar are evaluated with and without optimizing, and the
 * descriptions of the results, or the error codes, must be the same.
 */

#define XP3_RANDOM_DOCS 300
#define XP3_RANDOM_EXPRS 100

/*
 * The random numbers come from a linear congruential generator of our
 * own, not rand(), whose numbers differ between C libraries: the same
 * seed gives the same documents and expressions on every platform, so
 * a failure in CI can be reproduced anywhere. The multiplier and
 * increment are those of Knuth's MMIX (The Art of Computer
 * Programming, vol. 2, 3.3.4). With a modulus of 2^64, an odd
 * increment and a multiplier that is 1 modulo 4, the generator runs
 * through all 2^64 states before repeating (the Hull-Dobell theorem).
 * The low bits of such a generator repeat with short periods, so
 * pathRandom() uses the high ones. Each document starts from its own
 * seed, its index plus one.
 */
static uint64_t pathRandomState;

static unsigned
pathRandom(unsigned limit) {
    pathRandomState = pathRandomState * UINT64_C(6364136223846793005) +
                      UINT64_C(1442695040888963407);
    return((unsigned) (pathRandomState >> 33) % limit);
}

/* Two names, so that elements of one name often nest */
static const char *const xp3PathNames[] = { "a", "b" };

/* Append to a buffer; on overflow the buffer stays as it was */
static void
appendTo(char *out, size_t size, const char *text) {
    size_t used = strlen(out);

    if (used + strlen(text) < size)
        memcpy(out + used, text, strlen(text) + 1);
}

/* A random element with attributes, text, comments and children */
static void
randomElement(char *out, size_t size, int depth) {
    const char *name = xp3PathNames[pathRandom(2)];
    unsigned children = (depth < 2) ? 2 + pathRandom(3) :
                        (depth < 5) ? pathRandom(4) : 0;
    unsigned i;

    appendTo(out, size, "<");
    appendTo(out, size, name);
    if (pathRandom(2) == 0)
        appendTo(out, size, (pathRandom(2) == 0) ? " id='i1'" : " id='i2'");
    if (pathRandom(3) == 0)
        appendTo(out, size, " k='v'");
    appendTo(out, size, ">");
    for (i = 0; i < children; i++) {
        switch (pathRandom(8)) {
            case 0:
                appendTo(out, size, "x");
                break;
            case 1:
                appendTo(out, size, "<!--c-->");
                break;
            case 2:
                appendTo(out, size, "<?p d?>");
                break;
            default:
                randomElement(out, size, depth + 1);
                break;
        }
    }
    appendTo(out, size, "</");
    appendTo(out, size, name);
    appendTo(out, size, ">");
}

/* Axes for elements and other children, weighted toward those whose
 * order after several nodes is hard: siblings and descendants. The
 * attribute axis has tests of its own. */
static const char *const xp3PathAxes[] = {
    "child::", "descendant::", "descendant-or-self::", "self::",
    "parent::", "ancestor::", "ancestor-or-self::", "following-sibling::",
    "preceding-sibling::", "following::", "preceding::", "", "", "",
    "following-sibling::", "descendant::", "preceding-sibling::"
};

#define XP3_PATH_AXES (sizeof(xp3PathAxes) / sizeof(xp3PathAxes[0]))

/* Tests, weighted toward those that match many nodes */
static const char *const xp3PathTests[] = {
    "a", "b", "*", "*", "node()", "node()", "a", "b", "text()", "comment()"
};

#define XP3_PATH_TESTS (sizeof(xp3PathTests) / sizeof(xp3PathTests[0]))

static const char *const xp3PathAttributes[] = { "@id", "@k", "@*" };

/* Predicates that depend on position, that don't, and constant ones,
 * among them constant ranges and constant operands of and and or */
static const char *const xp3PathPredicates[] = {
    "[1]", "[2]", "[@id]", "[@id = 'i1']", "[a]", "[. = 'x']",
    "[@k = 'v' or b]", "[1 = 1]", "[1 + 1]", "[(1, 2)]",
    "[. instance of element()]", "[*[1]]", "[3 - 2]", "[0]",
    "[if (1) then 1 else 2]", "[1 = 2 and a]", "[1 = 1 or a]",
    "[a and 0]", "[a or 1]", "[if (0 to 0) then 1 else 2]",
    "[if (1 to 1) then a else b]", "[(0 to 0) or b]", "[(1 to 1) and b]",
    "[a and (1 to 1)]", "[b or (0 to 0)]"
};

#define XP3_PATH_PREDICATES \
    (sizeof(xp3PathPredicates) / sizeof(xp3PathPredicates[0]))

/* A step: mostly an element axis, sometimes . or .. or an attribute,
 * with no predicate half of the time and rarely two */
static void
randomStep(char *out, size_t size, int last) {
    unsigned choice = pathRandom(12);
    unsigned predicates = (choice < 8) ? 0 : (choice < 11) ? 1 : 2;
    unsigned i;

    switch (pathRandom(14)) {
        case 0:
            appendTo(out, size, ".");
            return;
        case 1:
            appendTo(out, size, "..");
            return;
        case 2:
            /* Attributes have no children: only at the end */
            if (last) {
                appendTo(out, size, xp3PathAttributes[pathRandom(3)]);
                break;
            }
            /* Fall through */
        default:
            appendTo(out, size, xp3PathAxes[pathRandom(XP3_PATH_AXES)]);
            appendTo(out, size, xp3PathTests[pathRandom(XP3_PATH_TESTS)]);
            break;
    }
    for (i = 0; i < predicates; i++)
        appendTo(out, size,
                 xp3PathPredicates[pathRandom(XP3_PATH_PREDICATES)]);
}

static void
randomPath(char *out, size_t size) {
    unsigned steps = 1 + pathRandom(3);
    unsigned i;

    switch (pathRandom(4)) {
        case 0:
            appendTo(out, size, "/");
            break;
        case 1:
            appendTo(out, size, "//");
            break;
        default:
            break;
    }
    for (i = 0; i < steps; i++) {
        if (i > 0)
            appendTo(out, size, (pathRandom(3) == 0) ? "//" : "/");
        randomStep(out, size, i + 1 == steps);
    }
}

/* Two steps that each give several nodes, then a sibling or
 * descendant step: where wrongly skipping the sort shows */
static const char *const xp3PathSiblingSteps[] = {
    "following-sibling::", "preceding-sibling::", "descendant::", "child::",
    "following-sibling::", "descendant-or-self::"
};

static void
randomSiblingPath(char *out, size_t size) {
    unsigned i;

    appendTo(out, size, (pathRandom(2) == 0) ? "*" : "//*");
    for (i = 0; i < 2; i++) {
        appendTo(out, size, "/");
        appendTo(out, size, xp3PathSiblingSteps[pathRandom(6)]);
        appendTo(out, size, (pathRandom(2) == 0) ? "*" : "node()");
    }
}

static void
randomPathExpr(char *out, size_t size) {
    out[0] = 0;
    if (pathRandom(5) == 0) {
        randomSiblingPath(out, size);
        return;
    }
    switch (pathRandom(6)) {
        case 0:
            randomPath(out, size);
            appendTo(out, size, " | ");
            randomPath(out, size);
            break;
        case 1:
            appendTo(out, size, "(");
            randomPath(out, size);
            appendTo(out, size, ")");
            appendTo(out, size,
                 xp3PathPredicates[pathRandom(XP3_PATH_PREDICATES)]);
            break;
        case 2:
            randomPath(out, size);
            appendTo(out, size, "/(");
            randomPath(out, size);
            appendTo(out, size, ")");
            break;
        default:
            randomPath(out, size);
            break;
    }
}

/*
 * Number the nodes of a document in document order, in their _private
 * fields, so that results show which nodes they hold and in what
 * order, not just their names. Only the test's own documents are
 * numbered; the library never uses _private.
 */
static void
numberNodes(xmlDocPtr doc) {
    xmlNodePtr root = (xmlNodePtr) doc;
    xmlNodePtr node = NULL;
    intptr_t number = 0;

    while ((node = xp3AxisNext(XP3_AXIS_DESCENDANT_OR_SELF, root, node)) !=
           NULL) {
        xmlNodePtr attr = NULL;

        node->_private = (void *) ++number;
        while ((attr = xp3AxisNext(XP3_AXIS_ATTRIBUTE, node, attr)) != NULL)
            attr->_private = (void *) ++number;
    }
}

/* A result as node numbers and atomic values, or the error code */
static void
runNumbered(const xp3StaticContext *sctx, xp3DynamicContext *ctx,
            const char *expr, char *out, size_t size) {
    xp3Error *err = NULL;
    xp3Expr *compiled = xp3Compile(sctx, BAD_CAST expr, &err);
    xp3Sequence *result = NULL;
    size_t i;

    out[0] = 0;
    if (compiled != NULL)
        result = xp3Evaluate(compiled, ctx, &err);
    if (result == NULL) {
        snprintf(out, size, "error:%s", (const char *) xp3ErrorCode(err));
    } else {
        for (i = 0; i < xp3SequenceLength(result); i++) {
            xmlNodePtr node = xp3SequenceNode(result, i);
            char item[64];

            if (node != NULL) {
                snprintf(item, sizeof(item), " #%ld",
                         (long) (intptr_t) node->_private);
            } else {
                xmlChar *str = xp3SequenceStringValue(result, i);

                snprintf(item, sizeof(item), " %s",
                         (str != NULL) ? (const char *) str : "?");
                xmlFree(str);
            }
            appendTo(out, size, item);
        }
    }
    xp3FreeSequence(result);
    xp3FreeExpr(compiled);
    xp3ErrorFree(err);
}

/* A random element of a document, as another context node */
static xmlNodePtr
randomContext(xmlDocPtr doc) {
    xmlNodePtr root = (xmlNodePtr) doc;
    xmlNodePtr node = NULL;
    xmlNodePtr chosen = xmlDocGetRootElement(doc);

    while ((node = xp3AxisNext(XP3_AXIS_DESCENDANT, root, node)) != NULL) {
        if ((node->type == XML_ELEMENT_NODE) && (pathRandom(4) == 0))
            chosen = node;
    }
    return(chosen);
}

static void
testRandomPaths(void) {
    xp3StaticContext *plain = xp3NewStaticContext();
    xp3DynamicContext *ctx = xp3NewDynamicContext();
    int docIndex;
    int compared = 0;
    int withNodes = 0;

    if ((plain == NULL) || (ctx == NULL)) {
        check(0, "random path contexts");
        goto done;
    }
    plain->noOptimize = 1;
    for (docIndex = 0; (docIndex < XP3_RANDOM_DOCS) && (failures < 20);
         docIndex++) {
        char xml[16384];
        xmlDocPtr doc;
        int exprIndex;

        pathRandomState = (uint64_t) docIndex + 1;
        xml[0] = 0;
        randomElement(xml, sizeof(xml), 0);
        doc = xmlReadMemory(xml, (int) strlen(xml), "random.xml", NULL, 0);
        if (doc == NULL) {
            check(0, "random document parses");
            continue;
        }
        numberNodes(doc);
        for (exprIndex = 0; exprIndex < XP3_RANDOM_EXPRS; exprIndex++) {
            char expr[512];
            char optimized[4096];
            char unoptimized[4096];
            xmlNodePtr context;

            switch (exprIndex % 3) {
                case 0:
                    context = (xmlNodePtr) doc;
                    break;
                case 1:
                    context = xmlDocGetRootElement(doc);
                    break;
                default:
                    context = randomContext(doc);
                    break;
            }
            randomPathExpr(expr, sizeof(expr));
            xp3SetContextNode(ctx, context);
            runNumbered(NULL, ctx, expr, optimized, sizeof(optimized));
            runNumbered(plain, ctx, expr, unoptimized, sizeof(unoptimized));
            compared++;
            if (strchr(unoptimized, '#') != NULL)
                withNodes++;
            if (strcmp(optimized, unoptimized) != 0) {
                fprintf(stderr, "FAIL: optimizing changes %s on %s\n"
                        "  optimized:   %s\n  unoptimized: %s\n", expr, xml,
                        optimized, unoptimized);
                failures++;
            }
        }
        xmlFreeDoc(doc);
    }
    check(compared == XP3_RANDOM_DOCS * XP3_RANDOM_EXPRS,
          "every random path compared");
    /*
     * Empty results always agree, so the generator must keep finding
     * nodes. About a third do: many of the rest are empty by nature,
     * such as ancestor:: from the document node. Fewer than a quarter
     * means the generator has drifted and tests little.
     */
    if (withNodes * 4 < compared) {
        fprintf(stderr, "FAIL: only %d of %d random paths find nodes\n",
                withNodes, compared);
        failures++;
    }

done:
    xp3FreeDynamicContext(ctx);
    xp3FreeStaticContext(plain);
}

/*
 * Allocation failures
 */

static const char *const xp3AllocExpr =
    "for $a in //x, $b in ($a/@id, $a/ancestor::*[1]) "
    "return ($b, (1, 'a', 2.5)[2], $v, (x[2], x[1])/., "
    "let $c := 1e0 return $c, some $d in (0, 1) satisfies $d)";

static const char *const xp3AllocExpected =
    "a:id string:a e:z e:x#x1 e:x#x1 e:x#x2 double:1 boolean:true "
    "e:r string:a e:z e:x#x1 e:x#x1 e:x#x2 double:1 boolean:true "
    "a:id string:a e:z e:x#x1 e:x#x1 e:x#x2 double:1 boolean:true "
    "e:r string:a e:z e:x#x1 e:x#x1 e:x#x2 double:1 boolean:true "
    "a:id string:a e:z e:x#x1 e:x#x1 e:x#x2 double:1 boolean:true "
    "e:z string:a e:z e:x#x1 e:x#x1 e:x#x2 double:1 boolean:true";

/*
 * Set up contexts, compile and evaluate. Returns the result, or NULL
 * with *code set to the error code.
 */
static const char *const xp3AllocOperators =
    "(1 + 2.5, 7 idiv 2, 1e0 div 4, 99999999999 * 99999999999, -1.5, "
    "'12' cast as xs:integer, 0.1e0 cast as xs:decimal, "
    "1.5e20 cast as xs:integer, 'TQ==' cast as xs:base64Binary, "
    "' a b ' cast as xs:NMTOKENS, '1' cast as xs:numeric, "
    "'x' castable as xs:integer, x[1] << x[2], x[1] is x[1], "
    "(x | z) except z, x intersect x[2], @a = 1, @a eq '1', 1 to 2, "
    "18446744073709551616 to 18446744073709551617, "
    "x instance of element()+, 1 treat as xs:integer)";

static const char *const xp3AllocOperatorsExpected =
    "decimal:3.5 integer:3 double:0.25 integer:9999999999800000000001 "
    "decimal:-1.5 integer:12 decimal:0.1 integer:150000000000000000000 "
    "base64Binary:TQ== NMTOKEN:a NMTOKEN:b double:1 boolean:false "
    "boolean:true boolean:true e:x#x1 e:x#x2 e:x#x2 boolean:true "
    "boolean:true integer:1 integer:2 integer:18446744073709551616 "
    "integer:18446744073709551617 boolean:true integer:1";

/* Function calls, the conversion rules, ||, ! and arrows */
static const char *const xp3AllocFunctions =
    "(count(x), exists(()), not(x), boolean('a'), string(x[1]), data(@a), "
    "number('1.5'), head(x), tail((1, 2, 3)), reverse(x), "
    "subsequence((1, 'a', 2), 2), remove((1, 2, 3), 2), "
    "insert-before((1, 2), 2, 'b'), exactly-one(1), zero-or-one(()), "
    "one-or-more(x)[1], x ! position(), 'a' || 1, x => count(), "
    "(1 to 3)[position() = last()], subsequence(x, @a), string(), "
    "number(), count(1 to 10000000000), subsequence(1 to 9, 8), "
    "remove($v, 1), reverse(1 to 2))";

static const char *const xp3AllocFunctionsExpected =
    "integer:2 boolean:false boolean:false boolean:true string:one "
    "untypedAtomic:1 double:1.5 e:x#x1 integer:2 integer:3 e:x#x2 e:x#x1 "
    "string:a integer:2 integer:1 integer:3 integer:1 string:b integer:2 "
    "integer:1 e:x#x1 integer:1 integer:2 string:a1 integer:2 integer:3 "
    "e:x#x1 e:x#x2 string:onetwotail double:NaN integer:10000000000 "
    "integer:8 integer:9 e:x#x1 integer:2 integer:1";

/* The aggregate functions */
static const char *const xp3AllocAggregate =
    "(sum((1, 2.5, @a)), sum((), 'z'), sum(1 to 10000000000), "
    "avg((1, 2)), avg(-3 to 4), min((3, xs:float(2))), max((@a, 0)), "
    "min(1 to 5), max((true(), false())), sum((1, 2), ()))";

static const char *const xp3AllocAggregateExpected =
    "double:4.5 string:z integer:50000000005000000000 decimal:1.5 "
    "decimal:0.5 float:2 double:1 integer:1 boolean:true integer:3";

/* The functions on numeric values */
static const char *const xp3AllocNumeric =
    "(abs(-1.5), abs(xs:byte(-2)), abs(@a), ceiling(-0.5e0), floor(1.5), "
    "round(2.5), round(1234, -2), round(35.425e0, 2), "
    "round(xs:float('0.05'), 1), round-half-to-even(2.5), "
    "round-half-to-even(12350e0, -2), round(-0.04e0, 1))";

static const char *const xp3AllocNumericExpected =
    "decimal:1.5 integer:2 double:1 double:-0 decimal:1 decimal:3 "
    "integer:1200 double:35.42 float:0.1 decimal:2 double:12400 double:-0";

/* The math functions, with the exponents of math:pow that allocate */
static const char *const xp3AllocMath =
    "(math:pi(), math:sqrt(4), math:pow(-1, 99999999999999999999), "
    "math:pow(4, 0.5), math:pow(2, 3), math:log(@a), math:exp10(()), "
    "math:atan2(0, -1))";

static const char *const xp3AllocMathExpected =
    "double:3.141592653589793 double:2 double:-1 double:2 double:8 "
    "double:0 double:3.141592653589793";

static xp3Sequence *
allocatingWork(xmlDocPtr doc, const char *text, char *code, size_t size) {
    xp3StaticContext *sctx = xp3NewStaticContext();
    xp3DynamicContext *ctx = xp3NewDynamicContext();
    xp3Sequence *bound = NULL;
    xp3Sequence *result = NULL;
    xp3Expr *expr = NULL;
    xp3Error *err = NULL;
    xmlNodePtr list[2];

    snprintf(code, size, "setup");
    list[0] = xmlDocGetRootElement(doc)->last;
    list[1] = xmlDocGetRootElement(doc)->children;
    if ((sctx == NULL) || (ctx == NULL) ||
        (xp3DeclareVariable(sctx, NULL, BAD_CAST "v") < 0) ||
        (xp3SetContextNode(ctx, xmlDocGetRootElement(doc)) < 0))
        goto done;
    bound = xp3NewNodeSequence(list, 2);
    if ((bound == NULL) || (xp3BindVariable(ctx, NULL, BAD_CAST "v",
                                            bound) < 0))
        goto done;
    expr = xp3Compile(sctx, BAD_CAST text, &err);
    if (expr != NULL)
        result = xp3Evaluate(expr, ctx, &err);
    snprintf(code, size, "%s", (err != NULL) ?
             (const char *) xp3ErrorCode(err) : "");

done:
    xp3ErrorFree(err);
    xp3FreeExpr(expr);
    xp3FreeSequence(bound);
    xp3FreeDynamicContext(ctx);
    xp3FreeStaticContext(sctx);
    return(result);
}

static void
testAllocationFailures(const char *text, const char *expected) {
    xmlDocPtr doc = parseDoc(xp3TestDoc);
    char actual[2048];
    char code[64];
    xp3Sequence *result;
    int i;

    if (doc == NULL)
        return;
    result = allocatingWork(doc, text, code, sizeof(code));
    describe(result, actual, sizeof(actual));
    check((result != NULL) && (strcmp(actual, expected) == 0),
          "reference evaluation");
    if (strcmp(actual, expected) != 0)
        fprintf(stderr, "  actual: %s (%s)\n", actual, code);
    xp3FreeSequence(result);

    if (!xp3TestAllocInstall()) {
        printf("testeval: allocation-failure tests skipped: this "
               "libxml2 ignores xmlMemSetup()\n");
        xp3TestAllocRestore();
        xmlFreeDoc(doc);
        return;
    }
    for (i = 0; i < 5000; i++) {
        int injected;

        xp3TestAllocFailAt(i);
        result = allocatingWork(doc, text, code, sizeof(code));
        injected = xp3TestAllocFailed();
        xp3TestAllocFailAt(-1);

        describe(result, actual, sizeof(actual));
        if (!injected) {
            check((result != NULL) && (strcmp(actual, expected) == 0),
                  "evaluation complete after allocation failures");
            xp3FreeSequence(result);
            break;
        }
        /* A failed allocation is never hidden: no result, NO_MEMORY */
        check(result == NULL, "a failed allocation fails the work");
        check((strcmp(code, "NO_MEMORY") == 0) ||
              (strcmp(code, "setup") == 0),
              "a failed allocation reports NO_MEMORY");
        xp3FreeSequence(result);
    }
    check(i > 0 && i < 5000, "allocation failure points exercised");
    xp3TestAllocRestore();
    xmlFreeDoc(doc);
}

/*
 * Numbers longer than the buffer for parsing them with a decimal comma:
 * a decimal promoted to a double, a cast to xs:float and a double
 * literal. Their last digit decides the rounding.
 */
static const char *const xp3LocaleExpr =
    "(xs:decimal('2.5000000000000000000000000000000000000000000000000000"
    "0000000000000001') * 1e0, "
    "xs:float('16777217.000000000000000000000000000000000000000000000000"
    "00000000001'), "
    "9007199254740993.000000000000000000000000000000000000000000000000000"
    "1e0)";

static const char *const xp3LocaleExpected =
    "double:2.5 float:1.6777218E7 double:9.007199254740994E15";

/* The same results under a locale with a decimal comma */
static void
testLocale(void) {
    const char *names[] = { "de_DE.UTF-8", "de_DE.utf8", "de_DE",
                            "fr_FR.UTF-8", "fr_FR.utf8" };
    size_t i;

    checkEval(NULL, NULL, xp3LocaleExpr, xp3LocaleExpected);
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (setlocale(LC_NUMERIC, names[i]) != NULL)
            break;
    }
    if (i == sizeof(names) / sizeof(names[0]) ||
        strcmp(localeconv()->decimal_point, ",") != 0) {
        printf("testeval: locale test skipped: no locale with a decimal "
               "comma\n");
        setlocale(LC_NUMERIC, "C");
        return;
    }
    checkEval(NULL, NULL, xp3LocaleExpr, xp3LocaleExpected);
    testAllocationFailures(xp3LocaleExpr, xp3LocaleExpected);
    setlocale(LC_NUMERIC, "C");
}

int
main(void) {
    xmlInitParser();

    testCases();
    testConstants();
    testRandomPaths();
    testEffectiveBoolean();
    testHugeLiteral();
    testHugeNumbers();
    testQNameComparison();
    testManyBindings();
    testFocus();
    testFunctionFocus();
    testAggregateLevels();
    testUserErrors();
    testTreeChange();
    testInvalidUtf8();
    testVariables();
    testLimits();
    testBoolean();
    testErrorPositions();
    testSequences();
    testReadingItems();
    testBuildingSequences();
    testBindingAllocationFailures();
    testAllocationFailures(xp3AllocExpr, xp3AllocExpected);
    testAllocationFailures(xp3AllocOperators, xp3AllocOperatorsExpected);
    testAllocationFailures(xp3AllocFunctions, xp3AllocFunctionsExpected);
    testAllocationFailures(xp3AllocNumeric, xp3AllocNumericExpected);
    testAllocationFailures(xp3AllocMath, xp3AllocMathExpected);
    testAllocationFailures(xp3AllocAggregate, xp3AllocAggregateExpected);
    testLocale();

    if (failures == 0)
        printf("testeval: OK\n");
    return(failures == 0 ? 0 : 1);
}
