/*
 * xp3double.c: converting doubles and floats to and from strings
 *
 * Casting xs:double or xs:float to xs:string needs the shortest digits
 * that read back as exactly the same value. They are found with a
 * round-trip search: format with 1, 2, ... significant digits and keep
 * the first result that parses back to the original. 17 digits always
 * round-trip for a double and 9 for a float, so the search ends. This
 * is about 30 lines instead of a vendored copy of Ryu; doc/DESIGN.md
 * section 4.4 has the trade-off and when to revisit it.
 *
 * snprintf() and strtod() use the decimal point of the current
 * locale, which an application may have set to ",". Both directions
 * are translated, so results never depend on the locale.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

#include "private/xp3double.h"

/* Room for "-d.dddddddddddddddde-308" and a locale's point */
#define XP3_NUM_BUF 64

/* Plain notation between these, exponent notation outside (F&O 3.1,
 * 19.1.2.2) */
#define XP3_PLAIN_MIN 1e-6
#define XP3_PLAIN_MAX 1e6

/**
 * Get a quiet NaN.
 *
 * @returns NaN
 */
double
xp3DoubleNaN(void) {
    return(NAN);
}

/*
 * Parse a number in C syntax with strtod(), or with strtof() if isFloat
 * is set, after replacing its "." with the locale's decimal point. The
 * copy goes into a buffer on the stack, or on the heap if it doesn't
 * fit, as for decimals with many digits: strtod() rounds correctly only
 * if it sees all of them. Returns 0, or -1 if memory allocation failed.
 */
static int
parseNumber(const char *str, int isFloat, double *value) {
    const char *point = localeconv()->decimal_point;
    const char *dot = strchr(str, '.');
    char buf[XP3_NUM_BUF];
    char *copy = buf;
    size_t pointLen;
    size_t before;
    size_t size;

    if ((dot == NULL) || (point == NULL) || (strcmp(point, ".") == 0)) {
        *value = isFloat ? (double) strtof(str, NULL) : strtod(str, NULL);
        return(0);
    }
    pointLen = strlen(point);
    before = (size_t) (dot - str);
    size = before + pointLen + strlen(dot + 1) + 1;
    if (size > sizeof(buf)) {
        copy = xmlMalloc(size);
        if (copy == NULL)
            return(-1);
    }
    memcpy(copy, str, before);
    memcpy(copy + before, point, pointLen);
    strcpy(copy + before + pointLen, dot + 1);
    *value = isFloat ? (double) strtof(copy, NULL) : strtod(copy, NULL);
    if (copy != buf)
        xmlFree(copy);
    return(0);
}

/**
 * Parse a number in C syntax with "." as the decimal point, whatever
 * the locale, rounding correctly as strtod() does.
 *
 * @param str  the number, for example "1.5e-3"
 * @param value  set to the double; out-of-range values become infinity
 *               or zero
 * @returns 0, or -1 if memory allocation failed
 */
int
xp3DoubleParse(const char *str, double *value) {
    return(parseNumber(str, 0, value));
}

/**
 * Parse a number as a float, as xp3DoubleParse() does for doubles.
 *
 * @param str  the number
 * @param value  set to the float
 * @returns 0, or -1 if memory allocation failed
 */
int
xp3FloatParse(const char *str, float *value) {
    double result;

    if (parseNumber(str, 1, &result) < 0)
        return(-1);
    *value = (float) result;
    return(0);
}

/*
 * The shortest significant digits of a positive finite value that read
 * back as the same value: a double, or a float if isFloat is set.
 * Stores at most 17 digits in digits, and the power of ten of the
 * first digit in *exponent. Returns the number of digits.
 *
 * The digits never end in 0. If the p digits that round-trip were
 * D followed by 0, the value would be within half a unit of the p-th
 * digit of D, so rounding it to p - 1 digits gives D as well, which
 * reads back as the same value and would have been found first.
 */
static int
shortestDigits(double value, int isFloat, char *digits, int *exponent) {
    int maxPrec = isFloat ? 9 : 17;
    int prec;
    int n = 0;

    for (prec = 1; prec <= maxPrec; prec++) {
        char buf[XP3_NUM_BUF];
        char canon[XP3_NUM_BUF];
        const char *cur;
        double back;

        snprintf(buf, sizeof(buf), "%.*e", prec - 1, value);

        /* Take the digits and the exponent, skipping whatever decimal
         * point the locale printed. */
        n = 0;
        for (cur = buf; (*cur != 0) && (*cur != 'e') && (*cur != 'E');
             cur++) {
            if ((*cur >= '0') && (*cur <= '9'))
                digits[n++] = *cur;
        }
        *exponent = (*cur != 0) ? atoi(cur + 1) : 0;

        /* Read it back in C syntax. It is short enough for the buffer
         * of parseNumber(), so nothing is allocated and it can't fail. */
        snprintf(canon, sizeof(canon), "%c.%.*se%d", digits[0], n - 1,
                 (n > 1) ? digits + 1 : "0", *exponent);
        if ((parseNumber(canon, isFloat, &back) == 0) &&
            (isFloat ? ((float) back == (float) value) : (back == value)))
            break;
    }

    digits[n] = 0;
    return(n);
}

/*
 * The two significant digits closest to a positive finite value, for
 * exponent notation when one digit would round-trip: "d.d" needs two
 * anyway, and Saxon, through Java's toString(), shows the closer one.
 * That only differs from padding with 0 for the smallest subnormals,
 * for example 4.9E-324 and 1.4E-45 rather than 5.0E-324 and 1.0E-45
 * (doc/DESIGN.md 4.4). Both read back as the same value.
 */
static int
closestTwoDigits(double value, char *digits, int *exponent) {
    char buf[XP3_NUM_BUF];
    const char *cur;
    int n = 0;

    snprintf(buf, sizeof(buf), "%.1e", value);
    for (cur = buf; (*cur != 0) && (*cur != 'e') && (*cur != 'E'); cur++) {
        if ((*cur >= '0') && (*cur <= '9') && (n < 2))
            digits[n++] = *cur;
    }
    *exponent = (*cur != 0) ? atoi(cur + 1) : 0;
    digits[n] = 0;
    return(n);
}

/*
 * The canonical string of a double or float value (F&O 3.1, 19.1.2.2):
 * NaN, INF, -INF, 0 and -0 for the special values; plain notation
 * without trailing zeros between 1e-6 and 1e6, such as "0.1" or "5";
 * otherwise one digit, a point, at least one more digit and the
 * exponent, such as "1.0E6" or "1.5E-7".
 */
static xmlChar *
formatNumber(double value, int isFloat) {
    char digits[XP3_NUM_BUF];
    char out[XP3_NUM_BUF + 400];
    char *cur = out;
    double mag;
    int exponent;
    int n;
    int i;

    if (isnan(value))
        return(xmlStrdup(BAD_CAST "NaN"));
    if (isinf(value))
        return(xmlStrdup(BAD_CAST ((value < 0) ? "-INF" : "INF")));
    if (value == 0)
        return(xmlStrdup(BAD_CAST (signbit(value) ? "-0" : "0")));

    mag = fabs(value);
    n = shortestDigits(mag, isFloat, digits, &exponent);
    if (value < 0)
        *cur++ = '-';

    if ((mag >= XP3_PLAIN_MIN) && (mag < XP3_PLAIN_MAX)) {
        if (exponent >= 0) {
            /* Integer part, padded with zeros, then any fraction */
            for (i = 0; i <= exponent; i++)
                *cur++ = (i < n) ? digits[i] : '0';
            if (n > exponent + 1) {
                *cur++ = '.';
                for (i = exponent + 1; i < n; i++)
                    *cur++ = digits[i];
            }
        } else {
            *cur++ = '0';
            *cur++ = '.';
            for (i = -1; i > exponent; i--)
                *cur++ = '0';
            for (i = 0; i < n; i++)
                *cur++ = digits[i];
        }
        *cur = 0;
    } else {
        if (n == 1)
            n = closestTwoDigits(mag, digits, &exponent);
        snprintf(cur, sizeof(out) - (size_t) (cur - out), "%c.%sE%d",
                 digits[0], (n > 1) ? digits + 1 : "0", exponent);
    }
    return(xmlStrdup(BAD_CAST out));
}

/**
 * Convert a double to its canonical string, as casting xs:double to
 * xs:string does: the shortest digits that read back as the same
 * value, for example "0.1", "1.0E6", "NaN" or "-0".
 *
 * @param value  the double
 * @returns the string, to be freed with xmlFree(), or NULL if memory
 *          allocation failed
 */
xmlChar *
xp3DoubleToString(double value) {
    return(formatNumber(value, 0));
}

/**
 * Convert a float to its canonical string, as casting xs:float to
 * xs:string does. Shortest means shortest for a float, so the float
 * nearest to 0.1 gives "0.1".
 *
 * @param value  the float
 * @returns the string, to be freed with xmlFree(), or NULL if memory
 *          allocation failed
 */
xmlChar *
xp3FloatToString(float value) {
    return(formatNumber((double) value, 1));
}
