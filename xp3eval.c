/*
 * xp3eval.c: the dynamic context and evaluating compiled expressions
 *
 * The evaluator walks the analysed tree (doc/DESIGN.md 3 and 7.1). Every
 * expression evaluates eagerly to a sequence with one reference, or to
 * NULL with the first error recorded. Variables live in an array of
 * slots that static analysis numbered, and the focus (XPath 3.1, 2.1.2)
 * is kept in the evaluation state and saved around the expressions
 * that change it: path steps, predicates and filters.
 *
 * Evaluation covers XPath 3.1 sections 3.1 (primary expressions), 3.2.1
 * (filters), 3.3 (paths), 3.4 (sequences, without the operators), 3.8
 * (and, or), 3.9 (for), 3.10 (let), 3.12 (if) and 3.13 (quantified
 * expressions). The rest raises UNSUPPORTED: operators come with Part
 * 7c, function calls with Part 8, and maps, arrays and function items
 * with Part 12.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libxml/tree.h>
#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

#include "private/xp3ast.h"
#include "private/xp3context.h"
#include "private/xp3decimal.h"
#include "private/xp3double.h"
#include "private/xp3eval.h"
#include "private/xp3nodes.h"
#include "private/xp3ops.h"
#include "private/xp3static.h"
#include "private/xp3types.h"

/* Room for a name in a message, longer ones are cut */
#define XP3_NAME_BUF 200

/*
 * Dynamic context
 */

/**
 * Create a dynamic context: no context item, no variables, and the
 * default limits.
 *
 * @returns the context, to be freed with xp3FreeDynamicContext(), or
 *          NULL if memory allocation failed
 */
xp3DynamicContext *
xp3NewDynamicContext(void) {
    xp3DynamicContext *ctx = xmlMalloc(sizeof(*ctx));

    if (ctx == NULL)
        return(NULL);
    memset(ctx, 0, sizeof(*ctx));
    ctx->maxSequenceLength = XP3_MAX_SEQUENCE_ITEMS;
    return(ctx);
}

/**
 * Free a dynamic context and release the values bound in it. NULL is
 * ignored.
 *
 * @param ctx  the context
 */
void
xp3FreeDynamicContext(xp3DynamicContext *ctx) {
    int i;

    if (ctx == NULL)
        return;
    for (i = 0; i < ctx->variableCount; i++) {
        xmlFree(ctx->variables[i].uri);
        xmlFree(ctx->variables[i].localName);
        xp3ValueUnref(ctx->variables[i].value);
    }
    xmlFree(ctx->variables);
    xmlFree(ctx);
}

/**
 * Set the context node, which "." and relative paths start from. Any
 * libxml2 node of a run of adjacent text nodes stands for the whole
 * run (doc/DESIGN.md 4.5).
 *
 * @param ctx   the context
 * @param node  the node, or NULL for no context item
 * @returns 0, or -1 if ctx is NULL or the node is not part of the XDM
 *          tree: a DTD, an entity reference, a namespace declaration,
 *          or text without content
 */
int
xp3SetContextNode(xp3DynamicContext *ctx, xmlNodePtr node) {
    xmlNodePtr canonical = NULL;

    if (ctx == NULL)
        return(-1);
    if (node != NULL) {
        canonical = xp3NodeCanonical(node);
        if (canonical == NULL)
            return(-1);
    }
    ctx->contextNode = canonical;
    return(0);
}

/**
 * Bind a value to a variable, which expressions compiled with the
 * variable declared (xp3DeclareVariable()) can use. Binding the same
 * name again replaces the value.
 *
 * @param ctx        the context
 * @param uri        the namespace URI of the name, or NULL or "" for
 *                   no namespace
 * @param localName  the local name
 * @param value      the value, which gets a reference of its own; NULL
 *                   removes the binding
 * @returns 0, or -1 if an argument is invalid or memory allocation
 *          failed
 */
int
xp3BindVariable(xp3DynamicContext *ctx, const xmlChar *uri,
                const xmlChar *localName, xp3Sequence *value) {
    xp3BoundVariable *var;
    int i;

    if ((ctx == NULL) || (localName == NULL))
        return(-1);
    if ((uri != NULL) && (*uri == 0))
        uri = NULL;

    for (i = 0; i < ctx->variableCount; i++) {
        var = &ctx->variables[i];
        if (!xmlStrEqual(var->uri, uri) ||
            !xmlStrEqual(var->localName, localName))
            continue;
        if (value == NULL) {
            xmlFree(var->uri);
            xmlFree(var->localName);
            xp3ValueUnref(var->value);
            ctx->variables[i] = ctx->variables[--ctx->variableCount];
        } else {
            xp3ValueRef(value);
            xp3ValueUnref(var->value);
            var->value = value;
        }
        return(0);
    }
    if (value == NULL)
        return(0);

    if (ctx->variableCount >= ctx->variableMax) {
        xp3BoundVariable *vars;
        int max;

        if ((ctx->variableMax > INT_MAX / 2) ||
            ((size_t) ctx->variableMax >
             SIZE_MAX / 2 / sizeof(xp3BoundVariable)))
            return(-1);
        max = (ctx->variableMax > 0) ? ctx->variableMax * 2 : 8;
        vars = xmlRealloc(ctx->variables,
                          (size_t) max * sizeof(xp3BoundVariable));
        if (vars == NULL)
            return(-1);
        ctx->variables = vars;
        ctx->variableMax = max;
    }
    var = &ctx->variables[ctx->variableCount];
    var->uri = (uri != NULL) ? xmlStrdup(uri) : NULL;
    var->localName = xmlStrdup(localName);
    if (((uri != NULL) && (var->uri == NULL)) || (var->localName == NULL)) {
        xmlFree(var->uri);
        xmlFree(var->localName);
        return(-1);
    }
    var->value = xp3ValueRef(value);
    ctx->variableCount++;
    return(0);
}

/**
 * Limit the number of items in the sequences an evaluation builds. A
 * longer sequence raises the error LIMIT. The default is 10 million,
 * the same as libxml2's limit on node-sets. Integer ranges such as
 * (1 to 100000000) are not stored item by item and don't count, and
 * parts of an expression that compiling evaluated already, such as the
 * (1, 2, 3) in (1, 2, 3)[2], build no sequences when evaluated.
 *
 * @param ctx  the context
 * @param max  the most items, at least 1
 * @returns 0, or -1 if an argument is invalid
 */
int
xp3SetMaxSequenceLength(xp3DynamicContext *ctx, size_t max) {
    if ((ctx == NULL) || (max == 0))
        return(-1);
    ctx->maxSequenceLength = max;
    return(0);
}

/*
 * Evaluation state
 */

/* The focus: the context item, position and size (XPath 3.1, 2.1.2) */
typedef struct {
    xp3Item item;               /* borrowed from the sequence iterated */
    int present;
    uint64_t position;          /* 1-based */
    uint64_t size;
} xp3Focus;

typedef struct {
    xp3Value **slots;           /* one reference each, or NULL */
    int slotCount;
    xp3Focus focus;
    xp3DocOrder *order;         /* created when first needed */
    size_t maxItems;
    xp3LanguageLevel level;
    uint64_t operations;        /* steps taken so far */
    uint64_t maxOperations;     /* 0 for no limit */
    size_t maxBytes;            /* folding only: most bytes of a value,
                                 * see evalCheckSize(); 0 for no limit */
    xp3Error *err;              /* the first error */
} xp3Eval;

/* Measuring the size of values while folding costs a step per this
 * many bytes, so it can't take longer than the steps allow */
#define XP3_BYTES_PER_STEP 256

static xp3Value *
evalExpr(xp3Eval *ev, const xp3AstNode *node);

/* Record an error at a node, unless one is recorded already. */
static void
evalError(xp3Eval *ev, xp3ErrCode code, const xp3AstNode *node,
          const char *format, ...) {
    va_list args;

    if (ev->err != NULL)
        return;
    if (code == XP3_ERR_NO_MEMORY) {
        ev->err = xp3ErrorNoMemory();
        return;
    }
    va_start(args, format);
    ev->err = xp3ErrorNewV(code, (node != NULL) ? node->line : 0,
                           (node != NULL) ? node->column : 0, format, args);
    va_end(args);
}

static void
evalNoMemory(xp3Eval *ev) {
    evalError(ev, XP3_ERR_NO_MEMORY, NULL, NULL);
}

/* Record the failure of a builder or iterator: memory or the limit */
static void
evalBuildError(xp3Eval *ev, xp3ErrCode code, const xp3AstNode *node) {
    if ((code == XP3_ERR_LIMIT) && (ev->maxBytes > 0))
        evalError(ev, XP3_ERR_LIMIT, node,
                  "a sequence would have more than %lu items or %lu bytes",
                  (unsigned long) ev->maxItems,
                  (unsigned long) ev->maxBytes);
    else if (code == XP3_ERR_LIMIT)
        evalError(ev, XP3_ERR_LIMIT, node,
                  "a sequence would have more than %lu items",
                  (unsigned long) ev->maxItems);
    else
        evalNoMemory(ev);
}

/*
 * Count steps of work: each expression evaluated, each node an axis
 * visits, each pair a general comparison tries and each item of an
 * expanded range. Past the limit, the evaluation fails with LIMIT.
 * Returns 0 or -1.
 */
static int
evalSteps(xp3Eval *ev, const xp3AstNode *node, uint64_t steps) {
    ev->operations += steps;
    if ((ev->maxOperations > 0) && (ev->operations > ev->maxOperations)) {
        evalError(ev, XP3_ERR_LIMIT, node, "the evaluation needs more than "
                  "%lu steps", (unsigned long) ev->maxOperations);
        return(-1);
    }
    return(0);
}

static int
evalCount(xp3Eval *ev, const xp3AstNode *node) {
    return(evalSteps(ev, node, 1));
}

static void
builderInit(const xp3Eval *ev, xp3SeqBuilder *builder) {
    xp3SeqBuilderInit(builder);
    builder->max = ev->maxItems;
    builder->maxBytes = ev->maxBytes;
}

static xp3Value *
builderFinish(xp3Eval *ev, xp3SeqBuilder *builder) {
    size_t bytes = builder->bytes;
    xp3Value *value = xp3SeqBuilderFinish(builder);

    if (value == NULL) {
        evalNoMemory(ev);
        return(NULL);
    }
    if (evalSteps(ev, NULL, bytes / XP3_BYTES_PER_STEP) < 0) {
        xp3ValueUnref(value);
        return(NULL);
    }
    return(value);
}

/* A sequence of one item, taking over its reference */
static xp3Value *
evalSingle(xp3Eval *ev, xp3Item item) {
    xp3Value *value = xp3ValueSingle(item);

    if (value == NULL)
        evalNoMemory(ev);
    return(value);
}

static xp3Value *
evalBoolean(xp3Eval *ev, int value) {
    xp3Atomic *atomic = xp3AtomicNewBoolean(value);

    if (atomic == NULL) {
        evalNoMemory(ev);
        return(NULL);
    }
    return(evalSingle(ev, xp3ItemFromAtomic(atomic)));
}

/* The name of a node as written, for messages. Unprefixed function
 * names resolve to the fn namespace and are shown without it. */
static const char *
displayName(const xp3AstNode *node, char *buf, size_t size) {
    const char *local = node->text ? (const char *) node->text : "";

    if (node->prefix != NULL)
        snprintf(buf, size, "%s:%s", (const char *) node->prefix, local);
    else if ((node->uri != NULL) &&
             !xmlStrEqual(node->uri, BAD_CAST XP3_NS_FN))
        snprintf(buf, size, "Q{%s}%s", (const char *) node->uri, local);
    else
        snprintf(buf, size, "%s", local);
    return(buf);
}

/*
 * Effective boolean value
 */

/* Whether a type is xs:string, a type derived from it, xs:anyURI or
 * xs:untypedAtomic */
static int
isStringLike(xp3TypeId type) {
    return(xp3TypeDerivesFrom(type, XP3_TYPE_STRING) ||
           (type == XP3_TYPE_ANY_URI) || (type == XP3_TYPE_UNTYPED_ATOMIC));
}

/**
 * Compute the effective boolean value of a sequence (XPath 3.1,
 * 2.4.3): false for the empty sequence, true if the first item is a
 * node, and for a single atomic value its boolean value, string
 * length or numeric value.
 *
 * @param value   the sequence
 * @param result  set to 0 or 1
 * @returns XP3_ERR_OK, XP3_ERR_FORG0006 if the sequence has none, or
 *          XP3_ERR_NO_MEMORY
 */
xp3ErrCode
xp3EffectiveBoolean(const xp3Value *value, int *result) {
    const xp3Atomic *atomic;
    xp3ErrCode ret = XP3_ERR_OK;
    xp3Item item;
    int found;

    *result = 0;
    found = xp3ValueItemAt(value, 0, &item);
    if (found < 0)
        return(XP3_ERR_NO_MEMORY);
    if (found == 0)
        return(XP3_ERR_OK);
    if (item.kind == XP3_ITEM_NODE) {
        *result = 1;
        return(XP3_ERR_OK);
    }
    if ((item.kind != XP3_ITEM_ATOMIC) || (xp3ValueCount(value) > 1)) {
        xp3ItemUnref(item);
        return(XP3_ERR_FORG0006);
    }

    atomic = item.u.atomic;
    switch (xp3TypeRepresentation(atomic->type)) {
        case XP3_REP_BOOLEAN:
            *result = atomic->u.boolean;
            break;
        case XP3_REP_STRING:
            if (isStringLike(atomic->type))
                *result = (atomic->u.str[0] != 0);
            else
                ret = XP3_ERR_FORG0006;
            break;
        case XP3_REP_DECIMAL:
            *result = (xp3DecimalSign(atomic->u.dec) != 0);
            break;
        case XP3_REP_FLOAT:
            *result = (atomic->u.f != 0) && !isnan(atomic->u.f);
            break;
        case XP3_REP_DOUBLE:
            *result = (atomic->u.d != 0) && !isnan(atomic->u.d);
            break;
        default:
            ret = XP3_ERR_FORG0006;
            break;
    }
    xp3ItemUnref(item);
    return(ret);
}

/*
 * Evaluate an expression to its effective boolean value. Returns 0 or
 * 1, or -1 on error.
 */
static int
evalCondition(xp3Eval *ev, const xp3AstNode *node) {
    xp3Value *value = evalExpr(ev, node);
    xp3ErrCode err;
    int result;

    if (value == NULL)
        return(-1);
    err = xp3EffectiveBoolean(value, &result);
    xp3ValueUnref(value);
    if (err == XP3_ERR_OK)
        return(result);
    if (err == XP3_ERR_FORG0006)
        evalError(ev, err, node, "the condition has no effective "
                  "boolean value: it is not empty, does not start with a "
                  "node, and is not a single boolean, string or number");
    else
        evalNoMemory(ev);
    return(-1);
}

/*
 * Primary expressions
 */

static xp3Value *
evalLiteral(xp3Eval *ev, const xp3AstNode *node) {
    size_t len = strlen((const char *) node->text);
    xp3Atomic *atomic = NULL;

    switch (node->kind) {
        case XP3_AST_INTEGER:
        case XP3_AST_DECIMAL: {
            int isInteger = (node->kind == XP3_AST_INTEGER);
            xp3Decimal *dec;
            xp3ErrCode err;

            err = xp3DecimalParse(node->text, len, isInteger, &dec);
            if (err == XP3_ERR_LIMIT) {
                evalError(ev, err, node, "the number %.40s... has too "
                          "many digits", (const char *) node->text);
                return(NULL);
            }
            if (err != XP3_ERR_OK) {
                evalNoMemory(ev);
                return(NULL);
            }
            atomic = xp3AtomicNewDecimal(isInteger ? XP3_TYPE_INTEGER :
                                                     XP3_TYPE_DECIMAL, dec);
            break;
        }
        case XP3_AST_DOUBLE: {
            double number;

            if (xp3DoubleParse((const char *) node->text, &number) < 0) {
                evalNoMemory(ev);
                return(NULL);
            }
            atomic = xp3AtomicNewDouble(number);
            break;
        }
        default:
            atomic = xp3AtomicNewString(XP3_TYPE_STRING, node->text, len);
            break;
    }
    if (atomic == NULL) {
        evalNoMemory(ev);
        return(NULL);
    }
    return(evalSingle(ev, xp3ItemFromAtomic(atomic)));
}

static xp3Value *
evalVarRef(xp3Eval *ev, const xp3AstNode *node) {
    char name[XP3_NAME_BUF];

    if ((node->op < 0) || (node->op >= ev->slotCount) ||
        (ev->slots[node->op] == NULL)) {
        evalError(ev, XP3_ERR_XPDY0002, node, "variable $%s has no value",
                  displayName(node, name, sizeof(name)));
        return(NULL);
    }
    return(xp3ValueRef(ev->slots[node->op]));
}

static xp3Value *
evalContextItem(xp3Eval *ev, const xp3AstNode *node) {
    if (!ev->focus.present) {
        evalError(ev, XP3_ERR_XPDY0002, node, "the context item is absent");
        return(NULL);
    }
    return(evalSingle(ev, xp3ItemRef(ev->focus.item)));
}

/* a, b, c: the items of each in turn */
static xp3Value *
evalSequence(xp3Eval *ev, const xp3AstNode *node) {
    const xp3AstNode *child;
    xp3SeqBuilder builder;

    builderInit(ev, &builder);
    for (child = node->first; child != NULL; child = child->next) {
        xp3Value *value = evalExpr(ev, child);
        xp3ErrCode err;

        if (value == NULL) {
            xp3SeqBuilderClear(&builder);
            return(NULL);
        }
        err = xp3SeqBuilderAddValue(&builder, value);
        xp3ValueUnref(value);
        if (err != XP3_ERR_OK) {
            evalBuildError(ev, err, node);
            xp3SeqBuilderClear(&builder);
            return(NULL);
        }
    }
    return(builderFinish(ev, &builder));
}

/*
 * Predicates
 */

/* Whether a numeric value equals a position */
static int
numericEquals(const xp3Atomic *atomic, uint64_t position) {
    int64_t integer;

    switch (xp3TypeRepresentation(atomic->type)) {
        case XP3_REP_DECIMAL:
            return((xp3DecimalToInt64(atomic->u.dec, &integer) == 0) &&
                   (integer > 0) && ((uint64_t) integer == position));
        case XP3_REP_FLOAT:
            return((double) atomic->u.f == (double) position);
        default:
            return(atomic->u.d == (double) position);
    }
}

/*
 * Whether an item passes a predicate (XPath 3.1, 3.3.3): a single
 * number selects the item at that position, anything else counts by
 * its effective boolean value. The focus is set by the caller. Returns
 * 0 or 1, or -1 on error.
 */
static int
predicateKeeps(xp3Eval *ev, const xp3AstNode *pred) {
    const xp3AstNode *expr = pred->first;
    xp3Value *value;
    xp3Item item;
    int ret = -2;

    value = evalExpr(ev, expr);
    if (value == NULL)
        return(-1);
    if (xp3ValueCount(value) == 1) {
        int found = xp3ValueItemAt(value, 0, &item);

        if (found < 0) {
            evalNoMemory(ev);
            ret = -1;
        } else if ((item.kind == XP3_ITEM_ATOMIC) &&
                   ((xp3TypeRepresentation(item.u.atomic->type) ==
                     XP3_REP_DECIMAL) ||
                    (xp3TypeRepresentation(item.u.atomic->type) ==
                     XP3_REP_FLOAT) ||
                    (xp3TypeRepresentation(item.u.atomic->type) ==
                     XP3_REP_DOUBLE))) {
            ret = numericEquals(item.u.atomic, ev->focus.position);
        }
        if (found > 0)
            xp3ItemUnref(item);
    }
    if (ret == -2) {
        xp3ErrCode err;
        int result;

        err = xp3EffectiveBoolean(value, &result);
        if (err == XP3_ERR_OK) {
            ret = result;
        } else {
            ret = -1;
            if (err == XP3_ERR_FORG0006)
                evalError(ev, err, expr, "the predicate is neither a "
                          "number nor has an effective boolean value");
            else
                evalNoMemory(ev);
        }
    }
    xp3ValueUnref(value);
    return(ret);
}

/*
 * Filter items in place with a predicate, their positions counted in
 * the order given. The items dropped are released. Returns 0 or -1.
 */
static int
applyPredicate(xp3Eval *ev, const xp3AstNode *pred, xp3Item *items,
               size_t *count) {
    xp3Focus saved = ev->focus;
    size_t kept = 0;
    size_t i;
    int ret = 0;

    for (i = 0; i < *count; i++) {
        int keep = 0;

        if (ret == 0) {
            ev->focus.item = items[i];
            ev->focus.present = 1;
            ev->focus.position = i + 1;
            ev->focus.size = *count;
            keep = predicateKeeps(ev, pred);
            if (keep < 0)
                ret = -1;
        }
        if (keep > 0)
            items[kept++] = items[i];
        else
            xp3ItemUnref(items[i]);
    }
    *count = kept;
    ev->focus = saved;
    return(ret);
}

/*
 * Steps
 */

/* Whether a node matches a NAME_TEST, which also needs the principal
 * node kind of the axis: attributes on the attribute axis, elements
 * elsewhere */
static int
matchesName(const xp3AstNode *test, xmlNodePtr node, xp3NodeKind kind) {
    if (xp3NodeGetKind(node) != kind)
        return(0);
    switch (test->op) {
        case XP3_NAMETEST_ANY:
            return(1);
        case XP3_NAMETEST_ANY_LOCAL:
            return(xmlStrEqual(test->uri, xp3NodeNamespaceUri(node)));
        case XP3_NAMETEST_ANY_NS:
            return(xmlStrEqual(test->text, xp3NodeLocalName(node)));
        default:
            return(xmlStrEqual(test->text, xp3NodeLocalName(node)) &&
                   xmlStrEqual(test->uri, xp3NodeNamespaceUri(node)));
    }
}

/*
 * Whether the type annotation of an untyped node matches the type T in
 * element(N, T) or attribute(N, T). Without a schema, elements are
 * xs:untyped and attributes xs:untypedAtomic, so T must be one of
 * those or an ancestor type (XPath 3.1, 2.5.5.3 and 2.5.5.5).
 */
static int
matchesAnnotation(const xp3AstNode *seqType, xp3NodeKind kind) {
    int type = seqType->first->op;

    if (kind == XP3_NODE_ELEMENT)
        return((type == XP3_SCHEMA_UNTYPED) || (type == XP3_SCHEMA_ANY));
    return((type == XP3_TYPE_UNTYPED_ATOMIC) ||
           (type == XP3_TYPE_ANY_ATOMIC) || (type == XP3_SCHEMA_ANY_SIMPLE) ||
           (type == XP3_SCHEMA_ANY));
}

static int
matchesKind(const xp3AstNode *test, xmlNodePtr node);

/* document-node(E): exactly one element child, which matches E, and
 * no text children (XPath 3.1, 2.5.5.2) */
static int
matchesDocument(const xp3AstNode *test, xmlNodePtr node) {
    xmlNodePtr child = NULL;
    xmlNodePtr element = NULL;

    if (xp3NodeGetKind(node) != XP3_NODE_DOCUMENT)
        return(0);
    if (test->first == NULL)
        return(1);
    while ((child = xp3AxisNext(XP3_AXIS_CHILD, node, child)) != NULL) {
        xp3NodeKind kind = xp3NodeGetKind(child);

        if ((kind == XP3_NODE_TEXT) ||
            ((kind == XP3_NODE_ELEMENT) && (element != NULL)))
            return(0);
        if (kind == XP3_NODE_ELEMENT)
            element = child;
    }
    return((element != NULL) && matchesKind(test->first, element));
}

/* Whether a node matches a KIND_TEST */
static int
matchesKind(const xp3AstNode *test, xmlNodePtr node) {
    xp3NodeKind kind = xp3NodeGetKind(node);
    const xp3AstNode *child;

    switch (test->op) {
        case XP3_KIND_ANY:
            return(1);
        case XP3_KIND_TEXT:
            return(kind == XP3_NODE_TEXT);
        case XP3_KIND_COMMENT:
            return(kind == XP3_NODE_COMMENT);
        case XP3_KIND_PI:
            return((kind == XP3_NODE_PI) &&
                   ((test->text == NULL) ||
                    xmlStrEqual(test->text, xp3NodeLocalName(node))));
        case XP3_KIND_DOCUMENT:
            return(matchesDocument(test, node));
        case XP3_KIND_ELEMENT:
        case XP3_KIND_ATTRIBUTE: {
            xp3NodeKind wanted = (test->op == XP3_KIND_ELEMENT) ?
                                 XP3_NODE_ELEMENT : XP3_NODE_ATTRIBUTE;

            if (kind != wanted)
                return(0);
            for (child = test->first; child != NULL; child = child->next) {
                if (child->kind == XP3_AST_NAME_TEST) {
                    if (!matchesName(child, node, wanted))
                        return(0);
                } else if (!matchesAnnotation(child, wanted)) {
                    return(0);
                }
            }
            return(1);
        }
        default:
            /* namespace-node() and the schema tests, which static
             * analysis rejects */
            return(0);
    }
}

static int
isReverseAxis(xp3Axis axis) {
    return((axis == XP3_AXIS_PARENT) || (axis == XP3_AXIS_ANCESTOR) ||
           (axis == XP3_AXIS_ANCESTOR_OR_SELF) ||
           (axis == XP3_AXIS_PRECEDING_SIBLING) ||
           (axis == XP3_AXIS_PRECEDING));
}

/*
 * An axis step from the context node (XPath 3.1, 3.3.2): the nodes of
 * the axis that match the node test, filtered by the predicates with
 * positions in axis order, and returned in document order.
 */
static xp3Value *
evalStep(xp3Eval *ev, const xp3AstNode *step) {
    const xp3AstNode *test = step->first;
    const xp3AstNode *pred;
    xp3Axis axis = (xp3Axis) step->op;
    xp3NodeKind principal = (axis == XP3_AXIS_ATTRIBUTE) ?
                            XP3_NODE_ATTRIBUTE : XP3_NODE_ELEMENT;
    xp3SeqBuilder builder;
    xmlNodePtr context;
    xmlNodePtr node = NULL;

    if (!ev->focus.present) {
        evalError(ev, XP3_ERR_XPDY0002, step,
                  "an axis step needs a context node, and the context "
                  "item is absent");
        return(NULL);
    }
    if (ev->focus.item.kind != XP3_ITEM_NODE) {
        evalError(ev, XP3_ERR_XPTY0020, step,
                  "the context item of an axis step is not a node");
        return(NULL);
    }
    context = ev->focus.item.u.node;

    builderInit(ev, &builder);
    while ((node = xp3AxisNext(axis, context, node)) != NULL) {
        int match = (test->kind == XP3_AST_NAME_TEST) ?
                    matchesName(test, node, principal) :
                    matchesKind(test, node);
        xp3ErrCode err;

        if (evalCount(ev, step) < 0) {
            xp3SeqBuilderClear(&builder);
            return(NULL);
        }
        if (!match)
            continue;
        err = xp3SeqBuilderAdd(&builder, xp3ItemFromNode(node));
        if (err != XP3_ERR_OK) {
            evalBuildError(ev, err, step);
            xp3SeqBuilderClear(&builder);
            return(NULL);
        }
    }

    for (pred = test->next; pred != NULL; pred = pred->next) {
        if (applyPredicate(ev, pred, builder.items, &builder.count) < 0) {
            xp3SeqBuilderClear(&builder);
            return(NULL);
        }
    }

    if (isReverseAxis(axis) && (builder.count > 1)) {
        size_t low = 0;
        size_t high = builder.count - 1;

        while (low < high) {
            xp3Item tmp = builder.items[low];

            builder.items[low++] = builder.items[high];
            builder.items[high--] = tmp;
        }
    }
    return(builderFinish(ev, &builder));
}

/*
 * Paths
 */

static xp3ErrCode
evalSortNodes(xp3Eval *ev, xp3SeqBuilder *builder) {
    if (ev->order == NULL) {
        ev->order = xp3DocOrderNew();
        if (ev->order == NULL)
            return(XP3_ERR_NO_MEMORY);
    }
    return(xp3DocOrderSort(ev->order, builder->items, &builder->count));
}

/* "/" at the start of a path: the root of the context node, which must
 * be a document node (XPath 3.1, 3.3) */
static xp3Value *
evalRoot(xp3Eval *ev, const xp3AstNode *path) {
    xmlNodePtr root;

    if (!ev->focus.present) {
        evalError(ev, XP3_ERR_XPDY0002, path,
                  "a path starting with / needs a context node, and the "
                  "context item is absent");
        return(NULL);
    }
    if (ev->focus.item.kind != XP3_ITEM_NODE) {
        evalError(ev, XP3_ERR_XPTY0020, path,
                  "a path starting with / needs a context node, and the "
                  "context item is not a node");
        return(NULL);
    }
    root = xp3NodeRoot(ev->focus.item.u.node);
    if (xp3NodeGetKind(root) != XP3_NODE_DOCUMENT) {
        evalError(ev, XP3_ERR_XPDY0050, path,
                  "the root of the context node is not a document node");
        return(NULL);
    }
    return(evalSingle(ev, xp3ItemFromNode(root)));
}

/*
 * E1/E2 (XPath 3.1, 3.3.1.1): E2 for each node of E1 as the context
 * item. A result of nodes only is sorted into document order without
 * duplicates; a result without nodes is kept in order; a mix is an
 * error.
 */
static xp3Value *
evalPathStep(xp3Eval *ev, xp3Value *input, const xp3AstNode *right) {
    uint64_t size = xp3ValueCount(input);
    xp3Focus saved = ev->focus;
    xp3SeqBuilder builder;
    xp3Iter iter;
    xp3Item item;
    size_t nodes = 0;
    size_t others = 0;
    int found;

    builderInit(ev, &builder);
    xp3IterInit(&iter, input);
    while ((found = xp3IterNext(&iter, &item)) > 0) {
        xp3Value *value;
        xp3Iter results;
        xp3Item result;
        int more;

        if (item.kind != XP3_ITEM_NODE) {
            xp3ItemUnref(item);
            evalError(ev, XP3_ERR_XPTY0019, right,
                      "the left side of / contains an item that is not a "
                      "node");
            goto error;
        }
        ev->focus.item = item;
        ev->focus.present = 1;
        ev->focus.position = iter.pos;
        ev->focus.size = size;
        value = evalExpr(ev, right);
        ev->focus = saved;
        xp3ItemUnref(item);
        if (value == NULL)
            goto error;

        xp3IterInit(&results, value);
        while ((more = xp3IterNext(&results, &result)) > 0) {
            xp3ErrCode err;

            if (result.kind == XP3_ITEM_NODE)
                nodes++;
            else
                others++;
            err = xp3SeqBuilderAdd(&builder, result);
            if (err != XP3_ERR_OK) {
                evalBuildError(ev, err, right);
                xp3ValueUnref(value);
                goto error;
            }
        }
        xp3ValueUnref(value);
        if (more < 0) {
            evalNoMemory(ev);
            goto error;
        }
    }
    if (found < 0) {
        evalNoMemory(ev);
        goto error;
    }

    if ((nodes > 0) && (others > 0)) {
        evalError(ev, XP3_ERR_XPTY0018, right,
                  "the last step of a path returns both nodes and other "
                  "items");
        goto error;
    }
    /* One context node and an axis step give document order already */
    if ((nodes > 1) && !(right->flags & XP3_AST_SORTED) &&
        ((size > 1) || (right->kind != XP3_AST_STEP))) {
        xp3ErrCode err = evalSortNodes(ev, &builder);

        if (err != XP3_ERR_OK) {
            if (err == XP3_ERR_NO_MEMORY)
                evalNoMemory(ev);
            else
                evalError(ev, err, right, "a node of the result is not "
                          "part of the data model");
            goto error;
        }
    }
    return(builderFinish(ev, &builder));

error:
    xp3SeqBuilderClear(&builder);
    return(NULL);
}

static xp3Value *
evalPath(xp3Eval *ev, const xp3AstNode *path) {
    const xp3AstNode *child = path->first;
    xp3Value *current;

    if (path->op == XP3_PATH_ROOT) {
        current = evalRoot(ev, path);
    } else {
        current = evalExpr(ev, child);
        child = child->next;
    }
    for (; (child != NULL) && (current != NULL); child = child->next) {
        xp3Value *next = evalPathStep(ev, current, child);

        xp3ValueUnref(current);
        current = next;
    }
    return(current);
}

/* A primary expression with predicates (XPath 3.1, 3.2.1), positions
 * counted in sequence order */
static xp3Value *
evalFilter(xp3Eval *ev, const xp3AstNode *node) {
    const xp3AstNode *child = node->first;
    xp3SeqBuilder builder;
    xp3Value *value;
    xp3ErrCode err;

    value = evalExpr(ev, child);
    if (value == NULL)
        return(NULL);
    builderInit(ev, &builder);
    err = xp3SeqBuilderAddValue(&builder, value);
    xp3ValueUnref(value);
    if (err != XP3_ERR_OK) {
        evalBuildError(ev, err, node);
        xp3SeqBuilderClear(&builder);
        return(NULL);
    }
    for (child = child->next; child != NULL; child = child->next) {
        if (child->kind != XP3_AST_PREDICATE) {
            evalError(ev, XP3_ERR_UNSUPPORTED, child, "%s is not "
                      "implemented yet", (child->kind == XP3_AST_LOOKUP) ?
                      "the lookup operator" : "calling a function item");
            xp3SeqBuilderClear(&builder);
            return(NULL);
        }
        if (applyPredicate(ev, child, builder.items, &builder.count) < 0) {
            xp3SeqBuilderClear(&builder);
            return(NULL);
        }
    }
    return(builderFinish(ev, &builder));
}

/*
 * for, let, some and every
 *
 * These loop over their bindings instead of recursing per binding: a
 * long list of bindings is not nesting, so the parser's depth limit
 * doesn't bound it, and recursion would overflow the stack.
 */

/* One binding of a for or quantified expression while it iterates */
typedef struct {
    const xp3AstNode *binding;
    xp3Value *value;            /* the sequence it ranges over */
    xp3Iter iter;
    xp3Value *current;          /* the item bound now, as a sequence */
    xp3Value *saved;            /* the slot before */
} xp3ClauseState;

/* Called with every combination of items bound: 0 to go on, 1 to
 * stop, -1 on error */
typedef int (*xp3ClauseVisit)(xp3Eval *ev, const xp3AstNode *last,
                              void *data);

/*
 * Bind each combination of the items of the BINDING nodes from first
 * on, in order, and call visit with the expression after them.
 * Returns 0 when done, 1 if visit stopped, or -1 on error.
 */
static int
forEachBinding(xp3Eval *ev, const xp3AstNode *first, xp3ClauseVisit visit,
               void *data) {
    const xp3AstNode *last = first;
    xp3ClauseState *states;
    size_t count = 0;
    size_t depth;
    size_t i;
    int ret = 0;

    while (last->kind == XP3_AST_BINDING) {
        count++;
        last = last->next;
    }
    if (count > SIZE_MAX / sizeof(xp3ClauseState)) {
        evalNoMemory(ev);
        return(-1);
    }
    states = xmlMalloc(count * sizeof(xp3ClauseState));
    if (states == NULL) {
        evalNoMemory(ev);
        return(-1);
    }
    for (i = 0; i < count; i++) {
        states[i].binding = (i == 0) ? first : states[i - 1].binding->next;
        states[i].value = NULL;
        states[i].current = NULL;
        states[i].saved = ev->slots[states[i].binding->op];
    }

    /* Odometer: depth is the binding whose next item comes next */
    depth = 0;
    states[0].value = evalExpr(ev, first->first);
    if (states[0].value == NULL)
        ret = -1;
    else
        xp3IterInit(&states[0].iter, states[0].value);
    while (ret == 0) {
        xp3ClauseState *state = &states[depth];
        xp3Item item;
        int found = xp3IterNext(&state->iter, &item);

        if (found < 0) {
            evalNoMemory(ev);
            ret = -1;
            break;
        }
        if (found == 0) {
            /* This binding is done: back to the one before */
            ev->slots[state->binding->op] = state->saved;
            xp3ValueUnref(state->current);
            state->current = NULL;
            xp3ValueUnref(state->value);
            state->value = NULL;
            if (depth == 0)
                break;
            depth--;
            continue;
        }
        /* Unbind the previous item before releasing it */
        ev->slots[state->binding->op] = state->saved;
        xp3ValueUnref(state->current);
        state->current = evalSingle(ev, item);
        if (state->current == NULL) {
            ret = -1;
            break;
        }
        ev->slots[state->binding->op] = state->current;
        if (depth + 1 == count) {
            ret = visit(ev, last, data);
            continue;
        }
        depth++;
        state = &states[depth];
        state->value = evalExpr(ev, state->binding->first);
        if (state->value == NULL) {
            ret = -1;
            break;
        }
        xp3IterInit(&state->iter, state->value);
    }

    /* Restore the slots, innermost first, after an error or a stop */
    for (i = count; i > 0; i--) {
        xp3ClauseState *state = &states[i - 1];

        if (state->current != NULL)
            ev->slots[state->binding->op] = state->saved;
        xp3ValueUnref(state->current);
        xp3ValueUnref(state->value);
    }
    xmlFree(states);
    return(ret);
}

/* for: add the value of the return expression to the builder */
static int
visitFor(xp3Eval *ev, const xp3AstNode *last, void *data) {
    xp3SeqBuilder *builder = data;
    xp3Value *value = evalExpr(ev, last);
    xp3ErrCode err;

    if (value == NULL)
        return(-1);
    err = xp3SeqBuilderAddValue(builder, value);
    xp3ValueUnref(value);
    if (err != XP3_ERR_OK) {
        evalBuildError(ev, err, last);
        return(-1);
    }
    return(0);
}

static xp3Value *
evalFor(xp3Eval *ev, const xp3AstNode *node) {
    xp3SeqBuilder builder;

    builderInit(ev, &builder);
    if (forEachBinding(ev, node->first, visitFor, &builder) < 0) {
        xp3SeqBuilderClear(&builder);
        return(NULL);
    }
    return(builderFinish(ev, &builder));
}

/* some and every: stop at the first binding that decides the result */
typedef struct {
    int isSome;
    int result;
} xp3Quantifier;

static int
visitQuantified(xp3Eval *ev, const xp3AstNode *last, void *data) {
    xp3Quantifier *quantifier = data;
    int result = evalCondition(ev, last);

    if (result < 0)
        return(-1);
    if (result != quantifier->isSome)
        return(0);
    quantifier->result = result;
    return(1);
}

static xp3Value *
evalQuantified(xp3Eval *ev, const xp3AstNode *node) {
    xp3Quantifier quantifier;

    quantifier.isSome = (node->kind == XP3_AST_SOME);
    quantifier.result = !quantifier.isSome;
    if (forEachBinding(ev, node->first, visitQuantified, &quantifier) < 0)
        return(NULL);
    return(evalBoolean(ev, quantifier.result));
}

/* let $x := E, ...: bind each value in turn, then the return
 * expression */
static xp3Value *
evalLet(xp3Eval *ev, const xp3AstNode *node) {
    const xp3AstNode *clause;
    struct {
        const xp3AstNode *binding;
        xp3Value *saved;        /* the slot before */
    } *bindings;
    xp3Value *ret = NULL;
    size_t count = 0;
    size_t bound = 0;

    for (clause = node->first; clause->kind == XP3_AST_BINDING;
         clause = clause->next)
        count++;
    if (count > SIZE_MAX / sizeof(*bindings)) {
        evalNoMemory(ev);
        return(NULL);
    }
    bindings = xmlMalloc(count * sizeof(*bindings));
    if (bindings == NULL) {
        evalNoMemory(ev);
        return(NULL);
    }
    for (clause = node->first; clause->kind == XP3_AST_BINDING;
         clause = clause->next) {
        xp3Value *value = evalExpr(ev, clause->first);

        if (value == NULL)
            break;
        bindings[bound].binding = clause;
        bindings[bound].saved = ev->slots[clause->op];
        bound++;
        ev->slots[clause->op] = value;
    }
    if (bound == count)
        ret = evalExpr(ev, clause);

    /* Unbind in reverse order */
    while (bound > 0) {
        bound--;
        xp3ValueUnref(ev->slots[bindings[bound].binding->op]);
        ev->slots[bindings[bound].binding->op] = bindings[bound].saved;
    }
    xmlFree(bindings);
    return(ret);
}

/*
 * Conditionals and logic
 */

static xp3Value *
evalIf(xp3Eval *ev, const xp3AstNode *node) {
    int condition = evalCondition(ev, node->first);

    if (condition < 0)
        return(NULL);
    return(evalExpr(ev, condition ? node->first->next : node->last));
}

/* and, or: the left side first, the right side only if needed */
static xp3Value *
evalLogical(xp3Eval *ev, const xp3AstNode *node) {
    int isOr = (node->op == XP3_OP_OR);
    int result = evalCondition(ev, node->first);

    if (result < 0)
        return(NULL);
    if (result != isOr) {
        result = evalCondition(ev, node->last);
        if (result < 0)
            return(NULL);
    }
    return(evalBoolean(ev, result));
}

/*
 * Not implemented yet
 */

static const char *const xp3OperatorNames[] = {
    "or", "and", "=", "!=", "<", "<=", ">", ">=", "eq", "ne", "lt", "le",
    "gt", "ge", "is", "<<", ">>", "||", "to", "+", "-", "*", "div", "idiv",
    "mod", "union", "intersect", "except", "!"
};

static const char *
operatorName(int op) {
    if ((op < 0) ||
        ((size_t) op >=
         sizeof(xp3OperatorNames) / sizeof(xp3OperatorNames[0])))
        return("?");
    return(xp3OperatorNames[op]);
}

static xp3Value *
evalUnsupported(xp3Eval *ev, const xp3AstNode *node) {
    char name[XP3_NAME_BUF];

    switch (node->kind) {
        case XP3_AST_FUNCTION_CALL:
            evalError(ev, XP3_ERR_UNSUPPORTED, node, "the function %s#%d "
                      "is not implemented yet",
                      displayName(node, name, sizeof(name)),
                      (int) xp3AstChildCount(node));
            break;
        case XP3_AST_BINARY:
        case XP3_AST_UNARY:
            evalError(ev, XP3_ERR_UNSUPPORTED, node, "the operator %s is "
                      "not implemented yet", operatorName(node->op));
            break;
        default:
            evalError(ev, XP3_ERR_UNSUPPORTED, node, "%s expressions are "
                      "not implemented yet", xp3AstKindName(node->kind));
            break;
    }
    return(NULL);
}

/*
 * Atomization
 */

/* Whether a sequence holds nodes only */
static int
isAllNodes(const xp3Value *value) {
    size_t i;

    switch (value->shape) {
        case XP3_SEQ_EMPTY:
            return(1);
        case XP3_SEQ_SINGLE:
            return(value->u.single.kind == XP3_ITEM_NODE);
        case XP3_SEQ_ARRAY:
            for (i = 0; i < value->u.array.count; i++) {
                if (value->u.array.items[i].kind != XP3_ITEM_NODE)
                    return(0);
            }
            return(1);
        default:
            return(0);
    }
}

/* Whether a sequence holds atomic values only */
static int
isAllAtomic(const xp3Value *value) {
    size_t i;

    switch (value->shape) {
        case XP3_SEQ_SINGLE:
            return(value->u.single.kind == XP3_ITEM_ATOMIC);
        case XP3_SEQ_ARRAY:
            for (i = 0; i < value->u.array.count; i++) {
                if (value->u.array.items[i].kind != XP3_ITEM_ATOMIC)
                    return(0);
            }
            return(1);
        default:
            return(1);
    }
}

/*
 * Atomize a sequence (XPath 3.1, 2.4.2): nodes become their typed
 * values. Takes over the reference to value.
 */
static xp3Value *
atomize(xp3Eval *ev, xp3Value *value, const xp3AstNode *node) {
    xp3SeqBuilder builder;
    xp3Iter iter;
    xp3Item item;
    int found;

    if (isAllAtomic(value))
        return(value);
    builderInit(ev, &builder);
    xp3IterInit(&iter, value);
    while ((found = xp3IterNext(&iter, &item)) > 0) {
        xp3ErrCode err;

        if (item.kind == XP3_ITEM_NODE) {
            xp3Atomic *atomic;

            err = xp3NodeTypedValue(item.u.node, &atomic);
            if (err == XP3_ERR_OK)
                err = xp3SeqBuilderAdd(&builder, xp3ItemFromAtomic(atomic));
        } else if (item.kind == XP3_ITEM_ATOMIC) {
            err = xp3SeqBuilderAdd(&builder, item);
        } else {
            xp3ItemUnref(item);
            evalError(ev, XP3_ERR_UNSUPPORTED, node, "atomizing function "
                      "items, maps and arrays is not implemented yet");
            goto error;
        }
        if (err != XP3_ERR_OK) {
            if (err == XP3_ERR_LIMIT)
                evalError(ev, err, node, "the string value of a node is "
                          "longer than 2 GB");
            else
                evalBuildError(ev, err, node);
            goto error;
        }
    }
    if (found < 0) {
        evalNoMemory(ev);
        goto error;
    }
    xp3ValueUnref(value);
    return(builderFinish(ev, &builder));

error:
    xp3SeqBuilderClear(&builder);
    xp3ValueUnref(value);
    return(NULL);
}

/*
 * Evaluate an operand that must be one atomic value or empty, as the
 * operands of arithmetic, value comparisons and casts. Sets *atomic to
 * the value with a reference, or to NULL for the empty sequence.
 * Returns 0, or -1 on error.
 */
static int
evalAtomic(xp3Eval *ev, const xp3AstNode *node, const char *what,
           xp3Atomic **atomic) {
    xp3Value *value = evalExpr(ev, node);
    xp3Item item;
    int ret = 0;

    *atomic = NULL;
    if (value != NULL)
        value = atomize(ev, value, node);
    if (value == NULL)
        return(-1);
    if (xp3ValueCount(value) > 1) {
        evalError(ev, XP3_ERR_XPTY0004, node, "the operand of %s is a "
                  "sequence of more than one item", what);
        ret = -1;
    } else if (xp3ValueCount(value) == 1) {
        if (xp3ValueItemAt(value, 0, &item) > 0) {
            *atomic = item.u.atomic;
        } else {
            evalNoMemory(ev);
            ret = -1;
        }
    }
    xp3ValueUnref(value);
    return(ret);
}

/* The name of an atomic type, for messages */
static const char *
typeName(xp3TypeId type) {
    const xmlChar *name = xp3TypeLocalName(type);

    return((name != NULL) ? (const char *) name : "?");
}

/* Cast an untyped value for an operator, recording a failure. Takes
 * over the reference to value. */
static xp3Atomic *
castUntyped(xp3Eval *ev, xp3Atomic *value, xp3TypeId type,
            const xp3AstNode *node) {
    xp3Atomic *result;
    xp3ErrCode err;

    err = xp3CastAtomic(value, type, &result);
    if (err == XP3_ERR_FORG0001)
        evalError(ev, err, node, "the untyped value \"%.40s\" is not a "
                  "valid xs:%s", (const char *) value->u.str,
                  typeName(type));
    else if (err == XP3_ERR_UNSUPPORTED)
        evalError(ev, err, node, "casting to xs:%s is not implemented "
                  "yet", typeName(type));
    else if (err != XP3_ERR_OK)
        evalNoMemory(ev);
    xp3AtomicUnref(value);
    return((err == XP3_ERR_OK) ? result : NULL);
}

/*
 * Arithmetic
 */

/* +, -, *, div, idiv, mod (XPath 3.1, 3.5) */
static xp3Value *
evalArithmetic(xp3Eval *ev, const xp3AstNode *node) {
    const char *name = operatorName(node->op);
    xp3Atomic *left;
    xp3Atomic *right = NULL;
    xp3Atomic *result;
    xp3ErrCode err;

    if (evalAtomic(ev, node->first, name, &left) < 0)
        return(NULL);
    if ((left != NULL) && (evalAtomic(ev, node->last, name, &right) < 0)) {
        xp3AtomicUnref(left);
        return(NULL);
    }
    if ((left == NULL) || (right == NULL)) {
        xp3AtomicUnref(left);
        xp3AtomicUnref(right);
        return(xp3ValueEmpty());
    }
    if (left->type == XP3_TYPE_UNTYPED_ATOMIC)
        left = castUntyped(ev, left, XP3_TYPE_DOUBLE, node->first);
    if ((left != NULL) && (right->type == XP3_TYPE_UNTYPED_ATOMIC))
        right = castUntyped(ev, right, XP3_TYPE_DOUBLE, node->last);
    if ((left == NULL) || (right == NULL)) {
        xp3AtomicUnref(left);
        xp3AtomicUnref(right);
        return(NULL);
    }

    err = xp3Arithmetic((xp3AstOp) node->op, left, right, &result);
    switch (err) {
        case XP3_ERR_OK:
            break;
        case XP3_ERR_XPTY0004:
            evalError(ev, err, node, "%s is not defined for xs:%s and xs:%s",
                      name, typeName(left->type), typeName(right->type));
            break;
        case XP3_ERR_FOAR0001:
            evalError(ev, err, node, "division by zero");
            break;
        case XP3_ERR_FOAR0002:
            evalError(ev, err, node, "the result of %s is too large, or "
                      "idiv got NaN or infinity", name);
            break;
        default:
            evalNoMemory(ev);
            break;
    }
    xp3AtomicUnref(left);
    xp3AtomicUnref(right);
    if (err != XP3_ERR_OK)
        return(NULL);
    return(evalSingle(ev, xp3ItemFromAtomic(result)));
}

/* Unary + and - (XPath 3.1, 3.5) */
static xp3Value *
evalUnary(xp3Eval *ev, const xp3AstNode *node) {
    const char *name = (node->op == XP3_OP_SUB) ? "unary -" : "unary +";
    xp3Atomic *value;
    xp3Atomic *result;
    xp3ErrCode err;

    if (evalAtomic(ev, node->first, name, &value) < 0)
        return(NULL);
    if (value == NULL)
        return(xp3ValueEmpty());
    if (value->type == XP3_TYPE_UNTYPED_ATOMIC) {
        value = castUntyped(ev, value, XP3_TYPE_DOUBLE, node->first);
        if (value == NULL)
            return(NULL);
    }
    if (!xp3TypeIsNumeric(value->type)) {
        evalError(ev, XP3_ERR_XPTY0004, node, "%s is not defined for xs:%s",
                  name, typeName(value->type));
        xp3AtomicUnref(value);
        return(NULL);
    }
    if (node->op != XP3_OP_SUB)
        return(evalSingle(ev, xp3ItemFromAtomic(value)));
    err = xp3NegateNumber(value, &result);
    xp3AtomicUnref(value);
    if (err != XP3_ERR_OK) {
        evalNoMemory(ev);
        return(NULL);
    }
    return(evalSingle(ev, xp3ItemFromAtomic(result)));
}

/*
 * Comparisons
 */

/* Record the failure of a comparison of two values */
static void
compareError(xp3Eval *ev, xp3ErrCode err, const xp3AstNode *node,
             const xp3Atomic *left, const xp3Atomic *right) {
    if (err == XP3_ERR_XPTY0004)
        evalError(ev, err, node, "xs:%s and xs:%s can't be compared with "
                  "%s", typeName(left->type), typeName(right->type),
                  operatorName(node->op));
    else
        evalNoMemory(ev);
}

/* eq, ne, lt, le, gt, ge (XPath 3.1, 3.7.1): untyped values compare as
 * strings */
static xp3Value *
evalValueComparison(xp3Eval *ev, const xp3AstNode *node) {
    const char *name = operatorName(node->op);
    xp3Atomic *left;
    xp3Atomic *right = NULL;
    xp3ErrCode err;
    int result;

    if (evalAtomic(ev, node->first, name, &left) < 0)
        return(NULL);
    if ((left != NULL) && (evalAtomic(ev, node->last, name, &right) < 0)) {
        xp3AtomicUnref(left);
        return(NULL);
    }
    if ((left == NULL) || (right == NULL)) {
        xp3AtomicUnref(left);
        xp3AtomicUnref(right);
        return(xp3ValueEmpty());
    }
    err = xp3CompareAtomics((xp3AstOp) node->op, left, right, ev->level,
                            &result);
    if (err != XP3_ERR_OK)
        compareError(ev, err, node, left, right);
    xp3AtomicUnref(left);
    xp3AtomicUnref(right);
    return((err == XP3_ERR_OK) ? evalBoolean(ev, result) : NULL);
}

/*
 * The type an untyped value is cast to when compared with another
 * value in a general comparison (XPath 3.1, 3.7.2): xs:double for
 * numbers, xs:string for strings and untyped values, otherwise the
 * primitive type of the other value.
 */
static xp3TypeId
generalTarget(xp3TypeId other) {
    if (xp3TypeIsNumeric(other))
        return(XP3_TYPE_DOUBLE);
    if (xp3TypeDerivesFrom(other, XP3_TYPE_STRING) ||
        (other == XP3_TYPE_ANY_URI) || (other == XP3_TYPE_UNTYPED_ATOMIC))
        return(XP3_TYPE_STRING);
    return(xp3TypePrimitive(other));
}

/*
 * Compare one pair of a general comparison, casting an untyped value
 * to the type of the other. Returns 0 or 1, or -1 on error.
 */
static int
comparePair(xp3Eval *ev, const xp3AstNode *node, xp3Atomic *left,
            xp3Atomic *right) {
    xp3ErrCode err;
    int result = -1;

    xp3AtomicRef(left);
    xp3AtomicRef(right);
    if (left->type == XP3_TYPE_UNTYPED_ATOMIC)
        left = castUntyped(ev, left, generalTarget(right->type),
                           node->first);
    if ((left != NULL) && (right->type == XP3_TYPE_UNTYPED_ATOMIC))
        right = castUntyped(ev, right, generalTarget(left->type),
                            node->last);
    if ((left != NULL) && (right != NULL)) {
        err = xp3CompareAtomics((xp3AstOp) node->op, left, right,
                                ev->level, &result);
        if (err != XP3_ERR_OK) {
            compareError(ev, err, node, left, right);
            result = -1;
        }
    }
    xp3AtomicUnref(left);
    xp3AtomicUnref(right);
    return(result);
}

/* =, !=, <, <=, >, >= (XPath 3.1, 3.7.2): true if any pair of atomized
 * values compares true */
static xp3Value *
evalGeneralComparison(xp3Eval *ev, const xp3AstNode *node) {
    xp3Value *left = evalExpr(ev, node->first);
    xp3Value *right = NULL;
    xp3Iter leftIter;
    xp3Item leftItem;
    int found;
    int result = 0;

    if (left != NULL)
        left = atomize(ev, left, node->first);
    if (left != NULL) {
        right = evalExpr(ev, node->last);
        if (right != NULL)
            right = atomize(ev, right, node->last);
    }
    if ((left == NULL) || (right == NULL)) {
        xp3ValueUnref(left);
        return(NULL);
    }
    /* No pairs: false, without walking the other side */
    if ((xp3ValueCount(left) == 0) || (xp3ValueCount(right) == 0)) {
        xp3ValueUnref(left);
        xp3ValueUnref(right);
        return(evalBoolean(ev, 0));
    }

    xp3IterInit(&leftIter, left);
    while ((result == 0) &&
           ((found = xp3IterNext(&leftIter, &leftItem)) != 0)) {
        xp3Iter rightIter;
        xp3Item rightItem;
        int more;

        if (found < 0) {
            evalNoMemory(ev);
            result = -1;
            break;
        }
        if (evalCount(ev, node) < 0) {
            xp3ItemUnref(leftItem);
            result = -1;
            break;
        }
        xp3IterInit(&rightIter, right);
        while ((result == 0) &&
               ((more = xp3IterNext(&rightIter, &rightItem)) != 0)) {
            if (more < 0) {
                evalNoMemory(ev);
                result = -1;
                break;
            }
            if (evalCount(ev, node) < 0) {
                xp3ItemUnref(rightItem);
                result = -1;
                break;
            }
            result = comparePair(ev, node, leftItem.u.atomic,
                                 rightItem.u.atomic);
            xp3ItemUnref(rightItem);
        }
        xp3ItemUnref(leftItem);
    }
    xp3ValueUnref(left);
    xp3ValueUnref(right);
    return((result < 0) ? NULL : evalBoolean(ev, result));
}

/*
 * The node of an operand of is, << or >>, which must be a single node
 * or empty (XPath 3.1, 3.7.3). Sets *node to NULL for empty. Returns 0
 * or -1.
 */
static int
evalNodeOperand(xp3Eval *ev, const xp3AstNode *expr, const char *name,
                xmlNodePtr *node) {
    xp3Value *value = evalExpr(ev, expr);
    xp3Item item;
    int ret = 0;

    *node = NULL;
    if (value == NULL)
        return(-1);
    if (xp3ValueCount(value) > 1) {
        evalError(ev, XP3_ERR_XPTY0004, expr, "the operand of %s is a "
                  "sequence of more than one item", name);
        ret = -1;
    } else if (xp3ValueCount(value) == 1) {
        if (xp3ValueItemAt(value, 0, &item) <= 0) {
            evalNoMemory(ev);
            ret = -1;
        } else {
            if (item.kind == XP3_ITEM_NODE) {
                *node = item.u.node;
            } else {
                evalError(ev, XP3_ERR_XPTY0004, expr, "the operand of %s "
                          "is not a node", name);
                ret = -1;
            }
            xp3ItemUnref(item);
        }
    }
    xp3ValueUnref(value);
    return(ret);
}

static xp3DocOrder *
evalOrder(xp3Eval *ev) {
    if (ev->order == NULL) {
        ev->order = xp3DocOrderNew();
        if (ev->order == NULL)
            evalNoMemory(ev);
    }
    return(ev->order);
}

/* is, <<, >> */
static xp3Value *
evalNodeComparison(xp3Eval *ev, const xp3AstNode *node) {
    const char *name = operatorName(node->op);
    xmlNodePtr left;
    xmlNodePtr right = NULL;
    xp3ErrCode err;
    int cmp;

    if ((evalNodeOperand(ev, node->first, name, &left) < 0) ||
        ((left != NULL) &&
         (evalNodeOperand(ev, node->last, name, &right) < 0)))
        return(NULL);
    if ((left == NULL) || (right == NULL))
        return(xp3ValueEmpty());
    if (node->op == XP3_OP_IS)
        return(evalBoolean(ev, left == right));
    if (evalOrder(ev) == NULL)
        return(NULL);
    err = xp3DocOrderCompare(ev->order, left, right, &cmp);
    if (err != XP3_ERR_OK) {
        evalNoMemory(ev);
        return(NULL);
    }
    return(evalBoolean(ev, (node->op == XP3_OP_PRECEDES) ? (cmp < 0) :
                                                            (cmp > 0)));
}

/*
 * Ranges and node sets
 */

/* An integer operand of to: empty, or one integer, an untyped value
 * cast to xs:integer (XPath 3.1, 3.4.1). Returns 0 or -1. */
static int
evalRangeBound(xp3Eval *ev, const xp3AstNode *expr, xp3Atomic **bound) {
    if (evalAtomic(ev, expr, "to", bound) < 0)
        return(-1);
    if (*bound == NULL)
        return(0);
    if ((*bound)->type == XP3_TYPE_UNTYPED_ATOMIC) {
        *bound = castUntyped(ev, *bound, XP3_TYPE_INTEGER, expr);
        return((*bound != NULL) ? 0 : -1);
    }
    if (!xp3TypeIsInteger((*bound)->type)) {
        evalError(ev, XP3_ERR_XPTY0004, expr, "the operand of to is an "
                  "xs:%s, not an integer", typeName((*bound)->type));
        xp3AtomicUnref(*bound);
        *bound = NULL;
        return(-1);
    }
    return(0);
}

/*
 * A range with a bound outside 64 bits, which ranges can't store: its
 * integers one by one, if there are no more than a sequence may have
 * (doc/DESIGN.md 4.1).
 */
static xp3Value *
expandRange(xp3Eval *ev, const xp3AstNode *node, const xp3Decimal *first,
            const xp3Decimal *last) {
    xp3SeqBuilder builder;
    xp3Decimal *one = xp3DecimalFromInt64(1);
    xp3Decimal *span = NULL;
    xp3Decimal *current = NULL;
    xp3ErrCode err;
    int64_t count;

    builderInit(ev, &builder);
    if (one == NULL) {
        evalNoMemory(ev);
        return(NULL);
    }
    err = xp3DecimalSub(last, first, &span);
    if (err != XP3_ERR_OK)
        goto error;
    if ((xp3DecimalToInt64(span, &count) < 0) ||
        ((uint64_t) count >= ev->maxItems)) {
        evalBuildError(ev, XP3_ERR_LIMIT, node);
        goto done;
    }
    err = xp3DecimalCopy(first, &current);
    while ((err == XP3_ERR_OK) && (count-- >= 0)) {
        xp3Decimal *copy;
        xp3Decimal *next;
        xp3Atomic *atomic;

        if (evalCount(ev, node) < 0)
            goto done;
        err = xp3DecimalCopy(current, &copy);
        if (err != XP3_ERR_OK)
            break;
        atomic = xp3AtomicNewDecimal(XP3_TYPE_INTEGER, copy);
        if (atomic == NULL) {
            err = XP3_ERR_NO_MEMORY;
            break;
        }
        err = xp3SeqBuilderAdd(&builder, xp3ItemFromAtomic(atomic));
        if (err != XP3_ERR_OK)
            break;
        err = xp3DecimalAdd(current, one, &next);
        if (err != XP3_ERR_OK)
            break;
        xp3DecimalFree(current);
        current = next;
    }

error:
    if (err != XP3_ERR_OK)
        evalBuildError(ev, err, node);
done:
    xp3DecimalFree(one);
    xp3DecimalFree(span);
    xp3DecimalFree(current);
    if (ev->err != NULL) {
        xp3SeqBuilderClear(&builder);
        return(NULL);
    }
    return(builderFinish(ev, &builder));
}

static xp3Value *
evalRange(xp3Eval *ev, const xp3AstNode *node) {
    xp3Atomic *first;
    xp3Atomic *last = NULL;
    xp3Value *result = NULL;
    int64_t from;
    int64_t to;

    if (evalRangeBound(ev, node->first, &first) < 0)
        return(NULL);
    if ((first != NULL) && (evalRangeBound(ev, node->last, &last) < 0)) {
        xp3AtomicUnref(first);
        return(NULL);
    }
    if ((first == NULL) || (last == NULL) ||
        (xp3DecimalCompare(first->u.dec, last->u.dec) > 0)) {
        result = xp3ValueEmpty();
    } else if ((xp3DecimalToInt64(first->u.dec, &from) < 0) ||
               (xp3DecimalToInt64(last->u.dec, &to) < 0)) {
        result = expandRange(ev, node, first->u.dec, last->u.dec);
    } else {
        xp3ErrCode err = xp3ValueRange(from, to, &result);

        if (err != XP3_ERR_OK) {
            result = NULL;
            if (err == XP3_ERR_LIMIT)
                evalError(ev, err, node, "the range is too long");
            else
                evalNoMemory(ev);
        }
    }
    xp3AtomicUnref(first);
    xp3AtomicUnref(last);
    return(result);
}

/* The nodes of an operand of union, intersect or except, in document
 * order without duplicates. Returns 0 or -1. */
static int
evalNodeSet(xp3Eval *ev, const xp3AstNode *expr, const char *name,
            xp3SeqBuilder *builder) {
    xp3Value *value = evalExpr(ev, expr);
    xp3ErrCode err;

    if (value == NULL)
        return(-1);
    if (!isAllNodes(value)) {
        evalError(ev, XP3_ERR_XPTY0004, expr, "an operand of %s contains "
                  "an item that is not a node", name);
        xp3ValueUnref(value);
        return(-1);
    }
    err = xp3SeqBuilderAddValue(builder, value);
    xp3ValueUnref(value);
    if (err == XP3_ERR_OK)
        err = evalSortNodes(ev, builder);
    if (err != XP3_ERR_OK) {
        if (err == XP3_ERR_NO_MEMORY)
            evalNoMemory(ev);
        else
            evalBuildError(ev, err, expr);
        return(-1);
    }
    return(0);
}

/* union, intersect, except (XPath 3.1, 3.4.2) */
static xp3Value *
evalSetOperation(xp3Eval *ev, const xp3AstNode *node) {
    const char *name = operatorName(node->op);
    xp3SeqBuilder left;
    xp3SeqBuilder right;
    size_t leftPos = 0;
    size_t rightPos = 0;
    size_t kept = 0;

    builderInit(ev, &left);
    builderInit(ev, &right);
    if ((evalNodeSet(ev, node->first, name, &left) < 0) ||
        (evalNodeSet(ev, node->last, name, &right) < 0))
        goto error;

    if (node->op == XP3_OP_UNION) {
        size_t i;

        for (i = 0; i < right.count; i++) {
            xp3ErrCode err = xp3SeqBuilderAdd(&left, right.items[i]);

            if (err != XP3_ERR_OK) {
                evalBuildError(ev, err, node);
                goto error;
            }
        }
        right.count = 0;
        xp3SeqBuilderClear(&right);
        if (evalSortNodes(ev, &left) != XP3_ERR_OK) {
            evalNoMemory(ev);
            goto error;
        }
        return(builderFinish(ev, &left));
    }

    /* Walk both in document order, keeping the left nodes that are in
     * the right operand (intersect) or not (except) */
    while (leftPos < left.count) {
        int cmp = 1;

        if (rightPos < right.count) {
            xp3ErrCode err = xp3DocOrderCompare(ev->order,
                                                left.items[leftPos].u.node,
                                                right.items[rightPos].u.node,
                                                &cmp);

            if (err != XP3_ERR_OK) {
                evalNoMemory(ev);
                goto error;
            }
        } else {
            cmp = -1;
        }
        if (cmp > 0) {
            rightPos++;
            continue;
        }
        if ((cmp == 0) == (node->op == XP3_OP_INTERSECT))
            left.items[kept++] = left.items[leftPos];
        leftPos++;
        if (cmp == 0)
            rightPos++;
    }
    left.count = kept;
    xp3SeqBuilderClear(&right);
    return(builderFinish(ev, &left));

error:
    xp3SeqBuilderClear(&left);
    xp3SeqBuilderClear(&right);
    return(NULL);
}

/*
 * Types
 */

/* Whether an atomic value is an instance of a type an ATOMIC_TYPE node
 * names: an atomic type, or the union xs:numeric */
static int
atomicMatches(const xp3Atomic *atomic, int type) {
    if (type == XP3_SCHEMA_NUMERIC)
        return(xp3TypeIsNumeric(atomic->type));
    if ((type < 0) || (type >= XP3_TYPE_COUNT))
        return(0);
    return(xp3TypeDerivesFrom(atomic->type, (xp3TypeId) type));
}

/* Whether an item matches an item type (XPath 3.1, 2.5.5.2) */
static int
itemMatches(xp3Item item, const xp3AstNode *type) {
    switch (type->kind) {
        case XP3_AST_ANY_ITEM_TYPE:
            return(1);
        case XP3_AST_ATOMIC_TYPE:
            return((item.kind == XP3_ITEM_ATOMIC) &&
                   atomicMatches(item.u.atomic, type->op));
        case XP3_AST_KIND_TEST:
            return((item.kind == XP3_ITEM_NODE) &&
                   matchesKind(type, item.u.node));
        default:
            /* function, map and array tests: no such items yet */
            return(0);
    }
}

/*
 * Whether a sequence matches a sequence type (XPath 3.1, 2.5.5): its
 * length fits the occurrence indicator and every item the item type.
 * Returns 0 or 1, or -1 on error.
 */
static int
sequenceMatches(xp3Eval *ev, const xp3Value *value,
                const xp3AstNode *seqType, const xp3AstNode *node) {
    uint64_t count = xp3ValueCount(value);
    xp3Iter iter;
    xp3Item item;
    int found;
    int ret = 1;

    if (seqType->kind == XP3_AST_EMPTY_SEQUENCE_TYPE)
        return(count == 0);
    switch (seqType->op) {
        case XP3_OCC_ONE:
            if (count != 1)
                return(0);
            break;
        case XP3_OCC_OPTIONAL:
            if (count > 1)
                return(0);
            break;
        case XP3_OCC_ONE_OR_MORE:
            if (count == 0)
                return(0);
            break;
        default:
            break;
    }
    /* A range holds integers only: its first item stands for all */
    if (value->shape == XP3_SEQ_RANGE) {
        found = xp3ValueItemAt(value, 0, &item);
        if (found < 0) {
            evalNoMemory(ev);
            return(-1);
        }
        ret = itemMatches(item, seqType->first);
        xp3ItemUnref(item);
        return(ret);
    }
    xp3IterInit(&iter, value);
    while ((ret == 1) && ((found = xp3IterNext(&iter, &item)) != 0)) {
        if (found < 0) {
            evalNoMemory(ev);
            return(-1);
        }
        if (evalCount(ev, node) < 0) {
            xp3ItemUnref(item);
            return(-1);
        }
        ret = itemMatches(item, seqType->first);
        xp3ItemUnref(item);
    }
    return(ret);
}

/* instance of (XPath 3.1, 3.14.1) */
static xp3Value *
evalInstanceOf(xp3Eval *ev, const xp3AstNode *node) {
    xp3Value *value = evalExpr(ev, node->first);
    int result;

    if (value == NULL)
        return(NULL);
    result = sequenceMatches(ev, value, node->last, node);
    xp3ValueUnref(value);
    return((result < 0) ? NULL : evalBoolean(ev, result));
}

/* treat as (XPath 3.1, 3.14.5): the value, if it matches the type */
static xp3Value *
evalTreatAs(xp3Eval *ev, const xp3AstNode *node) {
    xp3Value *value = evalExpr(ev, node->first);
    int result;

    if (value == NULL)
        return(NULL);
    result = sequenceMatches(ev, value, node->last, node);
    if (result == 1)
        return(value);
    if (result == 0)
        evalError(ev, XP3_ERR_XPDY0050, node, "the value does not match "
                  "the type of treat as");
    xp3ValueUnref(value);
    return(NULL);
}

/*
 * Cast one atomic value to the target of a cast: an atomic type, the
 * union xs:numeric, xs:error, or a list type (F&O 3.1, 19.3.5 and
 * 19.3.6). Adds the result to builder. Returns an error code; the
 * caller decides what it means.
 */
static xp3ErrCode
castToTarget(const xp3Atomic *value, int target, xp3SeqBuilder *builder) {
    xp3Atomic *result = NULL;
    xp3ErrCode err;

    switch (target) {
        case XP3_SCHEMA_NUMERIC: {
            static const xp3TypeId members[] = {
                XP3_TYPE_DOUBLE, XP3_TYPE_FLOAT, XP3_TYPE_DECIMAL
            };
            size_t i;

            if (xp3TypeIsNumeric(value->type))
                return(xp3SeqBuilderAdd(builder, xp3ItemRef(
                    xp3ItemFromAtomic((xp3Atomic *) value))));
            err = XP3_ERR_FORG0001;
            for (i = 0; i < sizeof(members) / sizeof(members[0]); i++) {
                err = xp3CastAtomic(value, members[i], &result);
                if ((err != XP3_ERR_FORG0001) && (err != XP3_ERR_XPTY0004))
                    break;
            }
            break;
        }
        case XP3_SCHEMA_ERROR:
            return(XP3_ERR_FORG0001);
        case XP3_SCHEMA_NMTOKENS:
        case XP3_SCHEMA_IDREFS:
        case XP3_SCHEMA_ENTITIES: {
            xp3TypeId itemType = (target == XP3_SCHEMA_NMTOKENS) ?
                                 XP3_TYPE_NMTOKEN :
                                 (target == XP3_SCHEMA_IDREFS) ?
                                 XP3_TYPE_IDREF : XP3_TYPE_ENTITY;
            const xmlChar *cur;
            size_t tokens = 0;

            if (!xp3TypeDerivesFrom(value->type, XP3_TYPE_STRING) &&
                (value->type != XP3_TYPE_UNTYPED_ATOMIC))
                return(XP3_ERR_XPTY0004);
            for (cur = value->u.str; *cur != 0;) {
                const xmlChar *end;
                xp3Atomic *token;
                xp3Atomic *typed;

                while ((*cur == ' ') || (*cur == '\t') || (*cur == '\n') ||
                       (*cur == '\r'))
                    cur++;
                if (*cur == 0)
                    break;
                for (end = cur; (*end != 0) && (*end != ' ') &&
                                (*end != '\t') && (*end != '\n') &&
                                (*end != '\r'); end++)
                    ;
                token = xp3AtomicNewString(XP3_TYPE_STRING, cur,
                                           (size_t) (end - cur));
                if (token == NULL)
                    return(XP3_ERR_NO_MEMORY);
                err = xp3CastAtomic(token, itemType, &typed);
                xp3AtomicUnref(token);
                if (err != XP3_ERR_OK)
                    return(err);
                err = xp3SeqBuilderAdd(builder, xp3ItemFromAtomic(typed));
                if (err != XP3_ERR_OK)
                    return(err);
                tokens++;
                cur = end;
            }
            /* List types have at least one item */
            return((tokens > 0) ? XP3_ERR_OK : XP3_ERR_FORG0001);
        }
        default:
            err = xp3CastAtomic(value, (xp3TypeId) target, &result);
            break;
    }
    if (err != XP3_ERR_OK)
        return(err);
    return(xp3SeqBuilderAdd(builder, xp3ItemFromAtomic(result)));
}

/*
 * cast as and castable as (XPath 3.1, 3.14.2 and 3.14.3). For cast,
 * the result or an error; for castable, true or false.
 */
static xp3Value *
evalCast(xp3Eval *ev, const xp3AstNode *node) {
    int castable = (node->kind == XP3_AST_CASTABLE_AS);
    const xp3AstNode *seqType = node->last;
    const xp3AstNode *target = seqType->first;
    int optional = (seqType->op == XP3_OCC_OPTIONAL);
    char name[XP3_NAME_BUF];
    xp3SeqBuilder builder;
    xp3Value *value;
    xp3Item item;
    xp3ErrCode err;

    value = evalExpr(ev, node->first);
    if (value != NULL)
        value = atomize(ev, value, node->first);
    if (value == NULL)
        return(NULL);
    if (xp3ValueCount(value) != 1) {
        int empty = (xp3ValueCount(value) == 0);

        xp3ValueUnref(value);
        if (castable)
            return(evalBoolean(ev, empty && optional));
        if (empty && optional)
            return(xp3ValueEmpty());
        evalError(ev, XP3_ERR_XPTY0004, node->first, "%s can't be cast "
                  "to %s", empty ? "the empty sequence" :
                  "a sequence of more than one item",
                  displayName(target, name, sizeof(name)));
        return(NULL);
    }
    if (xp3ValueItemAt(value, 0, &item) <= 0) {
        xp3ValueUnref(value);
        evalNoMemory(ev);
        return(NULL);
    }
    xp3ValueUnref(value);

    builderInit(ev, &builder);
    err = castToTarget(item.u.atomic, target->op, &builder);
    if (castable && ((err == XP3_ERR_OK) || (err == XP3_ERR_FORG0001) ||
                     (err == XP3_ERR_XPTY0004) || (err == XP3_ERR_FOCA0001) ||
                     (err == XP3_ERR_FOCA0002) || (err == XP3_ERR_FOCA0003))) {
        xp3SeqBuilderClear(&builder);
        xp3ItemUnref(item);
        return(evalBoolean(ev, err == XP3_ERR_OK));
    }
    switch (err) {
        case XP3_ERR_OK:
            break;
        case XP3_ERR_FORG0001:
            if (xp3TypeRepresentation(item.u.atomic->type) == XP3_REP_STRING)
                evalError(ev, err, node, "\"%.40s\" is not a valid %s",
                          (const char *) item.u.atomic->u.str,
                          displayName(target, name, sizeof(name)));
            else
                evalError(ev, err, node, "the xs:%s value is out of the "
                          "range of %s", typeName(item.u.atomic->type),
                          displayName(target, name, sizeof(name)));
            break;
        case XP3_ERR_XPTY0004:
            evalError(ev, err, node, "xs:%s can't be cast to %s",
                      typeName(item.u.atomic->type),
                      displayName(target, name, sizeof(name)));
            break;
        case XP3_ERR_FOCA0001:
        case XP3_ERR_FOCA0003:
            evalError(ev, err, node, "the value is too large for %s",
                      displayName(target, name, sizeof(name)));
            break;
        case XP3_ERR_FOCA0002:
            evalError(ev, err, node, "NaN and infinity can't be cast to %s",
                      displayName(target, name, sizeof(name)));
            break;
        case XP3_ERR_UNSUPPORTED:
            evalError(ev, err, node, "casting xs:%s to %s is not "
                      "implemented yet", typeName(item.u.atomic->type),
                      displayName(target, name, sizeof(name)));
            break;
        default:
            evalBuildError(ev, err, node);
            break;
    }
    xp3ItemUnref(item);
    if (err != XP3_ERR_OK) {
        xp3SeqBuilderClear(&builder);
        return(NULL);
    }
    return(builderFinish(ev, &builder));
}

/* Binary operators other than and and or */
static xp3Value *
evalBinary(xp3Eval *ev, const xp3AstNode *node) {
    switch (node->op) {
        case XP3_OP_OR:
        case XP3_OP_AND:
            return(evalLogical(ev, node));
        case XP3_OP_GEN_EQ:
        case XP3_OP_GEN_NE:
        case XP3_OP_GEN_LT:
        case XP3_OP_GEN_LE:
        case XP3_OP_GEN_GT:
        case XP3_OP_GEN_GE:
            return(evalGeneralComparison(ev, node));
        case XP3_OP_VAL_EQ:
        case XP3_OP_VAL_NE:
        case XP3_OP_VAL_LT:
        case XP3_OP_VAL_LE:
        case XP3_OP_VAL_GT:
        case XP3_OP_VAL_GE:
            return(evalValueComparison(ev, node));
        case XP3_OP_IS:
        case XP3_OP_PRECEDES:
        case XP3_OP_FOLLOWS:
            return(evalNodeComparison(ev, node));
        case XP3_OP_RANGE:
            return(evalRange(ev, node));
        case XP3_OP_ADD:
        case XP3_OP_SUB:
        case XP3_OP_MUL:
        case XP3_OP_DIV:
        case XP3_OP_IDIV:
        case XP3_OP_MOD:
            return(evalArithmetic(ev, node));
        case XP3_OP_UNION:
        case XP3_OP_INTERSECT:
        case XP3_OP_EXCEPT:
            return(evalSetOperation(ev, node));
        default:
            /* || and ! come with Part 12 */
            return(evalUnsupported(ev, node));
    }
}

/*
 * Dispatch
 */

/*
 * While folding, check the size of a single atomic value an expression
 * made; sequences of more items were checked by their builders. A
 * constant, a variable or the context item only returns a value made
 * and checked before. Returns 0, or -1 if the value is larger than
 * ev->maxBytes.
 */
static int
evalCheckSize(xp3Eval *ev, const xp3AstNode *node, const xp3Value *value) {
    size_t size;

    if ((node->kind == XP3_AST_CONSTANT) || (node->kind == XP3_AST_VARREF) ||
        (node->kind == XP3_AST_CONTEXT_ITEM) ||
        (value->shape != XP3_SEQ_SINGLE) ||
        (value->u.single.kind != XP3_ITEM_ATOMIC))
        return(0);
    size = xp3AtomicSize(value->u.single.u.atomic, ev->maxBytes);
    if (size > ev->maxBytes) {
        evalError(ev, XP3_ERR_LIMIT, node, "a value would have more than "
                  "%lu bytes", (unsigned long) ev->maxBytes);
        return(-1);
    }
    return(evalSteps(ev, node, size / XP3_BYTES_PER_STEP));
}

static xp3Value *
evalNode(xp3Eval *ev, const xp3AstNode *node);

static xp3Value *
evalExpr(xp3Eval *ev, const xp3AstNode *node) {
    xp3Value *value = evalNode(ev, node);

    if ((value != NULL) && (ev->maxBytes > 0) &&
        (evalCheckSize(ev, node, value) < 0)) {
        xp3ValueUnref(value);
        return(NULL);
    }
    return(value);
}

static xp3Value *
evalNode(xp3Eval *ev, const xp3AstNode *node) {
    if (evalCount(ev, node) < 0)
        return(NULL);
    switch (node->kind) {
        case XP3_AST_CONSTANT:
            /* A constant's count never changes, so this writes nothing.
             * The caller's limit holds for constants too. */
            if ((node->value->shape == XP3_SEQ_ARRAY) &&
                (node->value->u.array.count > ev->maxItems)) {
                evalBuildError(ev, XP3_ERR_LIMIT, node);
                return(NULL);
            }
            return(xp3ValueRef(node->value));
        case XP3_AST_INTEGER:
        case XP3_AST_DECIMAL:
        case XP3_AST_DOUBLE:
        case XP3_AST_STRING:
            return(evalLiteral(ev, node));
        case XP3_AST_VARREF:
            return(evalVarRef(ev, node));
        case XP3_AST_CONTEXT_ITEM:
            return(evalContextItem(ev, node));
        case XP3_AST_SEQUENCE:
            return(evalSequence(ev, node));
        case XP3_AST_EMPTY_SEQUENCE:
            return(xp3ValueEmpty());
        case XP3_AST_FOR:
            return(evalFor(ev, node));
        case XP3_AST_LET:
            return(evalLet(ev, node));
        case XP3_AST_SOME:
        case XP3_AST_EVERY:
            return(evalQuantified(ev, node));
        case XP3_AST_IF:
            return(evalIf(ev, node));
        case XP3_AST_BINARY:
            return(evalBinary(ev, node));
        case XP3_AST_UNARY:
            return(evalUnary(ev, node));
        case XP3_AST_CAST_AS:
        case XP3_AST_CASTABLE_AS:
            return(evalCast(ev, node));
        case XP3_AST_INSTANCE_OF:
            return(evalInstanceOf(ev, node));
        case XP3_AST_TREAT_AS:
            return(evalTreatAs(ev, node));
        case XP3_AST_PATH:
            return(evalPath(ev, node));
        case XP3_AST_STEP:
            return(evalStep(ev, node));
        case XP3_AST_FILTER:
            return(evalFilter(ev, node));
        default:
            return(evalUnsupported(ev, node));
    }
}

/*
 * Evaluating
 */

/* Bind the declared variables to the values in the dynamic context,
 * and set the focus to its context node. Returns 0 or -1. */
static int
evalSetup(xp3Eval *ev, const xp3Expr *expr, const xp3DynamicContext *ctx) {
    int i;

    ev->maxItems = (ctx != NULL) ? ctx->maxSequenceLength :
                                   XP3_MAX_SEQUENCE_ITEMS;
    ev->maxOperations = (ctx != NULL) ? ctx->maxOperations : 0;
    ev->level = expr->level;
    if ((ctx != NULL) && (ctx->contextNode != NULL)) {
        ev->focus.item = xp3ItemFromNode(ctx->contextNode);
        ev->focus.present = 1;
        ev->focus.position = 1;
        ev->focus.size = 1;
    }
    if (expr->slotCount > 0) {
        ev->slots = xmlMalloc((size_t) expr->slotCount * sizeof(xp3Value *));
        if (ev->slots == NULL) {
            evalNoMemory(ev);
            return(-1);
        }
        memset(ev->slots, 0, (size_t) expr->slotCount * sizeof(xp3Value *));
        ev->slotCount = expr->slotCount;
    }

    for (i = 0; i < expr->variableCount; i++) {
        const xp3ExprVariable *var = &expr->variables[i];
        int j;

        for (j = 0; (ctx != NULL) && (j < ctx->variableCount); j++) {
            const xp3BoundVariable *bound = &ctx->variables[j];

            if (xmlStrEqual(bound->uri, var->uri) &&
                xmlStrEqual(bound->localName, var->localName)) {
                ev->slots[var->slot] = xp3ValueRef(bound->value);
                break;
            }
        }
        if (ev->slots[var->slot] == NULL) {
            if (var->uri != NULL)
                evalError(ev, XP3_ERR_XPDY0002, NULL, "no value is bound "
                          "to the variable $Q{%s}%s",
                          (const char *) var->uri,
                          (const char *) var->localName);
            else
                evalError(ev, XP3_ERR_XPDY0002, NULL, "no value is bound "
                          "to the variable $%s",
                          (const char *) var->localName);
            return(-1);
        }
    }
    return(0);
}

static void
evalCleanup(xp3Eval *ev) {
    int i;

    for (i = 0; i < ev->slotCount; i++)
        xp3ValueUnref(ev->slots[i]);
    xmlFree(ev->slots);
    xp3DocOrderFree(ev->order);
}

/**
 * Evaluate a compiled expression.
 *
 * The dynamic context gives the context node and the values of the
 * variables the expression was compiled with; each of those variables
 * needs a value. Node identity and document order are those of the
 * libxml2 tree as it is during the evaluation, which must not change
 * it. The documents must outlive the result, which refers to their
 * nodes.
 *
 * Threads can evaluate one compiled expression at the same time, each
 * with a dynamic context of its own: contexts and sequences are
 * reference counted without locks, so each belongs to one thread at a
 * time.
 *
 * @param expr   the compiled expression
 * @param ctx    the dynamic context, or NULL for none: no context item
 *               and no variables
 * @param error  where to store the error, or NULL. It is set to NULL
 *               on success, and must be freed with xp3ErrorFree()
 *               otherwise.
 * @returns the result, to be freed with xp3FreeSequence(), or NULL on
 *          error or if expr is NULL
 */
xp3Sequence *
xp3Evaluate(const xp3Expr *expr, xp3DynamicContext *ctx, xp3Error **error) {
    xp3Value *result = NULL;
    xp3Eval ev;

    if (error != NULL)
        *error = NULL;
    if (expr == NULL)
        return(NULL);
    memset(&ev, 0, sizeof(ev));
    if (evalSetup(&ev, expr, ctx) == 0)
        result = evalExpr(&ev, expr->ast->root);
    evalCleanup(&ev);

    /* The result outlives the expression, its constants included */
    if (result != NULL) {
        result = xp3ValueDetach(result);
        if (result == NULL)
            evalNoMemory(&ev);
    }

    if ((result == NULL) && (ev.err == NULL))
        evalNoMemory(&ev);
    if (result != NULL) {
        xp3ErrorFree(ev.err);
    } else if (error != NULL) {
        *error = ev.err;
    } else {
        xp3ErrorFree(ev.err);
    }
    return(result);
}

/**
 * Evaluate a compiled expression to its effective boolean value
 * (XPath 3.1, 2.4.3), as the test of an if expression does: false for
 * the empty sequence, true for nodes, and the boolean value of a
 * single boolean, string or number.
 *
 * @param expr   the compiled expression
 * @param ctx    the dynamic context, or NULL for none
 * @param error  where to store the error, or NULL, as for xp3Evaluate()
 * @returns 1 for true, 0 for false, or -1 on error, such as FORG0006
 *          for a sequence without an effective boolean value
 */
int
xp3EvaluateBoolean(const xp3Expr *expr, xp3DynamicContext *ctx,
                   xp3Error **error) {
    xp3Value *value = xp3Evaluate(expr, ctx, error);
    const xp3AstNode *root;
    xp3ErrCode err;
    int result;

    if (value == NULL)
        return(-1);
    err = xp3EffectiveBoolean(value, &result);
    xp3ValueUnref(value);
    if (err == XP3_ERR_OK)
        return(result);
    if (error != NULL) {
        root = expr->ast->root;
        if (err == XP3_ERR_FORG0006)
            *error = xp3ErrorNew(err, root->line, root->column,
                                 "the result has no effective boolean "
                                 "value");
        else
            *error = xp3ErrorNoMemory();
    }
    return(-1);
}

/**
 * Evaluate a subexpression on its own, as constant folding does: no
 * focus, no dynamic context, and a budget of steps and items. The
 * subexpression may bind variables of its own, which need slots: the
 * caller lends an array of them, all NULL, and gets it back so.
 *
 * @param node           the subexpression
 * @param slots          the slots of the whole expression, all NULL
 * @param slotCount      their number
 * @param level          the language level
 * @param maxOperations  the most steps, see evalCount()
 * @param maxItems       the most items of any sequence built
 * @param maxBytes       the most bytes of any value made, see
 *                       xp3AtomicSize(); 0 for no limit
 * @param value          set to the value, with one reference
 * @param operations     set to the steps taken
 * @returns XP3_ERR_OK, or the code of the error evaluating raised
 */
xp3ErrCode
xp3EvalConstant(const xp3AstNode *node, xp3Value **slots, int slotCount,
                xp3LanguageLevel level, uint64_t maxOperations,
                size_t maxItems, size_t maxBytes, xp3Value **value,
                uint64_t *operations) {
    xp3ErrCode ret = XP3_ERR_OK;
    xp3Eval ev;

    *value = NULL;
    memset(&ev, 0, sizeof(ev));
    ev.level = level;
    ev.maxItems = maxItems;
    ev.maxBytes = maxBytes;
    ev.maxOperations = maxOperations;
    ev.slots = slots;
    ev.slotCount = slotCount;
    *value = evalExpr(&ev, node);
    *operations = ev.operations;
    if (*value == NULL)
        ret = (ev.err != NULL) ? ev.err->code : XP3_ERR_NO_MEMORY;

    /* The slots are lent: bindings restored them, and they stay */
    ev.slots = NULL;
    ev.slotCount = 0;
    evalCleanup(&ev);
    xp3ErrorFree(ev.err);
    return(ret);
}

/*
 * Sequences
 */

/**
 * Create a sequence of nodes, in the order given. Any libxml2 node of
 * a run of adjacent text nodes stands for the whole run.
 *
 * @param nodes  the nodes, may be NULL if count is 0
 * @param count  the number of nodes
 * @returns the sequence, to be freed with xp3FreeSequence(), or NULL
 *          if a node is not part of the XDM tree, there are more than
 *          10 million nodes, or memory allocation failed
 */
xp3Sequence *
xp3NewNodeSequence(xmlNodePtr *nodes, size_t count) {
    xp3SeqBuilder builder;
    size_t i;

    if ((nodes == NULL) && (count > 0))
        return(NULL);
    xp3SeqBuilderInit(&builder);
    for (i = 0; i < count; i++) {
        xmlNodePtr canonical = xp3NodeCanonical(nodes[i]);

        if ((canonical == NULL) ||
            (xp3SeqBuilderAdd(&builder, xp3ItemFromNode(canonical)) !=
             XP3_ERR_OK)) {
            xp3SeqBuilderClear(&builder);
            return(NULL);
        }
    }
    return(xp3SeqBuilderFinish(&builder));
}

/**
 * Create a sequence of one xs:string.
 *
 * @param str  the string in UTF-8
 * @returns the sequence, to be freed with xp3FreeSequence(), or NULL
 *          if str is NULL or not valid UTF-8, or memory allocation
 *          failed
 */
xp3Sequence *
xp3NewStringSequence(const xmlChar *str) {
    xp3Atomic *atomic;

    if ((str == NULL) || (!xmlCheckUTF8(str)))
        return(NULL);
    atomic = xp3AtomicNewString(XP3_TYPE_STRING, str,
                                strlen((const char *) str));
    if (atomic == NULL)
        return(NULL);
    return(xp3ValueSingle(xp3ItemFromAtomic(atomic)));
}

/**
 * Release a sequence. NULL is ignored.
 *
 * @param seq  the sequence
 */
void
xp3FreeSequence(xp3Sequence *seq) {
    xp3ValueUnref(seq);
}

/**
 * Get the number of items in a sequence.
 *
 * @param seq  the sequence
 * @returns the number of items, 0 for NULL. A range of integers too
 *          long for size_t, as (1 to 9999999999) on a 32-bit system,
 *          gives SIZE_MAX.
 */
size_t
xp3SequenceLength(const xp3Sequence *seq) {
    uint64_t count = xp3ValueCount(seq);

    return((count > SIZE_MAX) ? SIZE_MAX : (size_t) count);
}

/**
 * Get the kind of an item.
 *
 * @param seq    the sequence
 * @param index  the 0-based position of the item
 * @returns an xp3ItemKind, or -1 if there is no such item
 */
int
xp3SequenceItemKind(const xp3Sequence *seq, size_t index) {
    xp3Item item;
    int found;

    if (index >= xp3ValueCount(seq))
        return(-1);
    if (seq->shape == XP3_SEQ_RANGE)
        return(XP3_ITEM_ATOMIC);
    found = xp3ValueItemAt(seq, index, &item);
    if (found <= 0)
        return(-1);
    xp3ItemUnref(item);
    return(item.kind);
}

/**
 * Get a node of a sequence.
 *
 * @param seq    the sequence
 * @param index  the 0-based position of the item
 * @returns the node, or NULL if the item is not a node or there is no
 *          such item
 */
xmlNodePtr
xp3SequenceNode(const xp3Sequence *seq, size_t index) {
    xp3Item item;

    if ((xp3SequenceItemKind(seq, index) != XP3_ITEM_NODE) ||
        (xp3ValueItemAt(seq, index, &item) <= 0))
        return(NULL);
    return(item.u.node);
}

/**
 * Get the string value of an item: of a node (XDM 3.1, 5.12), or of
 * an atomic value as cast to xs:string.
 *
 * @param seq    the sequence
 * @param index  the 0-based position of the item
 * @returns the string, to be freed with xmlFree(), or NULL if there is
 *          no such item, it is a function, map or array, or memory
 *          allocation failed
 */
xmlChar *
xp3SequenceStringValue(const xp3Sequence *seq, size_t index) {
    xmlChar *str = NULL;
    xp3Item item;

    if ((index >= xp3ValueCount(seq)) ||
        (xp3ValueItemAt(seq, index, &item) <= 0))
        return(NULL);
    if (item.kind == XP3_ITEM_NODE) {
        if (xp3NodeStringValue(item.u.node, &str, NULL) != XP3_ERR_OK)
            str = NULL;
    } else if (item.kind == XP3_ITEM_ATOMIC) {
        str = xp3AtomicToString(item.u.atomic);
    }
    xp3ItemUnref(item);
    return(str);
}
