/*
 * 03-numbers.c: decimals and doubles, casts, sums, and reading typed
 * results in C
 *
 * The program of doc/tutorial/03-numbers.md. Run it with the directory
 * of the example documents:
 *
 *     03-numbers examples/data
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <math.h>
#include <stdio.h>

#include <libxml/tree.h>
#include <libxml/xmlmemory.h>
#include <libxpath3/xpath3.h>

#include "common.h"

/* A query, and the error code it raises, or NULL if it succeeds */
typedef struct {
    const char *text;
    const char *expectedError;
} Query;

static const Query queries[] = {
    /* Literals with a point are decimals, with an exponent doubles */
    { "0.1 + 0.2", NULL },
    { "0.1e0 + 0.2e0", NULL },
    { "0.1 + 0.2 = 0.3", NULL },
    { "0.1e0 + 0.2e0 = 0.3e0", NULL },
    /* fn:sum casts untyped values to xs:double; "" is not one */
    { "sum(//item/@price)", "FORG0001" },
    { "sum(//item/@price[. castable as xs:decimal])", NULL },
    { "sum(//item/@price[. castable as xs:decimal] ! xs:decimal(.))", NULL },
    { "sum(//item/@price[. castable as xs:double])", NULL },
    { "sum(())", NULL },
    { "avg(())", NULL },
    /* Integer division is decimal division (XPath 3.1, 3.5) */
    { "1 div 0", "FOAR0001" },
    { "1e0 div 0", NULL },
    { "-1e0 div 0", NULL },
    { "0e0 div 0", NULL },
    { "1 idiv 0", "FOAR0001" },
    { "1e0 idiv 0", "FOAR0001" },
    { "xs:double('INF') idiv 1", "FOAR0002" },
    { "1 div 3", NULL },
    { "1 div 3 * 3", NULL },
    { "-7 idiv 2", NULL },
    { "-7 mod 2", NULL },
    /* NaN equals nothing, itself included */
    { "xs:double('NaN') = xs:double('NaN')", NULL },
    { "xs:double('NaN') != xs:double('NaN')", NULL },
    { "//item[@price != @price]/string(@sku)", NULL },
    { "//item[number(@price) != number(@price)]/string(@sku)", NULL },
    /* Halves round up, or to even; 35.425e0 is not exactly a half */
    { "round(2.5)", NULL },
    { "round(-2.5)", NULL },
    { "round(-0.5)", NULL },
    { "round(-0.5e0)", NULL },
    { "round-half-to-even(-2.5)", NULL },
    { "round-half-to-even(-3.5)", NULL },
    { "round(35.425, 2)", NULL },
    { "round(35.425e0, 2)", NULL },
    { "floor(-0.5)", NULL },
    { "ceiling(-0.5e0)", NULL },
    /* Integers have no upper limit */
    { "9223372036854775807 + 1", NULL },
    { "-9223372036854775808 idiv -1", NULL },
    { "xs:long('9223372036854775807') + 1", NULL },
    { "xs:integer(//item[@sku = 'A8']/@qty) * 2", NULL },
    { "xs:double(//item[@sku = 'A8']/@qty)", NULL },
    /* Lexical forms: what each type accepts */
    { "xs:decimal('1e3')", "FORG0001" },
    { "xs:double('1e3')", NULL },
    { "xs:integer('1.0')", "FORG0001" },
    { "xs:integer(1.0)", NULL },
    { "xs:decimal(' 12.50 ')", NULL },
    { "xs:decimal('+5')", NULL },
    { "xs:decimal('.5')", NULL },
    { "xs:decimal('5.')", NULL },
    { "xs:decimal('1,250.00')", "FORG0001" },
    { "xs:decimal(translate('1,250.00', ',', ''))", NULL },
    { "xs:double('-0')", NULL },
    { "xs:decimal('-0')", NULL },
    { "xs:decimal(//item[@sku = 'A7']/@price) * 1000000", NULL },
    { "xs:double(//item[@sku = 'A7']/@price)", NULL },
    { "xs:double('1e309')", NULL },
    { "xs:integer(xs:double('INF'))", "FOCA0002" },
    /* A double as a string, and as a decimal (F&O 3.1, 19.1.2.3) */
    { "string(1e6)", NULL },
    { "string(1e-6)", NULL },
    { "xs:decimal(0.1e0)", NULL },
    { "round(xs:decimal(35.425e0), 2)", NULL },
    /* The integer becomes a double before comparing (XPath 3.1, B.1) */
    { "9007199254740993 = 9007199254740992e0", NULL },
    { "9007199254740993 = 9007199254740992", NULL }
};

/*
 * Evaluate a query and read its first item in the three ways the API
 * offers: as an int64_t, as a double and as a string.
 */
static int
readNumber(const xp3StaticContext *sctx, xp3DynamicContext *dctx,
           const char *query) {
    xp3Expr *expr;
    xp3Sequence *result;
    xmlChar *text;
    int64_t integer;
    double number;
    int ret;

    expr = xp3Compile(sctx, BAD_CAST query, NULL);
    if (expr == NULL)
        return(1);
    result = xp3Evaluate(expr, dctx, NULL);
    xp3FreeExpr(expr);
    if ((result == NULL) || (xp3SequenceLength(result) != 1)) {
        xp3FreeSequence(result);
        return(1);
    }
    printf("%s\n", query);

    /* 0: it fits, 1: an integer outside int64_t, -1: not an integer */
    ret = xp3SequenceInteger(result, 0, &integer);
    if (ret == 0)
        printf("  xp3SequenceInteger -> %lld\n", (long long) integer);
    else if (ret == 1)
        printf("  xp3SequenceInteger -> 1, outside int64_t\n");
    else
        printf("  xp3SequenceInteger -> -1, not an integer\n");

    /* Any numeric type, rounded to the nearest double */
    if (xp3SequenceDouble(result, 0, &number) < 0)
        printf("  xp3SequenceDouble -> -1, not a number\n");
    else if (isnan(number))
        printf("  xp3SequenceDouble -> NaN\n");
    else
        printf("  xp3SequenceDouble -> %.17g\n", number);

    /* The exact value, for every type */
    text = xp3SequenceStringValue(result, 0);
    if (text == NULL) {
        xp3FreeSequence(result);
        return(1);
    }
    printf("  xp3SequenceStringValue -> \"%s\"\n", (const char *) text);
    xmlFree(text);

    xp3FreeSequence(result);
    return(0);
}

int
main(int argc, char **argv) {
    static const char *const readQueries[] = {
        "count(//item)",
        "xs:integer(//item[@sku = 'A8']/@qty)",
        "sum(//item/@price[. castable as xs:decimal] ! xs:decimal(.))",
        "data(//item[@sku = 'A1']/@price)",
        "number(//item[@sku = 'A1']/@price)",
        "number(//item[@sku = 'A6']/@price)",
        "xs:double('-0')"
    };
    xmlDocPtr doc = NULL;
    xp3StaticContext *sctx = NULL;
    xp3DynamicContext *dctx = NULL;
    size_t i;
    int failures = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: %s DATA-DIRECTORY\n", argv[0]);
        return(2);
    }

    doc = exReadDocument(argv[1], "inventory.xml", 0);
    sctx = xp3NewStaticContext();
    dctx = xp3NewDynamicContext();
    if ((doc == NULL) || (sctx == NULL) || (dctx == NULL)) {
        failures++;
        goto done;
    }
    xp3SetContextNode(dctx, (xmlNodePtr) doc);

    for (i = 0; i < sizeof(queries) / sizeof(queries[0]); i++)
        failures += exRun(sctx, dctx, NULL, queries[i].text,
                          queries[i].expectedError);

    printf("Reading results in C:\n");
    for (i = 0; i < sizeof(readQueries) / sizeof(readQueries[0]); i++)
        failures += readNumber(sctx, dctx, readQueries[i]);

done:
    xp3FreeDynamicContext(dctx);
    xp3FreeStaticContext(sctx);
    xmlFreeDoc(doc);
    if (failures > 0)
        return(1);
    return(0);
}
