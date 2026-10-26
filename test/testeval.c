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
#include <stdint.h>
#include <stdio.h>
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
    { "1e300 idiv 1e-10", "error:FOAR0002" },
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
    { "+xs:byte(1)", "byte:1" },
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

    /* Not implemented yet */
    { "count(x)", "error:UNSUPPORTED" },
    { "'a' || 'b'", "error:UNSUPPORTED" },
    { "x ! 1", "error:UNSUPPORTED" },
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
    xp3Sequence *nodes = NULL;
    xp3Sequence *text = xp3NewStringSequence(BAD_CAST "hello");
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

    expr = xp3Compile(NULL, BAD_CAST "1 ||\n2", NULL);
    check((xp3Evaluate(expr, NULL, &err) == NULL) && (err != NULL) &&
          xmlStrEqual(xp3ErrorCode(err), BAD_CAST "UNSUPPORTED") &&
          (xp3ErrorNamespace(err) == NULL) &&
          (xp3ErrorMessage(err) != NULL) &&
          (strstr((const char *) xp3ErrorMessage(err), "||") != NULL),
          "UNSUPPORTED names the operator, without namespace");
    xp3ErrorFree(err);
    xp3FreeExpr(expr);

    expr = xp3Compile(NULL, BAD_CAST "count(())", NULL);
    err = NULL;
    check((xp3Evaluate(expr, NULL, &err) == NULL) && (err != NULL) &&
          (strstr((const char *) xp3ErrorMessage(err), " count#1 ") !=
           NULL), "UNSUPPORTED names a function without its namespace");
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
    check(xp3NewStringSequence(BAD_CAST "\xC3") == NULL,
          "invalid UTF-8 is rejected");
    check(xp3NewStringSequence(NULL) == NULL, "no string");
    xp3FreeSequence(NULL);

done:
    xp3FreeSequence(seq);
    xp3FreeDynamicContext(ctx);
    xp3FreeExpr(expr);
    xmlFreeDoc(doc);
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
    testInvalidUtf8();
    testVariables();
    testLimits();
    testBoolean();
    testErrorPositions();
    testSequences();
    testAllocationFailures(xp3AllocExpr, xp3AllocExpected);
    testAllocationFailures(xp3AllocOperators, xp3AllocOperatorsExpected);
    testLocale();

    if (failures == 0)
        printf("testeval: OK\n");
    return(failures == 0 ? 0 : 1);
}
