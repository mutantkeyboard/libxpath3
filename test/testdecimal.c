/*
 * testdecimal.c: tests for arbitrary-precision decimal arithmetic
 *
 * Expected values of add, sub, mul, idiv and mod come from Python's
 * decimal module with 1000 digits of precision. Expected values of div
 * were measured with Saxon-HE 10.9 and 12.5, which agree on every case
 * (doc/DESIGN.md 4.3).
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/xmlmemory.h>

#include "private/xp3decimal.h"
#include "testalloc.h"

static int failures = 0;

static void
check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static const char *
codeName(xp3ErrCode code) {
    const char *name = xp3ErrCodeName(code);

    return((name != NULL) ? name : "?");
}

/* Parse a decimal that must be valid. NULL on failure. */
static xp3Decimal *
dec(const char *str) {
    xp3Decimal *d = NULL;
    xp3ErrCode code = xp3DecimalParse(BAD_CAST str, strlen(str), 0, &d);
    char what[256];

    snprintf(what, sizeof(what), "parse '%.100s': %s", str, codeName(code));
    check(code == XP3_ERR_OK, what);
    return(d);
}

/* Check that a decimal prints as expected, then free it. */
static void
expectStr(xp3Decimal *d, const char *expected, const char *what) {
    xmlChar *str = xp3DecimalToString(d);
    char msg[512];

    snprintf(msg, sizeof(msg), "%s: got %.150s, expected %.150s", what,
             str ? (const char *) str : "(null)", expected);
    check((str != NULL) && (strcmp((const char *) str, expected) == 0), msg);
    xmlFree(str);
    xp3DecimalFree(d);
}

static const struct {
    const char *input;
    const char *canonical;
} parseCases[] = {
    { "0", "0" },
    { "-0", "0" },
    { "+0.000", "0" },
    { "000123", "123" },
    { "1.50", "1.5" },
    { "-.5", "-0.5" },
    { "5.", "5" },
    { "+5", "5" },
    { "0.001", "0.001" },
    { "123456789", "123456789" },
    { "1234567890", "1234567890" },
    { "1000000000", "1000000000" },
    { "999999999999999999", "999999999999999999" },
    { "0.000000001", "0.000000001" },
    { "0.0000000001", "0.0000000001" },
    { "123456789.123456789", "123456789.123456789" },
    { "-0099999999999999999999999999999.9999999999999999999900",
      "-99999999999999999999999999999.99999999999999999999" }
};

static const char *const invalidCases[] = {
    "", "-", "+", ".", "+.", "1.2.3", "1e5", " 1", "1 ", "abc", "1,5",
    "--1", "0x10", "1-"
};

static void
testParse(void) {
    size_t i;

    for (i = 0; i < sizeof(parseCases) / sizeof(parseCases[0]); i++) {
        char what[128];

        snprintf(what, sizeof(what), "canonical form of '%s'",
                 parseCases[i].input);
        expectStr(dec(parseCases[i].input), parseCases[i].canonical, what);
    }

    for (i = 0; i < sizeof(invalidCases) / sizeof(invalidCases[0]); i++) {
        xp3Decimal *d = NULL;
        char what[64];

        snprintf(what, sizeof(what), "'%s' is not a decimal",
                 invalidCases[i]);
        check(xp3DecimalParse(BAD_CAST invalidCases[i],
                              strlen(invalidCases[i]), 0, &d) ==
              XP3_ERR_FORG0001 && d == NULL, what);
    }

    /* Integers only */
    {
        xp3Decimal *d = NULL;

        check(xp3DecimalParse(BAD_CAST "-12", 3, 1, &d) == XP3_ERR_OK,
              "-12 is an integer");
        expectStr(d, "-12", "integer value");
        d = NULL;
        check(xp3DecimalParse(BAD_CAST "1.0", 3, 1, &d) == XP3_ERR_FORG0001,
              "1.0 is not the lexical form of an integer");
    }

    /* The length is respected, so text need not be terminated. */
    {
        xp3Decimal *d = NULL;

        check(xp3DecimalParse(BAD_CAST "12.5xyz", 4, 0, &d) == XP3_ERR_OK,
              "length-limited parse");
        expectStr(d, "12.5", "length-limited value");
    }
}

static const struct {
    const char *a;
    const char *op;
    const char *b;
    const char *expected;
} arithCases[] = {
    /* Carries and borrows across limbs */
    { "999999999", "+", "1", "1000000000" },
    { "1000000000", "-", "1", "999999999" },
    { "99999999999999999999", "+", "1", "100000000000000000000" },
    { "1", "-", "100000000000000000000", "-99999999999999999999" },
    /* Scales and signs */
    { "0.1", "+", "0.2", "0.3" },
    { "1.5", "-", "1.5", "0" },
    { "-1", "+", "1", "0" },
    { "-0.001", "-", "0.009", "-0.01" },
    { "123456789.987654321", "+", "0.012345679", "123456790" },
    /* Multiplication */
    { "123456789012345678901234567890", "*",
      "987654321098765432109876543210",
      "121932631137021795226185032733622923332237463801111263526900" },
    { "0.001", "*", "0.001", "0.000001" },
    { "-2.5", "*", "4", "-10" },
    { "-1.1", "*", "-1.1", "1.21" },
    { "999999999", "*", "999999999", "999999998000000001" },
    { "0", "*", "-5", "0" },
    { "1000000000", "*", "0.000000001", "1" },
    /* Integer division truncates toward zero */
    { "7", "idiv", "2", "3" },
    { "-7", "idiv", "2", "-3" },
    { "7", "idiv", "-2", "-3" },
    { "7.5", "idiv", "2", "3" },
    { "-0.5", "idiv", "3", "0" },
    { "123456789012345678901234567890", "idiv", "7",
      "17636684144620811271604938270" },
    /* The remainder has the sign of the dividend */
    { "7", "mod", "2", "1" },
    { "-7", "mod", "2", "-1" },
    { "7", "mod", "-2", "1" },
    { "7.5", "mod", "2", "1.5" },
    { "10", "mod", "3.3", "0.1" },
    { "-0.5", "mod", "3", "-0.5" },
    { "6", "mod", "2", "0" },
    /* An IBAN check: a 24-digit integer mod 97 */
    { "370400440532013000131489", "mod", "97", "1" },

    /*
     * Division, measured with Saxon: max(18, s1 - s2 + 18) digits after
     * the point, ties toward zero. "div" types the operands as XPath
     * types literals: without a point they are xs:integer, which changes
     * how their scale counts (xp3DecimalDivTyped()). "ddiv" divides two
     * xs:decimal values, as xs:decimal('1') div xs:decimal('300000').
     */
    { "1", "div", "3", "0.333333333333333333" },
    { "0", "div", "3", "0" },
    { "0", "div", "-0.7", "0" },
    { "0", "ddiv", "300000", "0" },
    { "2", "div", "3", "0.666666666666666667" },
    { "10", "div", "3", "3.333333333333333333" },
    { "1000000", "div", "3", "333333.333333333333333333" },
    { "1", "div", "300000", "0.000003333333333333" },
    { "0.001", "div", "3", "0.000333333333333333333" },
    { "1", "div", "7", "0.142857142857142857" },
    { "1.000000000000000000000000000001", "div", "3",
      "0.333333333333333333333333333333666666666666666667" },
    { "123456789012345678901234567890", "div", "7",
      "17636684144620811271604938270" },
    { "1", "div", "1.6", "0.625" },
    { "0.5", "div", "8", "0.0625" },
    { "1", "div", "2048", "0.00048828125" },
    { "2", "div", "2048", "0.0009765625" },
    { "1", "div", "4", "0.25" },
    { "100", "div", "4", "25" },
    { "1.00", "div", "4", "0.25" },
    { "1.0", "div", "3", "0.333333333333333333" },
    { "1.000", "div", "3", "0.333333333333333333" },
    { "1", "div", "3.0", "0.333333333333333333" },
    { "1.50", "div", "3", "0.5" },
    { "1", "div", "0.003", "333.333333333333333333" },
    { "1", "div", "0.000000000000000000000003",
      "333333333333333333333333.333333333333333333" },
    { "0.001", "div", "0.003", "0.333333333333333333" },
    { "1.5", "div", "0.5", "3" },
    { "25", "div", "10000000000000000000", "0.0000000000000000025" },
    { "15", "div", "10000000000000000000", "0.0000000000000000015" },
    { "35", "div", "10000000000000000000", "0.0000000000000000035" },
    { "-25", "div", "10000000000000000000", "-0.0000000000000000025" },
    { "251", "div", "100000000000000000000", "0.00000000000000000251" },
    { "249", "div", "100000000000000000000", "0.00000000000000000249" },
    { "7", "div", "2", "3.5" },
    { "6", "div", "2", "3" },
    { "0.00001", "div", "0.3", "0.0000333333333333333333" },
    { "0.1", "div", "0.3", "0.333333333333333333" },
    { "12.5", "div", "0.7", "17.857142857142857143" },
    { "0.0010", "div", "3", "0.000333333333333333333" },
    { "-2", "div", "3", "-0.666666666666666667" },
    { "-1", "div", "3", "-0.333333333333333333" },
    { "1", "div", "1267650600228229401496703205376", "0" },
    { "3", "div", "1180591620717411303424", "0" },
    { "0.000000000000000000001", "div", "3",
      "0.000000000000000000000333333333333333333" },
    { "5", "div", "6", "0.833333333333333333" },
    { "1", "div", "0.7", "1.428571428571428571" },
    { "100", "div", "0.0007", "142857.142857142857142857" },
    /* Exact ties at the last digit go toward zero, not to even */
    { "1", "div", "524288", "0.000001907348632812" },
    { "3", "div", "524288", "0.000005722045898437" },
    { "5", "div", "524288", "0.000009536743164062" },
    { "-1", "div", "524288", "-0.000001907348632812" },
    { "-3", "div", "524288", "-0.000005722045898437" },
    /* Integer operands: scale 0 when both are in [0, 2^31) ... */
    { "1", "div", "300000", "0.000003333333333333" },
    { "1", "div", "2140000000", "0.00000000046728972" },
    /* ... otherwise both are stripped, also for negatives */
    { "1", "div", "-2140000000", "-0.0000000004672897196261682" },
    { "1", "div", "2147483650", "0.0000000004656612869" },
    { "1", "div", "3000000000000000000",
      "0.000000000000000000333333333333333333" },
    { "10000000000000000000", "div", "3000000000",
      "3333333333.333333333333333333" },
    { "21400000000", "div", "7000000000", "3.0571428571428571429" },
    { "3000000000", "div", "7", "428571428.571428571428571429" },
    { "12345678901234567890", "div", "98765432109876543210",
      "0.1249999988609375" },
    /* An integer next to a decimal keeps scale 0; decimals are
     * stripped */
    { "1", "div", "300000.0", "0.00000333333333333333333" },
    { "1.0", "div", "300000", "0.000003333333333333" },
    { "0.5", "div", "300000", "0.0000016666666666667" },
    { "1", "ddiv", "300000", "0.00000333333333333333333" },
    { "25", "ddiv", "10000000000000000000", "0.0000000000000000025" }
};

static xp3ErrCode
applyOp(const char *op, const xp3Decimal *a, const xp3Decimal *b,
        xp3Decimal **res, int aIsInteger, int bIsInteger) {
    if (strcmp(op, "div") == 0)
        return(xp3DecimalDivTyped(a, aIsInteger, b, bIsInteger, res));
    if (strcmp(op, "+") == 0)
        return(xp3DecimalAdd(a, b, res));
    if (strcmp(op, "-") == 0)
        return(xp3DecimalSub(a, b, res));
    if (strcmp(op, "*") == 0)
        return(xp3DecimalMul(a, b, res));
    if (strcmp(op, "ddiv") == 0)
        return(xp3DecimalDiv(a, b, res));
    if (strcmp(op, "idiv") == 0)
        return(xp3DecimalIdiv(a, b, res));
    return(xp3DecimalMod(a, b, res));
}

static void
testArithmetic(void) {
    size_t i;

    for (i = 0; i < sizeof(arithCases) / sizeof(arithCases[0]); i++) {
        xp3Decimal *a = dec(arithCases[i].a);
        xp3Decimal *b = dec(arithCases[i].b);
        xp3Decimal *r = NULL;
        xp3ErrCode code;
        char what[256];

        if ((a == NULL) || (b == NULL)) {
            xp3DecimalFree(a);
            xp3DecimalFree(b);
            continue;
        }
        snprintf(what, sizeof(what), "%.60s %s %.60s", arithCases[i].a,
                 arithCases[i].op, arithCases[i].b);
        code = applyOp(arithCases[i].op, a, b, &r,
                       strchr(arithCases[i].a, '.') == NULL,
                       strchr(arithCases[i].b, '.') == NULL);
        if (code != XP3_ERR_OK) {
            char msg[320];

            snprintf(msg, sizeof(msg), "%s: error %s", what, codeName(code));
            check(0, msg);
        } else {
            expectStr(r, arithCases[i].expected, what);
        }
        xp3DecimalFree(a);
        xp3DecimalFree(b);
    }
}

static void
testDivisionByZero(void) {
    static const char *const ops[] = { "div", "idiv", "mod" };
    xp3Decimal *a = dec("1.5");
    xp3Decimal *zero = dec("0.000");
    size_t i;

    for (i = 0; i < 3; i++) {
        xp3Decimal *r = NULL;
        char what[64];

        snprintf(what, sizeof(what), "1.5 %s 0 raises FOAR0001", ops[i]);
        check(applyOp(ops[i], a, zero, &r, 0, 0) == XP3_ERR_FOAR0001 &&
              r == NULL, what);
    }
    xp3DecimalFree(a);
    xp3DecimalFree(zero);
}

/*
 * Divisions that take Knuth's add-back step (Algorithm D, step D6),
 * which random operands reach only about twice in 10^9 quotient limbs.
 * They were found by searching structured operands with a Python model
 * of magDivMod(); quotient and remainder are exact.
 */
static const struct {
    const char *a;
    const char *b;
    const char *quotient;
    const char *remainder;
} addBackCases[] = {
    { "500000000499999999000000001500000001000000001",
      "999999999000000000802472295000000000",
      "500000000", "999999998598763854000000001000000001" },
    { "499999999500000000499999999000000000000000000928906249",
      "999999998000000001000000000999999998",
      "500000000500000000", "999999998000000000500000001928906249" },
    { "500000000000000000500000000999999999",
      "500000000500000001999999998",
      "999999998", "499999999500000006999999995" },
    { "500000000000000000000000002500000000500000001499999999",
      "500000000000000000500000000999999999",
      "999999999999999999", "1500000002000000002499999998" }
};

static void
testAddBack(void) {
    size_t i;

    for (i = 0; i < sizeof(addBackCases) / sizeof(addBackCases[0]); i++) {
        xp3Decimal *a = dec(addBackCases[i].a);
        xp3Decimal *b = dec(addBackCases[i].b);
        xp3Decimal *r = NULL;
        char what[64];

        if ((a == NULL) || (b == NULL)) {
            xp3DecimalFree(a);
            xp3DecimalFree(b);
            continue;
        }
        snprintf(what, sizeof(what), "add-back case %lu quotient",
                 (unsigned long) i);
        check(xp3DecimalIdiv(a, b, &r) == XP3_ERR_OK, what);
        expectStr(r, addBackCases[i].quotient, what);
        snprintf(what, sizeof(what), "add-back case %lu remainder",
                 (unsigned long) i);
        r = NULL;
        check(xp3DecimalMod(a, b, &r) == XP3_ERR_OK, what);
        expectStr(r, addBackCases[i].remainder, what);
        xp3DecimalFree(a);
        xp3DecimalFree(b);
    }
}

/* xorshift64, for repeatable random tests */
static uint64_t rngState = 0x9E3779B97F4A7C15ull;

static uint64_t
rng(void) {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 7;
    rngState ^= rngState << 17;
    return(rngState);
}

/*
 * A random number as text: up to maxDigits digits drawn mostly from
 * runs of 9s and 0s, which put carries and limb boundaries under
 * stress, with an optional point and sign.
 */
static void
randomNumber(char *buf, size_t maxDigits, int allowFraction) {
    size_t n = 1 + (size_t) (rng() % maxDigits);
    size_t point = (allowFraction && (rng() % 2)) ? (size_t) (rng() % n) : n;
    size_t i;
    char *out = buf;

    if (rng() % 2)
        *out++ = '-';
    for (i = 0; i < n; i++) {
        unsigned kind = (unsigned) (rng() % 4);

        if ((i == point) && (i > 0))
            *out++ = '.';
        if (kind == 0)
            *out++ = '9';
        else if (kind == 1)
            *out++ = '0';
        else
            *out++ = (char) ('0' + rng() % 10);
    }
    *out = 0;
}

/*
 * For random a and b: q = a idiv b and r = a mod b must satisfy
 * q * b + r = a, |r| < |b|, and r has the sign of a or is 0. This
 * checks magDivMod() without needing a second implementation.
 */
static void
testDivisionProperties(void) {
    char abuf[400];
    char bbuf[400];
    int bad = 0;
    int i;

    for (i = 0; i < 3000; i++) {
        xp3Decimal *a;
        xp3Decimal *b;
        xp3Decimal *q = NULL;
        xp3Decimal *r = NULL;
        xp3Decimal *prod = NULL;
        xp3Decimal *sum = NULL;
        xp3Decimal *absR = NULL;
        xp3Decimal *absB = NULL;

        randomNumber(abuf, 300, 1);
        randomNumber(bbuf, 120, 1);
        a = dec(abuf);
        b = dec(bbuf);
        if ((a == NULL) || (b == NULL) || (xp3DecimalSign(b) == 0)) {
            xp3DecimalFree(a);
            xp3DecimalFree(b);
            continue;
        }
        if ((xp3DecimalIdiv(a, b, &q) != XP3_ERR_OK) ||
            (xp3DecimalMod(a, b, &r) != XP3_ERR_OK) ||
            (xp3DecimalMul(q, b, &prod) != XP3_ERR_OK) ||
            (xp3DecimalAdd(prod, r, &sum) != XP3_ERR_OK) ||
            (xp3DecimalCompare(sum, a) != 0) ||
            !xp3DecimalIsInteger(q)) {
            bad++;
        } else {
            /* |r| < |b|, and r has the sign of a or is 0 */
            const xp3Decimal *magR = r;
            const xp3Decimal *magB = b;

            if (xp3DecimalSign(r) < 0) {
                xp3DecimalNegate(r, &absR);
                magR = absR;
            }
            if (xp3DecimalSign(b) < 0) {
                xp3DecimalNegate(b, &absB);
                magB = absB;
            }
            if ((magR == NULL) || (magB == NULL) ||
                (xp3DecimalCompare(magR, magB) >= 0) ||
                ((xp3DecimalSign(r) != 0) &&
                 (xp3DecimalSign(r) != xp3DecimalSign(a))))
                bad++;
        }
        if (bad == 1) {
            fprintf(stderr, "first failing operands: %s and %s\n",
                    abuf, bbuf);
            bad++;
        }
        xp3DecimalFree(a);
        xp3DecimalFree(b);
        xp3DecimalFree(q);
        xp3DecimalFree(r);
        xp3DecimalFree(prod);
        xp3DecimalFree(sum);
        xp3DecimalFree(absR);
        xp3DecimalFree(absB);
    }
    check(bad == 0, "q * b + r = a for 3000 random divisions");
}

static const struct {
    const char *value;
    int precision;
    xp3RoundingMode mode;
    const char *expected;
} roundCases[] = {
    /* fn:round: ties toward positive infinity */
    { "2.5", 0, XP3_ROUND_HALF_CEILING, "3" },
    { "-2.5", 0, XP3_ROUND_HALF_CEILING, "-2" },
    { "1.125", 2, XP3_ROUND_HALF_CEILING, "1.13" },
    { "-1.125", 2, XP3_ROUND_HALF_CEILING, "-1.12" },
    { "12345", -2, XP3_ROUND_HALF_CEILING, "12300" },
    { "12350", -2, XP3_ROUND_HALF_CEILING, "12400" },
    /* fn:round-half-to-even */
    { "2.5", 0, XP3_ROUND_HALF_EVEN, "2" },
    { "3.5", 0, XP3_ROUND_HALF_EVEN, "4" },
    { "-2.5", 0, XP3_ROUND_HALF_EVEN, "-2" },
    { "1.125", 2, XP3_ROUND_HALF_EVEN, "1.12" },
    { "5", -1, XP3_ROUND_HALF_EVEN, "0" },
    { "15", -1, XP3_ROUND_HALF_EVEN, "20" },
    /* Other nearest modes */
    { "2.5", 0, XP3_ROUND_HALF_UP, "3" },
    { "-2.5", 0, XP3_ROUND_HALF_UP, "-3" },
    { "2.5", 0, XP3_ROUND_HALF_DOWN, "2" },
    { "-2.5", 0, XP3_ROUND_HALF_DOWN, "-2" },
    { "2.51", 0, XP3_ROUND_HALF_DOWN, "3" },
    /* Directed modes */
    { "1.9", 0, XP3_ROUND_FLOOR, "1" },
    { "-1.1", 0, XP3_ROUND_FLOOR, "-2" },
    { "1.1", 0, XP3_ROUND_CEILING, "2" },
    { "-1.9", 0, XP3_ROUND_CEILING, "-1" },
    { "-1.9", 0, XP3_ROUND_DOWN, "-1" },
    { "999.999", 2, XP3_ROUND_CEILING, "1000" },
    /* Precision at or above the scale changes nothing */
    { "1.25", 2, XP3_ROUND_HALF_EVEN, "1.25" },
    { "1.25", 7, XP3_ROUND_FLOOR, "1.25" },
    /* Far below the value */
    { "5", -1000000, XP3_ROUND_HALF_EVEN, "0" },
    { "5", -1000000, XP3_ROUND_FLOOR, "0" },
    { "-5", -50, XP3_ROUND_DOWN, "0" },
    { "0.0001", 2, XP3_ROUND_CEILING, "0.01" },
    { "-0.0001", 2, XP3_ROUND_FLOOR, "-0.01" }
};

static void
testRound(void) {
    size_t i;
    xp3Decimal *five;
    xp3Decimal *r = NULL;

    for (i = 0; i < sizeof(roundCases) / sizeof(roundCases[0]); i++) {
        xp3Decimal *a = dec(roundCases[i].value);
        xp3Decimal *res = NULL;
        char what[96];

        if (a == NULL)
            continue;
        snprintf(what, sizeof(what), "round(%s, %d) in mode %d",
                 roundCases[i].value, roundCases[i].precision,
                 (int) roundCases[i].mode);
        check(xp3DecimalRound(a, roundCases[i].precision, roundCases[i].mode,
                              &res) == XP3_ERR_OK, what);
        expectStr(res, roundCases[i].expected, what);
        xp3DecimalFree(a);
    }

    /* Rounding 5 up to a huge power of ten exceeds the digit limit. */
    five = dec("5");
    check(xp3DecimalRound(five, -200000, XP3_ROUND_CEILING, &r) ==
          XP3_ERR_LIMIT && r == NULL, "ceiling to 10^200000 hits LIMIT");
    xp3DecimalFree(five);
}

static void
testCompare(void) {
    static const struct {
        const char *a;
        const char *b;
        int expected;
    } cases[] = {
        { "0", "-0", 0 },
        { "1.50", "1.5", 0 },
        { "1", "2", -1 },
        { "-1", "-2", 1 },
        { "-1", "0", -1 },
        { "0.001", "0.0009", 1 },
        { "1000000000", "999999999.999999999", 1 },
        { "123456789012345678901234567890",
          "123456789012345678901234567891", -1 },
        { "-0.5", "-0.50000000000000000001", 1 },
        { "100", "1E2", 0 }
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        xp3Decimal *a;
        xp3Decimal *b;
        char what[128];

        if (strcmp(cases[i].b, "1E2") == 0)
            b = xp3DecimalFromInt64(100);
        else
            b = dec(cases[i].b);
        a = dec(cases[i].a);
        snprintf(what, sizeof(what), "compare %s with %s", cases[i].a,
                 cases[i].b);
        check(xp3DecimalCompare(a, b) == cases[i].expected, what);
        check(xp3DecimalCompare(b, a) == -cases[i].expected, what);
        xp3DecimalFree(a);
        xp3DecimalFree(b);
    }
}

static void
testConversions(void) {
    xp3Decimal *d;
    int64_t v = 0;

    d = xp3DecimalFromInt64(INT64_MIN);
    expectStr(d, "-9223372036854775808", "INT64_MIN as a decimal");
    d = xp3DecimalFromInt64(INT64_MAX);
    check(xp3DecimalToInt64(d, &v) == 0 && v == INT64_MAX,
          "INT64_MAX round-trips");
    xp3DecimalFree(d);
    d = dec("-9223372036854775808");
    check(xp3DecimalToInt64(d, &v) == 0 && v == INT64_MIN,
          "INT64_MIN round-trips");
    xp3DecimalFree(d);
    d = dec("9223372036854775808");
    check(xp3DecimalToInt64(d, &v) == -1, "2^63 does not fit");
    xp3DecimalFree(d);
    d = dec("1.5");
    check(xp3DecimalToInt64(d, &v) == -1, "1.5 is not an integer");
    xp3DecimalFree(d);
    d = xp3DecimalFromInt64(0);
    expectStr(d, "0", "zero from int64");

    d = dec("0.1");
    check(xp3DecimalToDouble(d) == 0.1, "0.1 to double");
    xp3DecimalFree(d);
    d = dec("-123.456");
    check(xp3DecimalToDouble(d) == -123.456, "-123.456 to double");
    xp3DecimalFree(d);
    {
        char big[420];

        memset(big, '9', 400);
        big[400] = 0;
        d = dec(big);
        check(isinf(xp3DecimalToDouble(d)), "huge decimal to infinity");
        xp3DecimalFree(d);
    }
}

static void
testLimits(void) {
    static char buf[2 * XP3_DECIMAL_MAX_DIGITS + 8];
    xp3Decimal *d = NULL;
    xp3Decimal *big;
    xp3Decimal *r = NULL;

    memset(buf, '7', XP3_DECIMAL_MAX_DIGITS + 1);
    check(xp3DecimalParse(BAD_CAST buf, XP3_DECIMAL_MAX_DIGITS, 0, &d) ==
          XP3_ERR_OK, "parse at the digit limit");
    big = d;
    d = NULL;
    check(xp3DecimalParse(BAD_CAST buf, XP3_DECIMAL_MAX_DIGITS + 1, 0, &d)
          == XP3_ERR_LIMIT && d == NULL, "parse above the digit limit");

    /* A tiny fraction counts its leading zeros. */
    buf[0] = '0';
    buf[1] = '.';
    memset(buf + 2, '0', XP3_DECIMAL_MAX_DIGITS);
    buf[2 + XP3_DECIMAL_MAX_DIGITS] = '1';
    check(xp3DecimalParse(BAD_CAST buf, XP3_DECIMAL_MAX_DIGITS + 3, 0, &d)
          == XP3_ERR_LIMIT, "scale above the digit limit");

    /* The product of two numbers at the limit is above it. */
    check(xp3DecimalMul(big, big, &r) == XP3_ERR_LIMIT && r == NULL,
          "product above the digit limit");

    /* 1 div 10^-100000 is 10^100000, one digit above the limit. */
    memset(buf + 2, '0', XP3_DECIMAL_MAX_DIGITS - 1);
    buf[2 + XP3_DECIMAL_MAX_DIGITS - 1] = '1';
    d = NULL;
    if (xp3DecimalParse(BAD_CAST buf, 2 + XP3_DECIMAL_MAX_DIGITS, 0, &d) ==
        XP3_ERR_OK) {
        xp3Decimal *one = dec("1");

        check(xp3DecimalDiv(one, d, &r) == XP3_ERR_LIMIT,
              "quotient above the digit limit");
        xp3DecimalFree(one);
    } else {
        check(0, "parse 10^-100000");
    }
    xp3DecimalFree(d);
    xp3DecimalFree(big);
}

/* A chain of operations for the allocation-failure test. Returns the
 * printed result, or NULL with *code set when something failed. */
static xmlChar *
operationChain(xp3ErrCode *code) {
    static const char a[] = "-12345678901234567890.123456789";
    static const char b[] = "987654321987654321.7";
    xp3Decimal *x = NULL;
    xp3Decimal *y = NULL;
    xp3Decimal *t1 = NULL;
    xp3Decimal *t2 = NULL;
    xp3Decimal *t3 = NULL;
    xp3Decimal *t4 = NULL;
    xp3Decimal *t5 = NULL;
    xmlChar *ret = NULL;

    if (((*code = xp3DecimalParse(BAD_CAST a, strlen(a), 0, &x)) != 0) ||
        ((*code = xp3DecimalParse(BAD_CAST b, strlen(b), 0, &y)) != 0) ||
        ((*code = xp3DecimalMul(x, y, &t1)) != 0) ||
        ((*code = xp3DecimalDiv(t1, y, &t2)) != 0) ||
        ((*code = xp3DecimalSub(t2, x, &t3)) != 0) ||
        ((*code = xp3DecimalMod(t1, y, &t4)) != 0) ||
        ((*code = xp3DecimalRound(t4, -3, XP3_ROUND_HALF_EVEN, &t5)) != 0))
        goto done;
    ret = xp3DecimalToString(t5);
    if (ret == NULL)
        *code = XP3_ERR_NO_MEMORY;

done:
    xp3DecimalFree(x);
    xp3DecimalFree(y);
    xp3DecimalFree(t1);
    xp3DecimalFree(t2);
    xp3DecimalFree(t3);
    xp3DecimalFree(t4);
    xp3DecimalFree(t5);
    return(ret);
}

static void
testAllocationFailures(void) {
    xmlChar *expected;
    xp3ErrCode code;
    int i;

    expected = operationChain(&code);
    check(expected != NULL, "reference chain computed");

    if (!xp3TestAllocInstall()) {
        printf("testdecimal: allocation-failure tests skipped: this "
               "libxml2 ignores xmlMemSetup()\n");
        xp3TestAllocRestore();
        xmlFree(expected);
        return;
    }

    for (i = 0; i < 1000; i++) {
        xmlChar *actual;

        xp3TestAllocFailAt(i);
        actual = operationChain(&code);
        if (!xp3TestAllocFailed()) {
            check(actual != NULL && expected != NULL &&
                  xmlStrEqual(actual, expected),
                  "chain complete after allocation failures");
            xp3TestAllocFailAt(-1);
            xmlFree(actual);
            break;
        }
        xp3TestAllocFailAt(-1);
        check(actual == NULL && code == XP3_ERR_NO_MEMORY,
              "failed allocation reported as NO_MEMORY");
        xmlFree(actual);
    }
    check(i > 0 && i < 1000, "allocation failure points exercised");

    xp3TestAllocRestore();
    xmlFree(expected);
}

int
main(void) {
    xmlInitParser();

    testParse();
    testArithmetic();
    testDivisionByZero();
    testAddBack();
    testDivisionProperties();
    testRound();
    testCompare();
    testConversions();
    testLimits();
    testAllocationFailures();

    if (failures == 0)
        printf("testdecimal: OK\n");
    return(failures == 0 ? 0 : 1);
}
