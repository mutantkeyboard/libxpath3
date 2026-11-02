/*
 * testvalue.c: tests for types, atomic values, sequences and number
 * formatting
 *
 * The expected strings for doubles and floats were produced by
 * Saxon-HE 12.5 (string(xs:double(...)) and string(xs:float(...))),
 * the reference doc/DESIGN.md 4.4 follows.
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
#include <string.h>

#include <libxml/parser.h>
#include <libxml/xmlmemory.h>

#include "private/xp3decimal.h"
#include "private/xp3double.h"
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

/*
 * Types
 */

static void
testTypes(void) {
    int i;

    check(xmlStrEqual(xp3TypeLocalName(XP3_TYPE_INTEGER), BAD_CAST "integer"),
          "name of xs:integer");
    check(xp3TypeLookup(BAD_CAST "unsignedByte") == XP3_TYPE_UNSIGNED_BYTE,
          "lookup of xs:unsignedByte");
    check(xp3TypeLookup(BAD_CAST "NMTOKENS") == -1,
          "list types are not atomic");
    check(xp3TypeLookup(BAD_CAST "nosuch") == -1, "unknown name");
    check(xp3TypeLookup(NULL) == -1, "NULL name");

    check(xp3TypeDerivesFrom(XP3_TYPE_BYTE, XP3_TYPE_INTEGER),
          "byte derives from integer");
    check(xp3TypeDerivesFrom(XP3_TYPE_BYTE, XP3_TYPE_DECIMAL),
          "byte derives from decimal");
    check(!xp3TypeDerivesFrom(XP3_TYPE_INTEGER, XP3_TYPE_BYTE),
          "integer does not derive from byte");
    check(!xp3TypeDerivesFrom(XP3_TYPE_UNTYPED_ATOMIC, XP3_TYPE_STRING),
          "untypedAtomic does not derive from string");
    check(!xp3TypeDerivesFrom(XP3_TYPE_FLOAT, XP3_TYPE_DOUBLE),
          "float does not derive from double");
    check(xp3TypeDerivesFrom(XP3_TYPE_DATE_TIME_STAMP, XP3_TYPE_DATE_TIME),
          "dateTimeStamp derives from dateTime");

    check(xp3TypePrimitive(XP3_TYPE_BYTE) == XP3_TYPE_DECIMAL,
          "primitive of byte");
    check(xp3TypePrimitive(XP3_TYPE_INTEGER) == XP3_TYPE_DECIMAL,
          "primitive of integer");
    check(xp3TypePrimitive(XP3_TYPE_ID) == XP3_TYPE_STRING,
          "primitive of ID");
    check(xp3TypePrimitive(XP3_TYPE_UNTYPED_ATOMIC) ==
          XP3_TYPE_UNTYPED_ATOMIC, "untypedAtomic is primitive");
    check(xp3TypeParent(XP3_TYPE_BYTE) == XP3_TYPE_SHORT, "parent of byte");
    check(xp3TypeParent(XP3_TYPE_DECIMAL) == XP3_TYPE_ANY_ATOMIC,
          "parent of a primitive type");
    check(xp3TypeParent(XP3_TYPE_ANY_ATOMIC) == XP3_TYPE_ANY_ATOMIC,
          "anyAtomicType is its own parent");
    check(xp3TypeParent(XP3_TYPE_COUNT) == XP3_TYPE_ANY_ATOMIC,
          "parent of an invalid type");
    check(xp3TypePrimitive(XP3_TYPE_DAY_TIME_DURATION) == XP3_TYPE_DURATION,
          "primitive of dayTimeDuration");

    check(xp3TypeRepresentation(XP3_TYPE_POSITIVE_INTEGER) == XP3_REP_DECIMAL,
          "integers are stored as decimals");
    check(xp3TypeRepresentation(XP3_TYPE_ANY_URI) == XP3_REP_STRING,
          "anyURI is stored as a string");
    check(xp3TypeIsAbstract(XP3_TYPE_ANY_ATOMIC) &&
          xp3TypeIsAbstract(XP3_TYPE_NOTATION) &&
          !xp3TypeIsAbstract(XP3_TYPE_STRING), "abstract types");

    /* Every type has a name that finds it again, and every chain of
     * parents ends at anyAtomicType. */
    for (i = 0; i < XP3_TYPE_COUNT; i++) {
        char what[64];

        snprintf(what, sizeof(what), "type %d round-trips by name", i);
        check(xp3TypeLookup(xp3TypeLocalName((xp3TypeId) i)) == i, what);
        snprintf(what, sizeof(what), "type %d derives from anyAtomicType",
                 i);
        check(xp3TypeDerivesFrom((xp3TypeId) i, XP3_TYPE_ANY_ATOMIC), what);
    }

    /* Invalid ids */
    check(xp3TypeLocalName(XP3_TYPE_COUNT) == NULL, "name of invalid type");
    check(xp3TypeRepresentation((xp3TypeId) -1) == XP3_REP_NONE,
          "representation of invalid type");
    check(!xp3TypeDerivesFrom(XP3_TYPE_COUNT, XP3_TYPE_ANY_ATOMIC),
          "invalid type derives from nothing");
}

/* The public names of the types, which a binding sees */
static void
testPublicTypes(void) {
    int i;

    check(XP3_TYPE_NOTATION == 45, "the last type keeps its value");
    for (i = 0; i < XP3_TYPE_COUNT; i++) {
        char what[64];

        snprintf(what, sizeof(what), "type %d has its public name", i);
        check((xp3AtomicTypeName((xp3AtomicType) i) != NULL) &&
              (xp3AtomicTypeName((xp3AtomicType) i) ==
               xp3TypeLocalName((xp3TypeId) i)), what);
        snprintf(what, sizeof(what), "type %d derives from itself", i);
        check(xp3AtomicTypeDerivesFrom((xp3AtomicType) i,
                                       (xp3AtomicType) i) == 1, what);
    }
    check(xmlStrEqual(xp3AtomicTypeName(XP3_TYPE_INTEGER),
                      BAD_CAST "integer"), "public name of xs:integer");
    check(xp3AtomicTypeName((xp3AtomicType) XP3_TYPE_COUNT) == NULL,
          "no name past the last type");
    check(xp3AtomicTypeName((xp3AtomicType) -1) == NULL,
          "no name for a negative type");

    check(xp3AtomicTypeDerivesFrom(XP3_TYPE_INT, XP3_TYPE_INTEGER) == 1,
          "int derives from integer");
    check(xp3AtomicTypeDerivesFrom(XP3_TYPE_INTEGER, XP3_TYPE_INT) == 0,
          "integer does not derive from int");
    check(xp3AtomicTypeDerivesFrom(XP3_TYPE_DOUBLE, XP3_TYPE_DECIMAL) == 0,
          "double does not derive from decimal");
    check(xp3AtomicTypeDerivesFrom((xp3AtomicType) XP3_TYPE_COUNT,
                                   XP3_TYPE_ANY_ATOMIC) == 0,
          "an unknown type derives from nothing");
    check(xp3AtomicTypeDerivesFrom(XP3_TYPE_INTEGER,
                                   (xp3AtomicType) -1) == 0,
          "nothing derives from an unknown type");
}

/*
 * Atomic values
 */

static void
testAtomics(void) {
    xp3Atomic *a;
    xp3Decimal *d;
    xmlChar *str;

    a = xp3AtomicNewString(XP3_TYPE_TOKEN, BAD_CAST "abcdef", 3);
    check(a != NULL && a->type == XP3_TYPE_TOKEN &&
          xmlStrEqual(a->u.str, BAD_CAST "abc"), "token value copied");
    check(xp3AtomicRef(a) == a && a->refs == 2, "reference added");
    xp3AtomicUnref(a);
    check(a->refs == 1, "reference released");
    xp3AtomicUnref(a);

    a = xp3AtomicNewString(XP3_TYPE_STRING, NULL, 0);
    check(a != NULL && xmlStrEqual(a->u.str, BAD_CAST ""), "empty string");
    xp3AtomicUnref(a);
    check(xp3AtomicNewString(XP3_TYPE_INTEGER, BAD_CAST "1", 1) == NULL,
          "integer is not string-like");
    check(xp3AtomicNewString(XP3_TYPE_ANY_ATOMIC, BAD_CAST "1", 1) == NULL,
          "abstract type rejected");

    a = xp3AtomicNewBoolean(42);
    check(a != NULL && a->u.boolean == 1, "boolean normalized to 1");
    xp3AtomicUnref(a);

    a = xp3AtomicNewInteger(-7);
    check(a != NULL && a->type == XP3_TYPE_INTEGER, "integer value");
    str = (a != NULL) ? xp3DecimalToString(a->u.dec) : NULL;
    check(str != NULL && xmlStrEqual(str, BAD_CAST "-7"), "integer -7");
    xmlFree(str);
    xp3AtomicUnref(a);

    d = NULL;
    xp3DecimalParse(BAD_CAST "1.5", 3, 0, &d);
    check(xp3AtomicNewDecimal(XP3_TYPE_INTEGER, d) == NULL,
          "1.5 is not an integer (and was freed)");
    d = NULL;
    xp3DecimalParse(BAD_CAST "200", 3, 0, &d);
    a = xp3AtomicNewDecimal(XP3_TYPE_UNSIGNED_BYTE, d);
    check(a != NULL && a->type == XP3_TYPE_UNSIGNED_BYTE,
          "derived integer type keeps its id");
    xp3AtomicUnref(a);

    a = xp3AtomicNewDouble(-0.0);
    check(a != NULL && signbit(a->u.d), "negative zero kept");
    xp3AtomicUnref(a);
    a = xp3AtomicNewFloat(1.5f);
    check(a != NULL && a->u.f == 1.5f, "float value");
    xp3AtomicUnref(a);

    a = xp3AtomicNewBinary(XP3_TYPE_HEX_BINARY,
                           (const unsigned char *) "\x00\xff", 2);
    check(a != NULL && a->u.bin.len == 2 && a->u.bin.data[1] == 0xff,
          "binary bytes copied, NUL included");
    xp3AtomicUnref(a);
    a = xp3AtomicNewBinary(XP3_TYPE_BASE64_BINARY, NULL, 0);
    check(a != NULL && a->u.bin.len == 0, "empty binary");
    xp3AtomicUnref(a);

    a = xp3AtomicNewQName(BAD_CAST "urn:example", BAD_CAST "ex",
                          BAD_CAST "local");
    check(a != NULL && xmlStrEqual(a->u.qname.uri, BAD_CAST "urn:example") &&
          xmlStrEqual(a->u.qname.prefix, BAD_CAST "ex") &&
          xmlStrEqual(a->u.qname.local, BAD_CAST "local"), "QName parts");
    xp3AtomicUnref(a);
    a = xp3AtomicNewQName(BAD_CAST "", NULL, BAD_CAST "x");
    check(a != NULL && a->u.qname.uri == NULL && a->u.qname.prefix == NULL,
          "QName in no namespace");
    xp3AtomicUnref(a);
    check(xp3AtomicNewQName(NULL, NULL, NULL) == NULL,
          "QName needs a local name");

    xp3AtomicUnref(NULL);
}

/*
 * Sequences
 */

/* Iterate a sequence and describe it as "1 2 3" for integers and "n"
 * for nodes. */
static void
describe(const xp3Value *v, char *buf, size_t size) {
    xp3Iter iter;
    xp3Item item;
    size_t used = 0;

    buf[0] = 0;
    xp3IterInit(&iter, v);
    while (xp3IterNext(&iter, &item) > 0) {
        char part[48];
        int64_t n;

        if (item.kind == XP3_ITEM_NODE)
            snprintf(part, sizeof(part), "n");
        else if (xp3DecimalToInt64(item.u.atomic->u.dec, &n) == 0)
            snprintf(part, sizeof(part), "%lld", (long long) n);
        else
            snprintf(part, sizeof(part), "?");
        used += (size_t) snprintf(buf + used, size - used, "%s%s",
                                  used ? " " : "", part);
        xp3ItemUnref(item);
        if (used >= size)
            break;
    }
}

static void
expectSeq(const xp3Value *v, const char *expected, const char *what) {
    char actual[256];
    char msg[512];

    describe(v, actual, sizeof(actual));
    snprintf(msg, sizeof(msg), "%s: got [%s], expected [%s]", what, actual,
             expected);
    check(strcmp(actual, expected) == 0, msg);
}

static void
testSequences(void) {
    xp3SeqBuilder builder;
    xp3Value *v;
    xp3Value *r = NULL;
    xp3Item item;
    xmlNode node;
    int64_t n;

    /* Empty: shared, uncounted */
    v = xp3ValueEmpty();
    check(v == xp3ValueEmpty() && xp3ValueCount(v) == 0, "shared empty");
    xp3ValueRef(v);
    xp3ValueUnref(v);
    xp3ValueUnref(v);
    check(v->shape == XP3_SEQ_EMPTY && xp3ValueCount(v) == 0,
          "empty survives references");
    check(xp3ValueItemAt(v, 0, &item) == 0, "no item in empty");

    /* Single */
    v = xp3ValueSingle(xp3ItemFromAtomic(xp3AtomicNewInteger(5)));
    check(v != NULL && v->shape == XP3_SEQ_SINGLE && xp3ValueCount(v) == 1,
          "single");
    expectSeq(v, "5", "single item");
    check(xp3ValueRef(v) == v && v->refs == 2, "reference to a sequence");
    xp3ValueUnref(v);
    check(v->refs == 1, "reference released");
    check(xp3ValueItemAt(v, 0, &item) == 1, "item of single");
    item = xp3ItemRef(item);
    xp3ValueUnref(v);
    check(item.kind == XP3_ITEM_ATOMIC && item.u.atomic->refs == 2,
          "item outlives its sequence");
    xp3ItemUnref(item);
    xp3ItemUnref(item);
    check(xp3ValueRef(NULL) == NULL, "reference to NULL");

    /* Nodes are borrowed */
    memset(&node, 0, sizeof(node));
    v = xp3ValueSingle(xp3ItemFromNode(&node));
    check(xp3ValueItemAt(v, 0, &item) == 1 && item.kind == XP3_ITEM_NODE &&
          item.u.node == &node, "node item");
    xp3ItemUnref(item);
    xp3ValueUnref(v);

    /* Builder: 0, 1 and many items */
    xp3SeqBuilderInit(&builder);
    v = xp3SeqBuilderFinish(&builder);
    check(v == xp3ValueEmpty(), "empty builder gives the empty sequence");
    xp3SeqBuilderAdd(&builder, xp3ItemFromAtomic(xp3AtomicNewInteger(1)));
    v = xp3SeqBuilderFinish(&builder);
    check(v != NULL && v->shape == XP3_SEQ_SINGLE, "one item gives single");
    xp3ValueUnref(v);
    for (n = 1; n <= 20; n++)
        check(xp3SeqBuilderAdd(&builder,
                               xp3ItemFromAtomic(xp3AtomicNewInteger(n))) ==
              XP3_ERR_OK, "builder add");
    v = xp3SeqBuilderFinish(&builder);
    check(v != NULL && v->shape == XP3_SEQ_ARRAY && xp3ValueCount(v) == 20,
          "array of 20");
    check(xp3ValueItemAt(v, 19, &item) == 1 &&
          xp3DecimalToInt64(item.u.atomic->u.dec, &n) == 0 && n == 20,
          "random access");
    xp3ItemUnref(item);
    check(xp3ValueItemAt(v, 20, &item) == 0, "past the end");

    /* Sharing: an item in two sequences */
    xp3SeqBuilderAddValue(&builder, v);
    xp3SeqBuilderAddValue(&builder, v);
    r = xp3SeqBuilderFinish(&builder);
    check(xp3ValueCount(r) == 40 && v->u.array.items[0].u.atomic->refs == 3,
          "items shared by reference");
    xp3ValueUnref(r);
    check(v->u.array.items[0].u.atomic->refs == 1, "references released");
    xp3ValueUnref(v);

    /* Clearing an abandoned builder releases its items */
    xp3SeqBuilderAdd(&builder, xp3ItemFromAtomic(xp3AtomicNewInteger(9)));
    xp3SeqBuilderClear(&builder);
    check(builder.count == 0 && builder.items == NULL, "builder cleared");
}

static void
testRanges(void) {
    xp3SeqBuilder builder;
    xp3Value *v = NULL;
    xp3Item item;
    int64_t n;

    check(xp3ValueRange(3, 7, &v) == XP3_ERR_OK && v != NULL &&
          v->shape == XP3_SEQ_RANGE && xp3ValueCount(v) == 5, "3 to 7");
    expectSeq(v, "3 4 5 6 7", "range items");
    xp3ValueUnref(v);

    check(xp3ValueRange(-2, 1, &v) == XP3_ERR_OK, "-2 to 1");
    expectSeq(v, "-2 -1 0 1", "negative range");
    xp3ValueUnref(v);

    check(xp3ValueRange(5, 4, &v) == XP3_ERR_OK && v == xp3ValueEmpty(),
          "5 to 4 is empty");
    check(xp3ValueRange(5, 5, &v) == XP3_ERR_OK && xp3ValueCount(v) == 1,
          "5 to 5");
    xp3ValueUnref(v);

    /* A huge range is cheap: it is never expanded. */
    check(xp3ValueRange(1, INT64_MAX, &v) == XP3_ERR_OK &&
          xp3ValueCount(v) == (uint64_t) INT64_MAX, "1 to INT64_MAX");
    check(xp3ValueItemAt(v, (uint64_t) INT64_MAX - 1, &item) == 1 &&
          xp3DecimalToInt64(item.u.atomic->u.dec, &n) == 0 &&
          n == INT64_MAX, "last item of a huge range");
    xp3ItemUnref(item);
    /* ... but it cannot be collected into a builder. */
    xp3SeqBuilderInit(&builder);
    check(xp3SeqBuilderAddValue(&builder, v) == XP3_ERR_LIMIT,
          "expanding a huge range hits LIMIT");
    xp3SeqBuilderClear(&builder);
    xp3ValueUnref(v);

    check(xp3ValueRange(INT64_MIN, INT64_MAX - 1, &v) == XP3_ERR_OK &&
          xp3ValueCount(v) == UINT64_MAX, "count of 2^64 - 1");
    xp3ValueUnref(v);
    v = NULL;
    check(xp3ValueRange(INT64_MIN, INT64_MAX, &v) == XP3_ERR_LIMIT &&
          v == NULL, "2^64 items do not fit");

    check(xp3ValueRange(INT64_MIN, INT64_MIN + 1, &v) == XP3_ERR_OK,
          "range at INT64_MIN");
    check(xp3ValueItemAt(v, 0, &item) == 1 &&
          xp3DecimalToInt64(item.u.atomic->u.dec, &n) == 0 &&
          n == INT64_MIN, "first item at INT64_MIN");
    xp3ItemUnref(item);
    xp3ValueUnref(v);
}

/*
 * Numbers to strings
 */

static const struct {
    const char *input;
    const char *expected;
} doubleCases[] = {
    { "1e6", "1.0E6" },
    { "999999.9999999999", "999999.9999999999" },
    { "1e-6", "0.000001" },
    { "9.99999e-7", "9.99999E-7" },
    { "1e-7", "1.0E-7" },
    { "0.1", "0.1" },
    { "0.3", "0.3" },
    { "1e0", "1" },
    { "-0", "-0" },
    { "0", "0" },
    { "100", "100" },
    { "123456.789", "123456.789" },
    { "1234567", "1.234567E6" },
    { "1e21", "1.0E21" },
    { "1.5e-7", "1.5E-7" },
    { "2.5", "2.5" },
    { "0.000001234", "0.000001234" },
    { "1.7976931348623157e308", "1.7976931348623157E308" },
    { "5e-324", "4.9E-324" },
    { "4.9e-324", "4.9E-324" },
    { "2.2250738585072014e-308", "2.2250738585072014E-308" },
    { "9007199254740993", "9.007199254740992E15" },
    { "-123.456", "-123.456" },
    { "1e100", "1.0E100" },
    { "1.0000000000000002", "1.0000000000000002" },
    { "0.30000000000000004", "0.30000000000000004" },
    { "3.141592653589793", "3.141592653589793" },
    { "12345678901", "1.2345678901E10" },
    { "0.00001", "0.00001" },
    /* Longer than the parser's buffer: a decimal-comma locale needs a
     * copy on the heap. The digits after the halfway point between two
     * doubles decide the rounding, so all of them must be read. */
    { "2.50000000000000000000000000000000000000000000000000000000000000000001",
      "2.5" },
    { "9007199254740993.0000000000000000000000000000000000000000000000000001",
      "9.007199254740994E15" }
};

static const struct {
    const char *input;
    const char *expected;
} floatCases[] = {
    { "1.1", "1.1" },
    { "0.1", "0.1" },
    { "16777217", "1.6777216E7" },
    { "1e6", "1.0E6" },
    { "3.4028235e38", "3.4028235E38" },
    { "1.4e-45", "1.4E-45" },
    { "-0", "-0" },
    { "123456.7", "123456.7" },
    { "1e-7", "1.0E-7" },
    { "0.333333333", "0.33333334" },
    { "7e-6", "0.000007" },
    { "16777217.00000000000000000000000000000000000000000000000000000000001",
      "1.6777218E7" }
};

/* Long enough for a copy on the heap under a decimal-comma locale */
static const char xp3LongNumber[] =
    "1.000000000000000000000000000000000000000000000000000000000000000005";

static double
parseDouble(const char *str) {
    double value = 0;

    check(xp3DoubleParse(str, &value) == 0, "parse a double");
    return(value);
}

static float
parseFloat(const char *str) {
    float value = 0;

    check(xp3FloatParse(str, &value) == 0, "parse a float");
    return(value);
}

static void
expectXmlStr(xmlChar *actual, const char *expected, const char *what) {
    char msg[256];

    snprintf(msg, sizeof(msg), "%s: got %s, expected %s", what,
             actual ? (const char *) actual : "(null)", expected);
    check(actual != NULL && strcmp((const char *) actual, expected) == 0,
          msg);
    xmlFree(actual);
}

static void
testNumberStrings(const char *label) {
    size_t i;

    for (i = 0; i < sizeof(doubleCases) / sizeof(doubleCases[0]); i++) {
        char what[96];

        snprintf(what, sizeof(what), "%sdouble %s", label,
                 doubleCases[i].input);
        expectXmlStr(xp3DoubleToString(parseDouble(doubleCases[i].input)),
                     doubleCases[i].expected, what);
    }
    for (i = 0; i < sizeof(floatCases) / sizeof(floatCases[0]); i++) {
        char what[96];

        snprintf(what, sizeof(what), "%sfloat %s", label,
                 floatCases[i].input);
        expectXmlStr(xp3FloatToString(parseFloat(floatCases[i].input)),
                     floatCases[i].expected, what);
    }
    expectXmlStr(xp3DoubleToString(xp3DoubleNaN()), "NaN", "NaN");
    expectXmlStr(xp3DoubleToString(INFINITY), "INF", "INF");
    expectXmlStr(xp3DoubleToString(-INFINITY), "-INF", "-INF");
    check(parseDouble("1.5e-3") == 1.5e-3, "parse with a point");
    check(parseDouble(xp3LongNumber) == 1, "parse a long number");
}

/*
 * Whether parsing the long number allocates: expected under a
 * decimal-comma locale, where a failure must be reported, and never
 * under a locale with a decimal point.
 */
static void
testParseAllocation(int allocates) {
    double value;
    float single;

    if (!xp3TestAllocInstall()) {
        xp3TestAllocRestore();
        return;
    }
    xp3TestAllocFailAt(0);
    check(xp3DoubleParse(xp3LongNumber, &value) == (allocates ? -1 : 0),
          "a failed allocation while parsing a double is reported");
    check(xp3TestAllocFailed() == allocates,
          "parsing a long double allocates only for the locale");
    xp3TestAllocFailAt(0);
    check(xp3FloatParse(xp3LongNumber, &single) == (allocates ? -1 : 0),
          "a failed allocation while parsing a float is reported");
    check(xp3TestAllocFailed() == allocates,
          "parsing a long float allocates only for the locale");
    xp3TestAllocFailAt(-1);
    xp3TestAllocRestore();
}

/* The same results under a locale with a decimal comma */
static void
testLocale(void) {
    const char *names[] = { "de_DE.UTF-8", "de_DE.utf8", "de_DE",
                            "fr_FR.UTF-8", "fr_FR.utf8" };
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (setlocale(LC_NUMERIC, names[i]) != NULL)
            break;
    }
    if (i == sizeof(names) / sizeof(names[0]) ||
        strcmp(localeconv()->decimal_point, ",") != 0) {
        printf("testvalue: locale test skipped: no locale with a decimal "
               "comma\n");
        setlocale(LC_NUMERIC, "C");
        return;
    }
    testNumberStrings("decimal-comma locale: ");
    testParseAllocation(1);
    setlocale(LC_NUMERIC, "C");
}

/*
 * Atomic values as strings
 */

static void
checkAtomicString(xp3Atomic *atomic, const char *expected,
                  const char *what) {
    xmlChar *str = xp3AtomicToString(atomic);

    if ((str == NULL) || (strcmp((const char *) str, expected) != 0)) {
        fprintf(stderr, "FAIL: %s: \"%s\", expected \"%s\"\n", what,
                (str != NULL) ? (const char *) str : "NULL", expected);
        failures++;
    }
    xmlFree(str);
    xp3AtomicUnref(atomic);
}

static void
testAtomicStrings(void) {
    xp3Decimal *dec = NULL;

    checkAtomicString(xp3AtomicNewString(XP3_TYPE_STRING, BAD_CAST "ab", 2),
                      "ab", "string");
    checkAtomicString(xp3AtomicNewBoolean(1), "true", "true");
    checkAtomicString(xp3AtomicNewBoolean(0), "false", "false");
    checkAtomicString(xp3AtomicNewInteger(-42), "-42", "integer");
    if (xp3DecimalParse(BAD_CAST "1.50", 4, 0, &dec) == XP3_ERR_OK)
        checkAtomicString(xp3AtomicNewDecimal(XP3_TYPE_DECIMAL, dec), "1.5",
                          "decimal");
    checkAtomicString(xp3AtomicNewFloat(0.1f), "0.1", "float");
    checkAtomicString(xp3AtomicNewDouble(1e7), "1.0E7", "double");
    checkAtomicString(xp3AtomicNewBinary(XP3_TYPE_HEX_BINARY,
                                         (const unsigned char *) "\x0f\xa0",
                                         2), "0FA0", "hexBinary");
    checkAtomicString(xp3AtomicNewBinary(XP3_TYPE_HEX_BINARY, NULL, 0), "",
                      "empty hexBinary");
    checkAtomicString(xp3AtomicNewBinary(XP3_TYPE_BASE64_BINARY,
                                         (const unsigned char *) "Man", 3),
                      "TWFu", "base64Binary, no padding");
    checkAtomicString(xp3AtomicNewBinary(XP3_TYPE_BASE64_BINARY,
                                         (const unsigned char *) "Ma", 2),
                      "TWE=", "base64Binary, one =");
    checkAtomicString(xp3AtomicNewBinary(XP3_TYPE_BASE64_BINARY,
                                         (const unsigned char *) "M", 1),
                      "TQ==", "base64Binary, two =");
    checkAtomicString(xp3AtomicNewBinary(XP3_TYPE_BASE64_BINARY,
                                         (const unsigned char *) "\xff\xfe"
                                         "\xfd\xfc", 4),
                      "//79/A==", "base64Binary, high bytes");
    checkAtomicString(xp3AtomicNewQName(BAD_CAST "urn:x", BAD_CAST "p",
                                        BAD_CAST "local"), "p:local",
                      "QName with prefix");
    checkAtomicString(xp3AtomicNewQName(NULL, NULL, BAD_CAST "local"),
                      "local", "QName without prefix");
    check(xp3AtomicToString(NULL) == NULL, "NULL has no string");
}

/*
 * Allocation failures
 */

/* Build values of every shape and describe them. Returns the
 * description, or NULL on failure. */
static int
buildValues(char *out, size_t size) {
    xp3SeqBuilder builder;
    xp3Value *range = NULL;
    xp3Value *all;
    xp3Atomic *a;
    xp3Decimal *dec;
    double half;
    xmlChar *num;

    out[0] = 0;
    if (xp3ValueRange(10, 12, &range) != XP3_ERR_OK)
        return(-1);
    xp3SeqBuilderInit(&builder);
    a = xp3AtomicNewInteger(1);
    if ((a == NULL) ||
        (xp3SeqBuilderAdd(&builder, xp3ItemFromAtomic(a)) != XP3_ERR_OK) ||
        (xp3SeqBuilderAddValue(&builder, range) != XP3_ERR_OK)) {
        xp3SeqBuilderClear(&builder);
        xp3ValueUnref(range);
        return(-1);
    }
    xp3ValueUnref(range);
    all = xp3SeqBuilderFinish(&builder);
    if (all == NULL)
        return(-1);
    describe(all, out, size);
    xp3ValueUnref(all);

    a = xp3AtomicNewQName(BAD_CAST "urn:example", BAD_CAST "ex",
                          BAD_CAST "q");
    if (a == NULL)
        return(-1);
    xp3AtomicUnref(a);
    a = xp3AtomicNewString(XP3_TYPE_STRING, BAD_CAST "abc", 3);
    if (a == NULL)
        return(-1);
    xp3AtomicUnref(a);
    a = xp3AtomicNewBinary(XP3_TYPE_HEX_BINARY,
                           (const unsigned char *) "\x01\x02", 2);
    if (a == NULL)
        return(-1);
    all = xp3ValueSingle(xp3ItemFromAtomic(a));
    if (all == NULL)
        return(-1);
    xp3ValueUnref(all);
    dec = xp3DecimalFromInt64(5);
    if (dec == NULL)
        return(-1);
    half = xp3DecimalToDouble(dec);
    xp3DecimalFree(dec);
    if (isnan(half))
        return(-1);
    num = xp3DoubleToString(0.1);
    if (num == NULL)
        return(-1);
    xmlFree(num);
    return(0);
}

static void
testAllocationFailures(void) {
    char expected[128];
    char actual[128];
    int i;

    check(buildValues(expected, sizeof(expected)) == 0 &&
          strcmp(expected, "1 10 11 12") == 0, "reference values built");

    if (!xp3TestAllocInstall()) {
        printf("testvalue: allocation-failure tests skipped: this "
               "libxml2 ignores xmlMemSetup()\n");
        xp3TestAllocRestore();
        return;
    }
    for (i = 0; i < 1000; i++) {
        int ret;

        xp3TestAllocFailAt(i);
        ret = buildValues(actual, sizeof(actual));
        if (!xp3TestAllocFailed()) {
            check(ret == 0 && strcmp(actual, expected) == 0,
                  "values complete after allocation failures");
            xp3TestAllocFailAt(-1);
            break;
        }
        xp3TestAllocFailAt(-1);
        /* A failure inside describe() only shortens the description,
         * so only a failed build has to return -1. */
    }
    check(i > 0 && i < 1000, "allocation failure points exercised");
    xp3TestAllocRestore();
}

/*
 * Constants hold atomic values only. Constant folding asks first, so
 * that a value with nodes is a fold skipped, not a failed allocation.
 */
static void
testConstants(void) {
    xmlNode node;
    xp3SeqBuilder builder;
    xp3Value *nodes;
    xp3Value *mixed;
    xp3Value *atomics;
    xp3Value *range = NULL;
    xp3Value *constant;

    memset(&node, 0, sizeof(node));
    node.type = XML_ELEMENT_NODE;
    nodes = xp3ValueSingle(xp3ItemFromNode(&node));
    xp3SeqBuilderInit(&builder);
    xp3SeqBuilderAdd(&builder, xp3ItemFromAtomic(xp3AtomicNewInteger(1)));
    xp3SeqBuilderAdd(&builder, xp3ItemFromNode(&node));
    mixed = xp3SeqBuilderFinish(&builder);
    xp3SeqBuilderInit(&builder);
    xp3SeqBuilderAdd(&builder, xp3ItemFromAtomic(xp3AtomicNewInteger(1)));
    xp3SeqBuilderAdd(&builder, xp3ItemFromAtomic(xp3AtomicNewBoolean(0)));
    atomics = xp3SeqBuilderFinish(&builder);
    xp3ValueRange(1, 3, &range);
    if ((nodes == NULL) || (mixed == NULL) || (atomics == NULL) ||
        (range == NULL)) {
        check(0, "constant test values");
        goto done;
    }

    check(!xp3ValueIsAtomic(nodes), "a node is not atomic");
    check(!xp3ValueIsAtomic(mixed), "a sequence with a node is not atomic");
    check(xp3ValueIsAtomic(atomics), "atomic values");
    check(xp3ValueIsAtomic(range), "a range is atomic");
    check(xp3ValueIsAtomic(xp3ValueEmpty()), "the empty sequence");
    check(xp3ValueMakeConstant(nodes) == NULL, "no constant with a node");
    check(xp3ValueMakeConstant(mixed) == NULL,
          "no constant with a node after an atomic value");
    constant = xp3ValueMakeConstant(atomics);
    check((constant != NULL) && (constant->refs == XP3_REFS_CONSTANT),
          "a constant of atomic values");
    xp3ValueFreeConstant(constant);

done:
    xp3ValueUnref(nodes);
    xp3ValueUnref(mixed);
    xp3ValueUnref(atomics);
    xp3ValueUnref(range);
}

int
main(void) {
    xmlInitParser();

    testTypes();
    testPublicTypes();
    testAtomics();
    testSequences();
    testRanges();
    testConstants();
    testNumberStrings("");
    testParseAllocation(0);
    testLocale();
    testAtomicStrings();
    testAllocationFailures();

    if (failures == 0)
        printf("testvalue: OK\n");
    return(failures == 0 ? 0 : 1);
}
