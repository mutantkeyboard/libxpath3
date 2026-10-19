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

#include <stdio.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

#include <libxpath3/xpath3.h>
#include "private/xp3decimal.h"
#include "private/xp3double.h"
#include "private/xp3eval.h"
#include "private/xp3nodes.h"
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
    { "descendant-or-self::*[@id = 'x3']", "error:UNSUPPORTED" },

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

    /* Not implemented yet */
    { "1 + 2", "error:UNSUPPORTED" },
    { "-1", "error:UNSUPPORTED" },
    { "count(x)", "error:UNSUPPORTED" },
    { "xs:integer('1')", "error:UNSUPPORTED" },
    { "x ! 1", "error:UNSUPPORTED" },
    { "map { 1: 2 }", "error:UNSUPPORTED" },
    { "(1)?*", "error:UNSUPPORTED" },
    { "(1)(2)", "error:UNSUPPORTED" },
    { "1 instance of xs:integer", "error:UNSUPPORTED" },
    { "for $a in x return $a + 1", "error:UNSUPPORTED" },
};

static xmlDocPtr
parseDoc(const char *xml) {
    xmlDocPtr doc = xmlReadMemory(xml, (int) strlen(xml), "test.xml", NULL,
                                  XML_PARSE_NOENT);

    check(doc != NULL, "test document parsed");
    return(doc);
}

static void
testCases(void) {
    xmlDocPtr doc = parseDoc(xp3TestDoc);
    xp3DynamicContext *ctx = xp3NewDynamicContext();
    size_t i;

    if ((doc == NULL) || (ctx == NULL) ||
        (xp3SetContextNode(ctx, xmlDocGetRootElement(doc)) < 0)) {
        check(0, "dynamic context set up");
    } else {
        for (i = 0; i < sizeof(xp3Cases) / sizeof(xp3Cases[0]); i++)
            checkEval(NULL, ctx, xp3Cases[i].expr, xp3Cases[i].expected);
    }
    xp3FreeDynamicContext(ctx);
    xmlFreeDoc(doc);
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
    checkEval(NULL, ctx, "(1, 2, 3)[2]", "error:LIMIT");
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
    check((evalBoolean(ctx, "1 + 1", &code) == -1) &&
          (strcmp(code, "UNSUPPORTED") == 0), "an evaluation error");
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

    expr = xp3Compile(NULL, BAD_CAST "1 +\n2", NULL);
    check((xp3Evaluate(expr, NULL, &err) == NULL) && (err != NULL) &&
          xmlStrEqual(xp3ErrorCode(err), BAD_CAST "UNSUPPORTED") &&
          (xp3ErrorNamespace(err) == NULL) &&
          (xp3ErrorMessage(err) != NULL) &&
          (strstr((const char *) xp3ErrorMessage(err), "+") != NULL),
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
static xp3Sequence *
allocatingWork(xmlDocPtr doc, char *code, size_t size) {
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
    expr = xp3Compile(sctx, BAD_CAST xp3AllocExpr, &err);
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
testAllocationFailures(void) {
    xmlDocPtr doc = parseDoc(xp3TestDoc);
    char actual[2048];
    char code[64];
    xp3Sequence *result;
    int i;

    if (doc == NULL)
        return;
    result = allocatingWork(doc, code, sizeof(code));
    describe(result, actual, sizeof(actual));
    check((result != NULL) && (strcmp(actual, xp3AllocExpected) == 0),
          "reference evaluation");
    if (strcmp(actual, xp3AllocExpected) != 0)
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
        result = allocatingWork(doc, code, sizeof(code));
        injected = xp3TestAllocFailed();
        xp3TestAllocFailAt(-1);

        describe(result, actual, sizeof(actual));
        if (!injected) {
            check((result != NULL) &&
                  (strcmp(actual, xp3AllocExpected) == 0),
                  "evaluation complete after allocation failures");
            xp3FreeSequence(result);
            break;
        }
        /* Either a clean failure or, if the failure was absorbed, the
         * right result */
        if (result != NULL)
            check(strcmp(actual, xp3AllocExpected) == 0,
                  "a result despite a failed allocation is right");
        else
            check((strcmp(code, "NO_MEMORY") == 0) ||
                  (strcmp(code, "setup") == 0),
                  "a failed allocation reports NO_MEMORY");
        xp3FreeSequence(result);
    }
    check(i > 0 && i < 5000, "allocation failure points exercised");
    xp3TestAllocRestore();
    xmlFreeDoc(doc);
}

int
main(void) {
    xmlInitParser();

    testCases();
    testEffectiveBoolean();
    testHugeLiteral();
    testManyBindings();
    testFocus();
    testVariables();
    testLimits();
    testBoolean();
    testErrorPositions();
    testSequences();
    testAllocationFailures();

    if (failures == 0)
        printf("testeval: OK\n");
    return(failures == 0 ? 0 : 1);
}
