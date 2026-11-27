/*
 * 06-extension-functions.c: C functions called from XPath
 *
 * The program of doc/tutorial/06-extension-functions.md. Run it with
 * the directory of the example documents:
 *
 *     06-extension-functions examples/data
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/tree.h>
#include <libxml/xmlmemory.h>
#include <libxpath3/xpath3.h>

#include "common.h"

#define INV_NS "urn:example:inventory"
#define LABEL_NS "urn:example:labels"

/* How often each function ran, through its registration's user data */
typedef struct {
    int parsePrice;
    int restock;
    int decimalPlaces;
    int unitsIn;
    int stock;
} Calls;

/* The stock of one warehouse, given to an evaluation as its user data */
typedef struct {
    const char *sku;
    int64_t level;
} StockLevel;

typedef struct {
    const StockLevel *levels;
    size_t count;
} Warehouse;

/*
 * Return the empty sequence. Any constructor makes one from no values.
 */
static xp3Sequence *
emptySequence(void) {
    return(xp3NewIntegerSequence(NULL, 0));
}

/*
 * inv:parse-price($raw as xs:string?) as xs:decimal?
 *
 * Reads a price as people write it: spaces and thousands separators
 * are dropped. Nothing, or nothing but spaces, gives the empty
 * sequence. Anything else that isn't a decimal raises inv:BADPRICE.
 */
static xp3Sequence *
parsePrice(xp3FunctionCall *call, xp3Sequence **args, int count,
           void *userData) {
    Calls *calls = userData;
    xp3Sequence *result;
    xp3Error *error = NULL;
    xmlChar *raw;
    xmlChar *clean;
    const xmlChar *cur;
    size_t length = 0;

    (void) count;
    calls->parsePrice++;

    /* xs:string? : the empty sequence or one string */
    if (xp3SequenceLength(args[0]) == 0)
        return(emptySequence());
    raw = xp3SequenceStringValue(args[0], 0);
    if (raw == NULL)
        return(NULL);
    clean = xmlMalloc(xmlStrlen(raw) + 1);
    if (clean == NULL) {
        xmlFree(raw);
        return(NULL);
    }
    for (cur = raw; *cur != 0; cur++) {
        if ((*cur != ' ') && (*cur != ','))
            clean[length++] = *cur;
    }
    clean[length] = 0;

    if (length == 0) {
        result = emptySequence();
    } else {
        const xmlChar *lexical = clean;

        result = xp3NewAtomicSequence(XP3_TYPE_DECIMAL, &lexical, 1, &error);
        if ((result == NULL) && (error != NULL) &&
            (!xmlStrEqual(xp3ErrorCode(error), BAD_CAST "NO_MEMORY")))
            xp3FunctionError(call, BAD_CAST INV_NS, BAD_CAST "BADPRICE",
                             BAD_CAST "not a decimal number");
        xp3ErrorFree(error);
    }

    xmlFree(clean);
    xmlFree(raw);
    return(result);
}

/*
 * inv:restock($qty as xs:integer) as xs:integer
 * inv:restock($qty as xs:integer, $minimum as xs:integer) as xs:integer
 *
 * How many to order to have at least the minimum, 5 by default.
 */
static xp3Sequence *
restock(xp3FunctionCall *call, xp3Sequence **args, int count,
        void *userData) {
    Calls *calls = userData;
    int64_t quantity;
    int64_t minimum = 5;
    int64_t order = 0;
    int fits;

    calls->restock++;
    if ((count == 2) && (xp3SequenceInteger(args[1], 0, &minimum) != 0)) {
        xp3FunctionError(call, BAD_CAST INV_NS, BAD_CAST "RANGE",
                         BAD_CAST "the minimum does not fit in 64 bits");
        return(NULL);
    }

    /* Integers have no size limit, so the C code checks */
    fits = xp3SequenceInteger(args[0], 0, &quantity);
    if (fits == 1) {
        xmlChar *text = xp3SequenceStringValue(args[0], 0);
        int negative;

        if (text == NULL)
            return(NULL);
        negative = (text[0] == '-');
        xmlFree(text);
        if (negative) {
            xp3FunctionError(call, BAD_CAST INV_NS, BAD_CAST "RANGE",
                             BAD_CAST "the quantity does not fit in 64 bits");
            return(NULL);
        }
        /* A positive quantity beyond 64 bits is more than enough */
        quantity = minimum;
    }

    if (quantity < minimum)
        order = minimum - quantity;
    return(xp3NewIntegerSequence(&order, 1));
}

/*
 * inv:decimal-places($amount as xs:decimal) as xs:integer
 *
 * The number of digits after the point. The string value of a decimal
 * is its canonical form, so trailing zeros are gone.
 */
static xp3Sequence *
decimalPlaces(xp3FunctionCall *call, xp3Sequence **args, int count,
              void *userData) {
    Calls *calls = userData;
    xmlChar *text;
    const xmlChar *point;
    int64_t places = 0;

    (void) call;
    (void) count;
    calls->decimalPlaces++;

    text = xp3SequenceStringValue(args[0], 0);
    if (text == NULL)
        return(NULL);
    point = xmlStrchr(text, '.');
    if (point != NULL)
        places = xmlStrlen(point + 1);
    xmlFree(text);
    return(xp3NewIntegerSequence(&places, 1));
}

/*
 * inv:units-in($pack as xs:string) as xs:integer
 *
 * The number of units in a pack. The same name always gives the same
 * number, so the function can be registered as deterministic.
 */
static xp3Sequence *
unitsIn(xp3FunctionCall *call, xp3Sequence **args, int count,
        void *userData) {
    static const struct {
        const char *name;
        int64_t units;
    } packs[] = {
        { "pair", 2 },
        { "dozen", 12 },
        { "gross", 144 }
    };
    Calls *calls = userData;
    xmlChar *name;
    size_t i;

    (void) count;
    calls->unitsIn++;

    name = xp3SequenceStringValue(args[0], 0);
    if (name == NULL)
        return(NULL);
    for (i = 0; i < sizeof(packs) / sizeof(packs[0]); i++) {
        if (xmlStrEqual(name, BAD_CAST packs[i].name)) {
            xmlFree(name);
            return(xp3NewIntegerSequence(&packs[i].units, 1));
        }
    }
    xmlFree(name);
    xp3FunctionError(call, BAD_CAST INV_NS, BAD_CAST "UNKNOWNPACK",
                     BAD_CAST "no such pack");
    return(NULL);
}

/*
 * inv:stock($sku as xs:string) as xs:integer
 *
 * The stock level in the warehouse that the evaluation is for, given
 * with xp3SetDynamicContextUserData(). Levels change, so the function
 * is not deterministic.
 */
static xp3Sequence *
stock(xp3FunctionCall *call, xp3Sequence **args, int count, void *userData) {
    Calls *calls = userData;
    const Warehouse *warehouse;
    xmlChar *sku;
    int64_t level = 0;
    size_t i;

    (void) count;
    calls->stock++;

    warehouse = xp3FunctionUserData(call);
    if (warehouse == NULL) {
        xp3FunctionError(call, BAD_CAST INV_NS, BAD_CAST "NOWAREHOUSE",
                         BAD_CAST "the evaluation has no warehouse");
        return(NULL);
    }
    sku = xp3SequenceStringValue(args[0], 0);
    if (sku == NULL)
        return(NULL);
    for (i = 0; i < warehouse->count; i++) {
        if (xmlStrEqual(sku, BAD_CAST warehouse->levels[i].sku))
            level = warehouse->levels[i].level;
    }
    xmlFree(sku);
    return(xp3NewIntegerSequence(&level, 1));
}

/*
 * Q{urn:example:labels}tag($sku as xs:string) as xs:string
 */
static xp3Sequence *
tag(xp3FunctionCall *call, xp3Sequence **args, int count, void *userData) {
    const xmlChar *labels[1];
    xmlChar *sku;
    xmlChar *label;
    xp3Sequence *result;

    (void) call;
    (void) count;
    (void) userData;

    sku = xp3SequenceStringValue(args[0], 0);
    if (sku == NULL)
        return(NULL);
    label = xmlStrncatNew(BAD_CAST "SKU-", sku, -1);
    xmlFree(sku);
    if (label == NULL)
        return(NULL);
    labels[0] = label;
    result = xp3NewStringSequence(labels, 1);
    xmlFree(label);
    return(result);
}

/*
 * inv:broken() as xs:integer, which returns a string instead
 */
static xp3Sequence *
brokenResult(xp3FunctionCall *call, xp3Sequence **args, int count,
             void *userData) {
    const xmlChar *five = BAD_CAST "five";

    (void) call;
    (void) args;
    (void) count;
    (void) userData;
    return(xp3NewStringSequence(&five, 1));
}

/*
 * inv:untyped() as xs:integer, which returns xs:untypedAtomic
 */
static xp3Sequence *
untypedResult(xp3FunctionCall *call, xp3Sequence **args, int count,
              void *userData) {
    const xmlChar *seven = BAD_CAST "7";

    (void) call;
    (void) args;
    (void) count;
    (void) userData;
    return(xp3NewAtomicSequence(XP3_TYPE_UNTYPED_ATOMIC, &seven, 1, NULL));
}

/*
 * Register a function and print the outcome if it failed. Returns 0
 * if the outcome is the expected one, 1 if not.
 */
static int
registerFunction(xp3StaticContext *sctx, const char *signature, int flags,
                 xp3FunctionImpl impl, void *userData,
                 const char *expectedError) {
    xp3Error *error = NULL;
    int ret = 0;

    if (xp3RegisterFunction(sctx, BAD_CAST signature, flags, impl, userData,
                            &error) == 0) {
        if (expectedError != NULL)
            ret = 1;
        return(ret);
    }

    printf("register %s -> ", signature);
    exPrintError(error);
    printf("\n");
    if ((expectedError == NULL) ||
        (!xmlStrEqual(xp3ErrorCode(error), BAD_CAST expectedError)))
        ret = 1;
    xp3ErrorFree(error);
    return(ret);
}

/* A function to register, and the error registering raises, or NULL */
typedef struct {
    const char *signature;
    int flags;
    xp3FunctionImpl impl;
    const char *expectedError;
} Registration;

static const Registration registrations[] = {
    { "inv:parse-price($raw as xs:string?) as xs:decimal?",
      0, parsePrice, NULL },
    { "inv:restock($qty as xs:integer) as xs:integer",
      XP3_FUNCTION_DETERMINISTIC, restock, NULL },
    { "inv:restock($qty as xs:integer, $minimum as xs:integer)"
      " as xs:integer",
      XP3_FUNCTION_DETERMINISTIC, restock, NULL },
    { "inv:decimal-places($amount as xs:decimal) as xs:integer",
      XP3_FUNCTION_DETERMINISTIC, decimalPlaces, NULL },
    { "inv:stock($sku as xs:string) as xs:integer",
      0, stock, NULL },
    { "inv:broken() as xs:integer", 0, brokenResult, NULL },
    { "inv:untyped() as xs:integer", 0, untypedResult, NULL },
    /* A namespace written out, without a prefix for it */
    { "Q{" LABEL_NS "}tag($sku as xs:string) as xs:string",
      XP3_FUNCTION_DETERMINISTIC, tag, NULL },
    /* Mistakes in signatures are reported when registering */
    { "stock:level($sku as xs:string)", 0, stock, "XPST0081" },
    { "inv:age($days as xs:days)", 0, stock, "XPST0051" },
    { "inv:stock($code as xs:string)", 0, stock, "XQST0034" }
};

/* A query, and the error code it raises, or NULL if it succeeds */
typedef struct {
    const char *text;
    const char *expectedError;
} Query;

static const Query queries[] = {
    /* An optional parameter, given the empty sequence */
    { "inv:parse-price(())", NULL },
    { "inv:parse-price(//item[@sku = 'A6']/@price)", NULL },
    { "//item[@sku = ('A1', 'A11')] ! inv:parse-price(@price)", NULL },
    { "//item ! inv:parse-price(@price)", "BADPRICE" },
    /* Converted before the C code runs: untyped values are cast */
    { "inv:decimal-places(//item[@sku = 'A1']/@price)", NULL },
    { "inv:decimal-places(//item[@sku = 'A7']/@price)", NULL },
    { "inv:decimal-places(12)", NULL },
    { "inv:decimal-places(//item[@sku = 'A2']/@price)", "FORG0001" },
    { "inv:decimal-places(//item[@sku = 'A6']/@price)", "XPTY0004" },
    { "inv:decimal-places('12.50')", "XPTY0004" },
    { "inv:decimal-places(1.5e0)", "XPTY0004" },
    /* Two arities of one name, and a third that doesn't exist */
    { "inv:restock(3)", NULL },
    { "inv:restock(3, 10)", NULL },
    { "inv:restock(1, 2, 3)", "XPST0017" },
    { "inv:restock(//item/@qty)", "XPTY0004" },
    /* Once per item, quantities beyond 64 bits included */
    { "//item ! inv:restock(@qty)", NULL },
    /* Results are converted and checked like arguments */
    { "inv:broken()", "XPTY0004" },
    { "inv:untyped()", NULL },
    { "Q{" LABEL_NS "}tag('A1')", NULL },
    { "inv:stock('A1')", "NOWAREHOUSE" }
};

/*
 * Compile an expression and evaluate it twice, and show how often the
 * function behind it ran when.
 */
static int
countCalls(const xp3StaticContext *sctx, xp3DynamicContext *dctx,
           const char *query, const int *counter, const char *expectedError) {
    xp3Expr *expr;
    int failures = 0;

    expr = xp3Compile(sctx, BAD_CAST query, NULL);
    if (expr == NULL)
        return(1);
    printf("calls after compiling %s -> %d\n", query, *counter);
    failures += exEvaluate(expr, dctx, "first evaluation", expectedError);
    failures += exEvaluate(expr, dctx, "second evaluation", expectedError);
    printf("calls after evaluating twice -> %d\n", *counter);
    xp3FreeExpr(expr);
    return(failures);
}

/*
 * The same function, deterministic or not, with constant arguments.
 */
static int
folding(xp3DynamicContext *dctx) {
    static const char signature[] =
        "inv:units-in($pack as xs:string) as xs:integer";
    xp3StaticContext *once = xp3NewStaticContext();
    xp3StaticContext *always = xp3NewStaticContext();
    Calls onceCalls;
    Calls alwaysCalls;
    int failures = 0;

    memset(&onceCalls, 0, sizeof(onceCalls));
    memset(&alwaysCalls, 0, sizeof(alwaysCalls));
    if ((once == NULL) || (always == NULL) ||
        (xp3DeclareNamespace(once, BAD_CAST "inv", BAD_CAST INV_NS) < 0) ||
        (xp3DeclareNamespace(always, BAD_CAST "inv", BAD_CAST INV_NS) < 0)) {
        failures++;
        goto done;
    }
    failures += registerFunction(once, signature,
                                 XP3_FUNCTION_DETERMINISTIC, unitsIn,
                                 &onceCalls, NULL);
    failures += registerFunction(always, signature, 0, unitsIn,
                                 &alwaysCalls, NULL);

    printf("Deterministic:\n");
    failures += countCalls(once, dctx, "inv:units-in('gross') * 2",
                           &onceCalls.unitsIn, NULL);
    printf("Not deterministic:\n");
    failures += countCalls(always, dctx, "inv:units-in('gross') * 2",
                           &alwaysCalls.unitsIn, NULL);
    /* An error while folding is dropped, and raised when evaluating */
    printf("Deterministic, raising an error:\n");
    onceCalls.unitsIn = 0;
    failures += countCalls(once, dctx, "inv:units-in('crate')",
                           &onceCalls.unitsIn, "UNKNOWNPACK");

done:
    xp3FreeStaticContext(always);
    xp3FreeStaticContext(once);
    return(failures);
}

/*
 * One compiled expression, evaluated for two warehouses.
 */
static int
warehouses(const xp3StaticContext *sctx, xp3DynamicContext *dctx) {
    static const StockLevel northLevels[] = {
        { "A1", 7 }, { "A2", 0 }, { "A9", 1 }
    };
    static const StockLevel southLevels[] = {
        { "A1", 0 }, { "A2", 12 }, { "A9", 30 }
    };
    Warehouse north = { northLevels, 3 };
    Warehouse south = { southLevels, 3 };
    xp3Expr *expr;
    int failures = 0;

    expr = xp3Compile(sctx, BAD_CAST
                      "//item[@sku = ('A1', 'A2', 'A9')]"
                      "[inv:stock(@sku) lt 2]/string(@sku)", NULL);
    if (expr == NULL)
        return(1);
    xp3SetDynamicContextUserData(dctx, &north);
    failures += exEvaluate(expr, dctx, "low stock in the north", NULL);
    xp3SetDynamicContextUserData(dctx, &south);
    failures += exEvaluate(expr, dctx, "low stock in the south", NULL);
    xp3SetDynamicContextUserData(dctx, NULL);
    xp3FreeExpr(expr);
    return(failures);
}

int
main(int argc, char **argv) {
    xmlDocPtr doc = NULL;
    xp3StaticContext *sctx = NULL;
    xp3DynamicContext *dctx = NULL;
    Calls calls;
    size_t i;
    int failures = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: %s DATA-DIRECTORY\n", argv[0]);
        return(2);
    }

    memset(&calls, 0, sizeof(calls));
    doc = exReadDocument(argv[1], "inventory.xml", 0);
    sctx = xp3NewStaticContext();
    dctx = xp3NewDynamicContext();
    if ((doc == NULL) || (sctx == NULL) || (dctx == NULL)) {
        failures++;
        goto done;
    }
    xp3SetContextNode(dctx, (xmlNodePtr) doc);

    if (xp3DeclareNamespace(sctx, BAD_CAST "inv", BAD_CAST INV_NS) < 0) {
        failures++;
        goto done;
    }
    for (i = 0; i < sizeof(registrations) / sizeof(registrations[0]); i++)
        failures += registerFunction(sctx, registrations[i].signature,
                                     registrations[i].flags,
                                     registrations[i].impl, &calls,
                                     registrations[i].expectedError);
    for (i = 0; i < sizeof(queries) / sizeof(queries[0]); i++)
        failures += exRun(sctx, dctx, NULL, queries[i].text,
                          queries[i].expectedError);
    printf("calls of inv:parse-price -> %d\n", calls.parsePrice);
    printf("calls of inv:decimal-places -> %d\n", calls.decimalPlaces);
    printf("calls of inv:restock -> %d\n", calls.restock);

    failures += folding(dctx);
    failures += warehouses(sctx, dctx);
    printf("calls of inv:stock -> %d\n", calls.stock);

done:
    xp3FreeDynamicContext(dctx);
    xp3FreeStaticContext(sctx);
    xmlFreeDoc(doc);
    if (failures > 0)
        return(1);
    return(0);
}
