/*
 * xp3ops.c: operations on atomic values
 *
 * Arithmetic on numbers with type promotion (F&O 3.1, 4.2) and on
 * dates, times and durations (8.4 and 9.7), value comparison (XPath
 * 3.1, 3.7.1, and the comparison operators of F&O 3.1, also those of
 * dates, times and durations in 8.2 and 9.4), and casting between the
 * atomic types (F&O 3.1, 19). The evaluator
 * atomizes the operands and handles the empty sequence; the functions
 * here take single atomic values.
 *
 * Two casts follow choices recorded in doc/DESIGN.md 4.2: xs:double
 * to xs:decimal gives the shortest decimal that reads back as the
 * double, as Saxon does, and xs:double to xs:float rounds to nearest,
 * as QT3 expects, although F&O 3.1, 19.1.2.1 describes truncation.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

#include "private/xp3chars.h"
#include "private/xp3decimal.h"
#include "private/xp3double.h"
#include "private/xp3ops.h"

/*
 * Types
 */

/**
 * Whether a type is numeric: xs:decimal and the types derived from
 * it, xs:float or xs:double.
 *
 * @param type  the type
 * @returns 1 or 0
 */
int
xp3TypeIsNumeric(xp3TypeId type) {
    return(xp3TypeDerivesFrom(type, XP3_TYPE_DECIMAL) ||
           (type == XP3_TYPE_FLOAT) || (type == XP3_TYPE_DOUBLE));
}

/**
 * Whether a type is xs:integer or derived from it.
 *
 * @param type  the type
 * @returns 1 or 0
 */
int
xp3TypeIsInteger(xp3TypeId type) {
    return(xp3TypeDerivesFrom(type, XP3_TYPE_INTEGER));
}

/* Numeric types in the order of promotion (XPath 3.1, B.1) */
typedef enum {
    XP3_NUM_NONE = 0,
    XP3_NUM_INTEGER,
    XP3_NUM_DECIMAL,
    XP3_NUM_FLOAT,
    XP3_NUM_DOUBLE
} xp3NumKind;

static xp3NumKind
numKind(xp3TypeId type) {
    if (xp3TypeIsInteger(type))
        return(XP3_NUM_INTEGER);
    if (xp3TypeDerivesFrom(type, XP3_TYPE_DECIMAL))
        return(XP3_NUM_DECIMAL);
    if (type == XP3_TYPE_FLOAT)
        return(XP3_NUM_FLOAT);
    if (type == XP3_TYPE_DOUBLE)
        return(XP3_NUM_DOUBLE);
    return(XP3_NUM_NONE);
}

/*
 * Making values
 */

static xp3ErrCode
newDecimal(xp3TypeId type, xp3Decimal *dec, xp3Atomic **res) {
    *res = xp3AtomicNewDecimal(type, dec);
    return((*res != NULL) ? XP3_ERR_OK : XP3_ERR_NO_MEMORY);
}

static xp3ErrCode
newDouble(double value, xp3Atomic **res) {
    *res = xp3AtomicNewDouble(value);
    return((*res != NULL) ? XP3_ERR_OK : XP3_ERR_NO_MEMORY);
}

static xp3ErrCode
newFloat(float value, xp3Atomic **res) {
    *res = xp3AtomicNewFloat(value);
    return((*res != NULL) ? XP3_ERR_OK : XP3_ERR_NO_MEMORY);
}

static xp3ErrCode
newBoolean(int value, xp3Atomic **res) {
    *res = xp3AtomicNewBoolean(value);
    return((*res != NULL) ? XP3_ERR_OK : XP3_ERR_NO_MEMORY);
}

static xp3ErrCode
newDateTime(xp3TypeId type, const xp3DateTime *value, xp3Atomic **res) {
    *res = xp3AtomicNewDateTime(type, value);
    if (*res == NULL)
        return(XP3_ERR_NO_MEMORY);
    return(XP3_ERR_OK);
}

static xp3ErrCode
newDuration(xp3TypeId type, const xp3Duration *value, xp3Atomic **res) {
    *res = xp3AtomicNewDuration(type, value);
    if (*res == NULL)
        return(XP3_ERR_NO_MEMORY);
    return(XP3_ERR_OK);
}

/*
 * Numeric conversions
 */

/* A number in C syntax as a double, or as a float stored in a double if
 * isFloat is set */
static xp3ErrCode
parseNumberString(const xmlChar *str, int isFloat, double *value) {
    float single;

    if (!isFloat)
        return((xp3DoubleParse((const char *) str, value) == 0) ?
               XP3_ERR_OK : XP3_ERR_NO_MEMORY);
    if (xp3FloatParse((const char *) str, &single) < 0)
        return(XP3_ERR_NO_MEMORY);
    *value = single;
    return(XP3_ERR_OK);
}

/* A decimal as a double or float, through its string, so that both are
 * rounded once and correctly */
static xp3ErrCode
decimalToBinary(const xp3Decimal *dec, int isFloat, double *value) {
    xmlChar *str = xp3DecimalToString(dec);
    xp3ErrCode err;

    if (str == NULL)
        return(XP3_ERR_NO_MEMORY);
    err = parseNumberString(str, isFloat, value);
    xmlFree(str);
    return(err);
}

/* The value of a numeric atomic as a double, or as a float stored in a
 * double if isFloat is set */
static xp3ErrCode
numberToBinary(const xp3Atomic *atomic, int isFloat, double *value) {
    switch (xp3TypeRepresentation(atomic->type)) {
        case XP3_REP_DECIMAL:
            return(decimalToBinary(atomic->u.dec, isFloat, value));
        case XP3_REP_FLOAT:
            *value = atomic->u.f;
            return(XP3_ERR_OK);
        default:
            *value = atomic->u.d;
            return(XP3_ERR_OK);
    }
}

/*
 * A finite double or float as the shortest decimal that reads back as
 * it, from its canonical string: "123.5", "-0" or "1.5E-7".
 */
static xp3ErrCode
decimalFromBinary(double value, int isFloat, xp3Decimal **res) {
    xmlChar *str = isFloat ? xp3FloatToString((float) value) :
                             xp3DoubleToString(value);
    const xmlChar *mark;
    xmlChar *plain;
    xp3ErrCode ret;
    char digits[64];
    size_t count = 0;
    size_t len;
    int exponent;
    int point;
    int negative;
    const xmlChar *cur;

    *res = NULL;
    if (str == NULL)
        return(XP3_ERR_NO_MEMORY);
    mark = xmlStrchr(str, 'E');
    if (mark == NULL) {
        ret = xp3DecimalParse(str, (size_t) xmlStrlen(str), 0, res);
        xmlFree(str);
        return(ret);
    }

    /* d.dddE±n: the digits, then where the point goes */
    negative = (str[0] == '-');
    point = 0;
    for (cur = str + negative; cur < mark; cur++) {
        if (*cur == '.') {
            point = (int) count;
            continue;
        }
        if (count < sizeof(digits))
            digits[count++] = (char) *cur;
    }
    exponent = atoi((const char *) mark + 1);
    xmlFree(str);
    point += exponent;

    /* At most 309 digits before the point and 330 after it */
    len = count + (size_t) abs(point) + 4;
    plain = xmlMalloc(len);
    if (plain == NULL)
        return(XP3_ERR_NO_MEMORY);
    {
        xmlChar *out = plain;
        int i;

        if (negative)
            *out++ = '-';
        if (point <= 0) {
            *out++ = '0';
            *out++ = '.';
            for (i = point; i < 0; i++)
                *out++ = '0';
            memcpy(out, digits, count);
            out += count;
        } else {
            for (i = 0; i < point; i++)
                *out++ = (i < (int) count) ? (xmlChar) digits[i] : '0';
            if ((int) count > point) {
                *out++ = '.';
                memcpy(out, digits + point, count - (size_t) point);
                out += count - (size_t) point;
            }
        }
        len = (size_t) (out - plain);
    }
    ret = xp3DecimalParse(plain, len, 0, res);
    xmlFree(plain);
    return(ret);
}

/* The exact integer value of a finite double without fraction */
static xp3ErrCode
integerFromBinary(double value, xp3Decimal **res) {
    xp3Decimal *result;
    xp3Decimal *power;
    xp3ErrCode ret = XP3_ERR_OK;
    double mantissa;
    int exponent;

    *res = NULL;
    if (fabs(value) < 9.0e18) {
        *res = xp3DecimalFromInt64((int64_t) value);
        return((*res != NULL) ? XP3_ERR_OK : XP3_ERR_NO_MEMORY);
    }

    /* value = mantissa * 2^exponent with a 53-bit integer mantissa,
     * and the exponent is positive this far from zero */
    mantissa = ldexp(frexp(value, &exponent), 53);
    exponent -= 53;
    result = xp3DecimalFromInt64((int64_t) mantissa);
    power = xp3DecimalFromInt64(2);
    if ((result == NULL) || (power == NULL)) {
        xp3DecimalFree(result);
        xp3DecimalFree(power);
        return(XP3_ERR_NO_MEMORY);
    }
    while (exponent > 0) {
        xp3Decimal *next;

        if (exponent & 1) {
            ret = xp3DecimalMul(result, power, &next);
            if (ret != XP3_ERR_OK)
                break;
            xp3DecimalFree(result);
            result = next;
        }
        exponent >>= 1;
        if (exponent > 0) {
            ret = xp3DecimalMul(power, power, &next);
            if (ret != XP3_ERR_OK)
                break;
            xp3DecimalFree(power);
            power = next;
        }
    }
    xp3DecimalFree(power);
    if (ret != XP3_ERR_OK) {
        xp3DecimalFree(result);
        return(ret);
    }
    *res = result;
    return(XP3_ERR_OK);
}

/*
 * Arithmetic on dates, times and durations (F&O 3.1, 8.4 and 9.7)
 */

/* What an operand of arithmetic is, as the operator mapping of XPath
 * 3.1, B.2 tells operands apart */
typedef enum {
    XP3_ARITH_OTHER = 0,
    XP3_ARITH_NUMBER,
    XP3_ARITH_YEAR_MONTH,       /* xs:yearMonthDuration */
    XP3_ARITH_DAY_TIME,         /* xs:dayTimeDuration */
    XP3_ARITH_DATE,
    XP3_ARITH_DATE_TIME,        /* also xs:dateTimeStamp */
    XP3_ARITH_TIME
} xp3ArithKind;

static xp3ArithKind
arithKind(xp3TypeId type) {
    if (xp3TypeIsNumeric(type))
        return(XP3_ARITH_NUMBER);
    if (xp3TypeDerivesFrom(type, XP3_TYPE_YEAR_MONTH_DURATION))
        return(XP3_ARITH_YEAR_MONTH);
    if (xp3TypeDerivesFrom(type, XP3_TYPE_DAY_TIME_DURATION))
        return(XP3_ARITH_DAY_TIME);
    if (type == XP3_TYPE_DATE)
        return(XP3_ARITH_DATE);
    if (xp3TypeDerivesFrom(type, XP3_TYPE_DATE_TIME))
        return(XP3_ARITH_DATE_TIME);
    if (type == XP3_TYPE_TIME)
        return(XP3_ARITH_TIME);
    return(XP3_ARITH_OTHER);
}

static int
isDuration(xp3ArithKind kind) {
    return((kind == XP3_ARITH_YEAR_MONTH) || (kind == XP3_ARITH_DAY_TIME));
}

static int
isPointInTime(xp3ArithKind kind) {
    return((kind == XP3_ARITH_DATE) || (kind == XP3_ARITH_DATE_TIME) ||
           (kind == XP3_ARITH_TIME));
}

/* Whether a kind takes part in the arithmetic of this section */
static int
isTemporal(xp3ArithKind kind) {
    return(isDuration(kind) || isPointInTime(kind));
}

/* Whether a duration of one kind moves a value of another: either
 * duration a date or a dateTime, and a dayTimeDuration a time */
static int
movesBy(xp3ArithKind value, xp3ArithKind duration) {
    if ((value == XP3_ARITH_DATE) || (value == XP3_ARITH_DATE_TIME))
        return(isDuration(duration));
    return((value == XP3_ARITH_TIME) && (duration == XP3_ARITH_DAY_TIME));
}

/* The type of a result of the kind of an operand */
static xp3TypeId
kindType(xp3ArithKind kind) {
    switch (kind) {
        case XP3_ARITH_YEAR_MONTH:
            return(XP3_TYPE_YEAR_MONTH_DURATION);
        case XP3_ARITH_DAY_TIME:
            return(XP3_TYPE_DAY_TIME_DURATION);
        case XP3_ARITH_DATE:
            return(XP3_TYPE_DATE);
        case XP3_ARITH_DATE_TIME:
            return(XP3_TYPE_DATE_TIME);
        default:
            return(XP3_TYPE_TIME);
    }
}

/*
 * The implicit timezone for two dates or times, asked for only if one
 * of them has no timezone, so that values with timezones compare,
 * subtract and fold without it (doc/DESIGN.md 6.2). Sets *timezone to
 * 0 if it isn't needed.
 */
static xp3ErrCode
pairTimezone(const xp3Atomic *left, const xp3Atomic *right, xp3Clock *clock,
             int *timezone) {
    *timezone = 0;
    if (left->u.dt.hasTimezone && right->u.dt.hasTimezone)
        return(XP3_ERR_OK);
    return(xp3ClockTimezone(clock, timezone));
}

/* Two durations of one ordered type added or subtracted (F&O 3.1,
 * 8.4.1, 8.4.2, 8.4.6 and 8.4.7) */
static xp3ErrCode
addDurations(const xp3Atomic *left, const xp3Atomic *right, int subtract,
             xp3Atomic **res) {
    xp3Duration amount = right->u.dur;
    xp3Duration sum;
    xp3ErrCode err;

    if (subtract)
        xp3DurationNegate(&right->u.dur, &amount);
    err = xp3DurationAdd(&left->u.dur, &amount, &sum);
    if (err != XP3_ERR_OK)
        return(err);
    return(newDuration(kindType(arithKind(left->type)), &sum, res));
}

/* A date, dateTime or time plus or minus a duration (F&O 3.1, 9.7.5
 * to 9.7.14), of xs:date, xs:dateTime or xs:time */
static xp3ErrCode
moveDateTime(const xp3Atomic *value, const xp3Atomic *duration,
             int subtract, xp3Atomic **res) {
    xp3TypeId type = kindType(arithKind(value->type));
    xp3Duration amount = duration->u.dur;
    xp3DateTime moved;
    xp3ErrCode err;

    if (subtract)
        xp3DurationNegate(&duration->u.dur, &amount);
    err = xp3DateTimeAddDuration(type, &value->u.dt, &amount, &moved);
    if (err != XP3_ERR_OK)
        return(err);
    return(newDateTime(type, &moved, res));
}

/* A date, dateTime or time minus another of its kind (F&O 3.1, 9.7.2
 * to 9.7.4): the elapsed time between their instants in UTC */
static xp3ErrCode
subtractDateTimes(const xp3Atomic *left, const xp3Atomic *right,
                  xp3Clock *clock, xp3Atomic **res) {
    int timezone;
    xp3Instant leftInstant;
    xp3Instant rightInstant;
    xp3Duration difference;
    xp3ErrCode err;

    err = pairTimezone(left, right, clock, &timezone);
    if (err != XP3_ERR_OK)
        return(err);
    leftInstant = xp3DateTimeInstant(left->type, &left->u.dt, timezone);
    rightInstant = xp3DateTimeInstant(right->type, &right->u.dt, timezone);
    err = xp3InstantDifference(&leftInstant, &rightInstant, &difference);
    if (err != XP3_ERR_OK)
        return(err);
    return(newDuration(XP3_TYPE_DAY_TIME_DURATION, &difference, res));
}

/* 2^63 as a double: the first whole number beyond int64_t */
#define XP3_TWO_TO_63 9223372036854775808.0

/*
 * fn:round of a finite double: to the nearest whole number, halves
 * up. value - floor(value) is exact unless -1 < value < 0, where a
 * fraction that only rounds to 0.5 goes up to 0, as it should.
 */
static double
roundHalfUp(double value) {
    double result = floor(value);

    if (value - result >= 0.5)
        result += 1;
    return(result);
}

/*
 * An xs:yearMonthDuration multiplied or divided by a number (F&O 3.1,
 * 8.4.3 and 8.4.4): the months times or by the number as an xs:double,
 * rounded with fn:round, so P1M * 1.5 is P2M and P1M * -0.5 is P0M.
 */
static xp3ErrCode
scaleMonths(const xp3Duration *duration, double factor, int divide,
            xp3Duration *result) {
    double months = (double) duration->months;
    double scaled;
    double rounded;

    memset(result, 0, sizeof(*result));
    if (isnan(factor))
        return(XP3_ERR_FOCA0005);
    if (divide) {
        if (factor == 0)
            return(XP3_ERR_FODT0002);
        scaled = months / factor;
    } else {
        if (isinf(factor))
            return(XP3_ERR_FODT0002);
        scaled = months * factor;
    }
    if (isinf(scaled))
        return(XP3_ERR_FODT0002);
    rounded = roundHalfUp(scaled);
    /* INT64_MIN is no duration either */
    if ((rounded >= XP3_TWO_TO_63) || (rounded <= -XP3_TWO_TO_63))
        return(XP3_ERR_FODT0002);
    result->months = (int64_t) rounded;
    return(XP3_ERR_OK);
}

/* The seconds of a duration as a whole number of nanoseconds */
static xp3ErrCode
durationNanos(const xp3Duration *duration, xp3Decimal **res) {
    xp3Decimal *seconds = xp3DecimalFromInt64(duration->seconds);
    xp3Decimal *perSecond = xp3DecimalFromInt64(XP3_NANOS_PER_SECOND);
    xp3Decimal *nanos = xp3DecimalFromInt64(duration->nanoseconds);
    xp3Decimal *whole = NULL;
    xp3ErrCode err = XP3_ERR_NO_MEMORY;

    *res = NULL;
    if ((seconds == NULL) || (perSecond == NULL) || (nanos == NULL))
        goto done;
    err = xp3DecimalMul(seconds, perSecond, &whole);
    if (err == XP3_ERR_OK)
        err = xp3DecimalAdd(whole, nanos, res);

done:
    xp3DecimalFree(seconds);
    xp3DecimalFree(perSecond);
    xp3DecimalFree(nanos);
    xp3DecimalFree(whole);
    return(err);
}

/* A whole number of nanoseconds as the seconds of a duration, or
 * FODT0002 if the seconds don't fit in 64 bits */
static xp3ErrCode
nanosToDuration(const xp3Decimal *nanos, xp3Duration *result) {
    xp3Decimal *perSecond = xp3DecimalFromInt64(XP3_NANOS_PER_SECOND);
    xp3Decimal *seconds = NULL;
    xp3Decimal *rest = NULL;
    int64_t wholeSeconds;
    int64_t fraction;
    xp3ErrCode err = XP3_ERR_NO_MEMORY;

    memset(result, 0, sizeof(*result));
    if (perSecond == NULL)
        goto done;
    /* Both truncate toward zero, so the parts have the same sign */
    err = xp3DecimalIdiv(nanos, perSecond, &seconds);
    if (err == XP3_ERR_OK)
        err = xp3DecimalMod(nanos, perSecond, &rest);
    if (err != XP3_ERR_OK)
        goto done;
    if ((xp3DecimalToInt64(seconds, &wholeSeconds) < 0) ||
        (wholeSeconds == INT64_MIN) ||
        (xp3DecimalToInt64(rest, &fraction) < 0)) {
        err = XP3_ERR_FODT0002;
        goto done;
    }
    result->seconds = wholeSeconds;
    result->nanoseconds = (int32_t) fraction;

done:
    xp3DecimalFree(perSecond);
    xp3DecimalFree(seconds);
    xp3DecimalFree(rest);
    return(err);
}

/*
 * Bits of the largest nanoseconds of a duration: (2^63 - 1) * 10^9 +
 * 999,999,999 is below 2^93, while every duration but zero has at
 * least one nanosecond.
 */
#define XP3_NANOS_BITS 93

/*
 * An xs:dayTimeDuration multiplied or divided by a number (F&O 3.1,
 * 8.4.8 and 8.4.9), as Saxon does it (doc/DESIGN.md 4.2, D44): the
 * number becomes the shortest decimal that reads back as the double,
 * as when it is cast to xs:decimal, so 0.7 is 0.7 and not the double's
 * exact value just below it. The nanoseconds of the duration are then
 * multiplied or divided by that decimal exactly, and the result is
 * truncated toward zero: PT1S * 0.7 is PT0.7S, PT2S div 3 is
 * PT0.666666666S.
 *
 * Factors below 2^-93, and from 2^93 up, are settled without decimal
 * arithmetic. A duration that isn't zero has at least one nanosecond
 * and at most about 9.22 * 10^27, below 2^93, which is about
 * 9.90 * 10^27. The shortest decimal of a factor below 2^-93 is below
 * 2^-93 too, and that of a factor of 2^93 or more is about 9.90 * 10^27
 * or more, so such a product is below one nanosecond or beyond the
 * largest duration, and a quotient the other way around.
 */
static xp3ErrCode
scaleSeconds(const xp3Duration *duration, double factor, int divide,
             xp3Duration *result) {
    double magnitude = fabs(factor);
    int tiny = (magnitude < ldexp(1.0, -XP3_NANOS_BITS));
    int huge = (magnitude >= ldexp(1.0, XP3_NANOS_BITS));
    xp3Decimal *nanos = NULL;
    xp3Decimal *number = NULL;
    xp3Decimal *product = NULL;
    xp3Decimal *scaled = NULL;
    xp3ErrCode err;

    memset(result, 0, sizeof(*result));
    if (isnan(factor))
        return(XP3_ERR_FOCA0005);
    if (divide && (factor == 0))
        return(XP3_ERR_FODT0002);
    if (!divide && isinf(factor))
        return(XP3_ERR_FODT0002);
    if ((duration->seconds == 0) && (duration->nanoseconds == 0))
        return(XP3_ERR_OK);
    if (divide) {
        if (huge)
            return(XP3_ERR_OK);
        if (tiny)
            return(XP3_ERR_FODT0002);
    } else {
        if (tiny)
            return(XP3_ERR_OK);
        if (huge)
            return(XP3_ERR_FODT0002);
    }

    err = durationNanos(duration, &nanos);
    if (err == XP3_ERR_OK)
        err = decimalFromBinary(factor, 0, &number);
    if (err != XP3_ERR_OK)
        goto done;
    if (divide) {
        err = xp3DecimalIdiv(nanos, number, &scaled);
    } else {
        err = xp3DecimalMul(nanos, number, &product);
        if (err == XP3_ERR_OK)
            err = xp3DecimalRound(product, 0, XP3_ROUND_DOWN, &scaled);
    }
    if (err == XP3_ERR_OK)
        err = nanosToDuration(scaled, result);

done:
    xp3DecimalFree(nanos);
    xp3DecimalFree(number);
    xp3DecimalFree(product);
    xp3DecimalFree(scaled);
    return(err);
}

/* A duration multiplied or divided by a number, of the duration's
 * type */
static xp3ErrCode
scaleDuration(const xp3Atomic *duration, const xp3Atomic *number,
              int divide, xp3Atomic **res) {
    xp3ArithKind kind = arithKind(duration->type);
    xp3Duration scaled;
    double factor;
    xp3ErrCode err;

    /* The number is an xs:double in the signatures of F&O 3.1, 8.4 */
    err = numberToBinary(number, 0, &factor);
    if (err != XP3_ERR_OK)
        return(err);
    if (kind == XP3_ARITH_YEAR_MONTH)
        err = scaleMonths(&duration->u.dur, factor, divide, &scaled);
    else
        err = scaleSeconds(&duration->u.dur, factor, divide, &scaled);
    if (err != XP3_ERR_OK)
        return(err);
    return(newDuration(kindType(kind), &scaled, res));
}

/* Digits after the point of a duration divided by a duration */
#define XP3_DURATION_RATIO_DIGITS 20

/*
 * Two durations of one ordered type divided (F&O 3.1, 8.4.5 and
 * 8.4.10): the ratio of their months or of their seconds, as an
 * xs:decimal with 20 digits after the point, rounded half to even, and
 * shorter only when exact, as Saxon gives it (doc/DESIGN.md 4.2, not
 * the D3 rule of the div operator on numbers).
 */
static xp3ErrCode
divideDurations(const xp3Atomic *left, const xp3Atomic *right,
                xp3Atomic **res) {
    xp3Decimal *dividend = NULL;
    xp3Decimal *divisor = NULL;
    xp3Decimal *ratio = NULL;
    xp3ErrCode err = XP3_ERR_NO_MEMORY;

    if (arithKind(left->type) == XP3_ARITH_YEAR_MONTH) {
        dividend = xp3DecimalFromInt64(left->u.dur.months);
        divisor = xp3DecimalFromInt64(right->u.dur.months);
        if ((dividend == NULL) || (divisor == NULL))
            goto done;
    } else {
        err = durationNanos(&left->u.dur, &dividend);
        if (err == XP3_ERR_OK)
            err = durationNanos(&right->u.dur, &divisor);
        if (err != XP3_ERR_OK)
            goto done;
    }
    err = xp3DecimalDivRound(dividend, divisor, XP3_DURATION_RATIO_DIGITS,
                             XP3_ROUND_HALF_EVEN, &ratio);
    if (err == XP3_ERR_OK)
        err = newDecimal(XP3_TYPE_DECIMAL, ratio, res);

done:
    xp3DecimalFree(dividend);
    xp3DecimalFree(divisor);
    return(err);
}

/*
 * The operators of XPath 3.1, B.2 on dates, times and durations: two
 * durations of one ordered type added, subtracted and divided, a
 * duration multiplied and divided by a number, a date, dateTime or
 * time moved by a duration, and two of one kind subtracted. idiv and
 * mod, plain xs:duration and every other pair are type errors.
 */
static xp3ErrCode
temporalArithmetic(xp3AstOp op, const xp3Atomic *left,
                   const xp3Atomic *right, xp3Clock *clock,
                   xp3Atomic **res) {
    xp3ArithKind leftKind = arithKind(left->type);
    xp3ArithKind rightKind = arithKind(right->type);
    int sameDuration = isDuration(leftKind) && (leftKind == rightKind);

    switch (op) {
        case XP3_OP_ADD:
            if (sameDuration)
                return(addDurations(left, right, 0, res));
            if (movesBy(leftKind, rightKind))
                return(moveDateTime(left, right, 0, res));
            if (movesBy(rightKind, leftKind))
                return(moveDateTime(right, left, 0, res));
            break;
        case XP3_OP_SUB:
            if (sameDuration)
                return(addDurations(left, right, 1, res));
            if (movesBy(leftKind, rightKind))
                return(moveDateTime(left, right, 1, res));
            if ((leftKind == rightKind) && isPointInTime(leftKind))
                return(subtractDateTimes(left, right, clock, res));
            break;
        case XP3_OP_MUL:
            if (isDuration(leftKind) && (rightKind == XP3_ARITH_NUMBER))
                return(scaleDuration(left, right, 0, res));
            if ((leftKind == XP3_ARITH_NUMBER) && isDuration(rightKind))
                return(scaleDuration(right, left, 0, res));
            break;
        case XP3_OP_DIV:
            if (isDuration(leftKind) && (rightKind == XP3_ARITH_NUMBER))
                return(scaleDuration(left, right, 1, res));
            if (sameDuration)
                return(divideDurations(left, right, res));
            break;
        default:
            break;
    }
    return(XP3_ERR_XPTY0004);
}

/*
 * Arithmetic
 */

/* Arithmetic on doubles, or on floats if isFloat is set (F&O 3.1,
 * 4.2) */
static xp3ErrCode
binaryArithmetic(xp3AstOp op, double left, double right, int isFloat,
                 xp3Atomic **res) {
    double result;

    switch (op) {
        case XP3_OP_ADD:
            result = left + right;
            break;
        case XP3_OP_SUB:
            result = left - right;
            break;
        case XP3_OP_MUL:
            result = left * right;
            break;
        case XP3_OP_DIV:
            result = left / right;
            break;
        case XP3_OP_MOD:
            result = fmod(left, right);
            break;
        default: {
            /* idiv (F&O 3.1, 4.2.5) */
            xp3Decimal *dec;
            xp3ErrCode err;
            double quotient;

            if (right == 0)
                return(XP3_ERR_FOAR0001);
            if (isnan(left) || isnan(right) || isinf(left))
                return(XP3_ERR_FOAR0002);
            quotient = left / right;
            if (isFloat)
                quotient = (float) quotient;
            /* idiv is (left div right) cast as xs:integer, and casting
             * infinity fails (F&O 3.1, 19.1.2.4) */
            if (isinf(quotient))
                return(XP3_ERR_FOCA0002);
            err = integerFromBinary(trunc(quotient), &dec);
            if (err != XP3_ERR_OK)
                return(err);
            return(newDecimal(XP3_TYPE_INTEGER, dec, res));
        }
    }
    if (isFloat)
        return(newFloat((float) result, res));
    return(newDouble(result, res));
}

/**
 * Apply an arithmetic operator to two values (XPath 3.1, B.2): +, -,
 * *, div, idiv or mod.
 *
 * Numbers (F&O 3.1, 4.2) are promoted to a common type: xs:integer,
 * xs:decimal, xs:float or xs:double. Integers give integers, except
 * that div gives a decimal.
 *
 * Dates, times and durations (F&O 3.1, 8.4 and 9.7): durations of one
 * ordered type are added, subtracted and divided, a duration is
 * multiplied or divided by a number, a date, dateTime or time is moved
 * by a duration, and two of one kind are subtracted, using the
 * implicit timezone for one without a timezone (doc/DESIGN.md 4.2).
 *
 * @param op     the operator
 * @param left   the first operand
 * @param right  the second operand
 * @param clock  gives the implicit timezone, for subtracting dates and
 *               times without one; NULL for none
 * @param res    where to store the result
 * @returns XP3_ERR_OK, XP3_ERR_XPTY0004 if the operator isn't defined
 *          on the operands, XP3_ERR_FOAR0001 for a division by zero or
 *          by a zero duration, XP3_ERR_FOAR0002 for an overflow or an
 *          invalid idiv, XP3_ERR_FOCA0002 for an idiv whose quotient is
 *          infinite, XP3_ERR_FODT0001 for a date beyond the year
 *          limits, XP3_ERR_FODT0002 for a duration beyond its limits,
 *          XP3_ERR_FOCA0005 for a duration multiplied or divided by
 *          NaN, XP3_ERR_NEEDS_CONTEXT if the implicit timezone is
 *          needed and the clock has none, or XP3_ERR_NO_MEMORY
 */
xp3ErrCode
xp3Arithmetic(xp3AstOp op, const xp3Atomic *left, const xp3Atomic *right,
              xp3Clock *clock, xp3Atomic **res) {
    xp3NumKind leftKind = numKind(left->type);
    xp3NumKind rightKind = numKind(right->type);
    xp3NumKind kind = (leftKind > rightKind) ? leftKind : rightKind;
    xp3TypeId type = XP3_TYPE_DECIMAL;
    xp3Decimal *dec = NULL;
    xp3ErrCode err;

    *res = NULL;
    if (isTemporal(arithKind(left->type)) ||
        isTemporal(arithKind(right->type)))
        return(temporalArithmetic(op, left, right, clock, res));
    if ((leftKind == XP3_NUM_NONE) || (rightKind == XP3_NUM_NONE))
        return(XP3_ERR_XPTY0004);

    if (kind >= XP3_NUM_FLOAT) {
        int isFloat = (kind == XP3_NUM_FLOAT);
        double a;
        double b;

        err = numberToBinary(left, isFloat, &a);
        if (err == XP3_ERR_OK)
            err = numberToBinary(right, isFloat, &b);
        if (err != XP3_ERR_OK)
            return(err);
        return(binaryArithmetic(op, a, b, isFloat, res));
    }

    switch (op) {
        case XP3_OP_ADD:
            err = xp3DecimalAdd(left->u.dec, right->u.dec, &dec);
            break;
        case XP3_OP_SUB:
            err = xp3DecimalSub(left->u.dec, right->u.dec, &dec);
            break;
        case XP3_OP_MUL:
            err = xp3DecimalMul(left->u.dec, right->u.dec, &dec);
            break;
        case XP3_OP_DIV:
            err = xp3DecimalDivTyped(left->u.dec,
                                     leftKind == XP3_NUM_INTEGER,
                                     right->u.dec,
                                     rightKind == XP3_NUM_INTEGER, &dec);
            break;
        case XP3_OP_IDIV:
            err = xp3DecimalIdiv(left->u.dec, right->u.dec, &dec);
            break;
        default:
            err = xp3DecimalMod(left->u.dec, right->u.dec, &dec);
            break;
    }
    if (err == XP3_ERR_LIMIT)
        return(XP3_ERR_FOAR0002);
    if (err != XP3_ERR_OK)
        return(err);
    if ((op == XP3_OP_IDIV) ||
        ((kind == XP3_NUM_INTEGER) && (op != XP3_OP_DIV)))
        type = XP3_TYPE_INTEGER;
    return(newDecimal(type, dec, res));
}

/**
 * Negate a number, as unary minus does (F&O 3.1, 4.2.8). Integers of
 * derived types become xs:integer.
 *
 * @param value  the number
 * @param res    where to store the result
 * @returns XP3_ERR_OK, XP3_ERR_XPTY0004 if the value is not numeric, or
 *          XP3_ERR_NO_MEMORY
 */
xp3ErrCode
xp3NegateNumber(const xp3Atomic *value, xp3Atomic **res) {
    xp3Decimal *dec;
    xp3ErrCode err;

    *res = NULL;
    switch (numKind(value->type)) {
        case XP3_NUM_INTEGER:
        case XP3_NUM_DECIMAL:
            err = xp3DecimalNegate(value->u.dec, &dec);
            if (err != XP3_ERR_OK)
                return(err);
            return(newDecimal(xp3TypeIsInteger(value->type) ?
                              XP3_TYPE_INTEGER : XP3_TYPE_DECIMAL, dec, res));
        case XP3_NUM_FLOAT:
            return(newFloat(-value->u.f, res));
        case XP3_NUM_DOUBLE:
            return(newDouble(-value->u.d, res));
        default:
            return(XP3_ERR_XPTY0004);
    }
}

/*
 * Comparison
 */

/* Groups of types whose values can be compared with each other */
typedef enum {
    XP3_CMP_OTHER = 0,
    XP3_CMP_NUMERIC,
    XP3_CMP_STRING,             /* also xs:anyURI and xs:untypedAtomic */
    XP3_CMP_BOOLEAN,
    XP3_CMP_HEX_BINARY,
    XP3_CMP_BASE64_BINARY,
    XP3_CMP_QNAME,
    XP3_CMP_DURATION,           /* all three duration types */
    XP3_CMP_DATE_TIME           /* the date and time types, each
                                 * primitive type only with itself */
} xp3CompareClass;

static xp3CompareClass
compareClass(xp3TypeId type) {
    if (xp3TypeIsNumeric(type))
        return(XP3_CMP_NUMERIC);
    if (xp3TypeDerivesFrom(type, XP3_TYPE_STRING) ||
        (type == XP3_TYPE_ANY_URI) || (type == XP3_TYPE_UNTYPED_ATOMIC))
        return(XP3_CMP_STRING);
    if (xp3TypeRepresentation(type) == XP3_REP_DURATION)
        return(XP3_CMP_DURATION);
    if (xp3TypeRepresentation(type) == XP3_REP_DATE_TIME)
        return(XP3_CMP_DATE_TIME);
    switch (type) {
        case XP3_TYPE_BOOLEAN:
            return(XP3_CMP_BOOLEAN);
        case XP3_TYPE_HEX_BINARY:
            return(XP3_CMP_HEX_BINARY);
        case XP3_TYPE_BASE64_BINARY:
            return(XP3_CMP_BASE64_BINARY);
        case XP3_TYPE_QNAME:
            return(XP3_CMP_QNAME);
        default:
            return(XP3_CMP_OTHER);
    }
}

/* A comparison result for NaN, which is not ordered */
#define XP3_UNORDERED 2

/* Compare two numbers after promotion: -1, 0, 1 or XP3_UNORDERED */
static xp3ErrCode
compareNumbers(const xp3Atomic *left, const xp3Atomic *right, int *cmp) {
    xp3NumKind leftKind = numKind(left->type);
    xp3NumKind rightKind = numKind(right->type);
    xp3NumKind kind = (leftKind > rightKind) ? leftKind : rightKind;
    xp3ErrCode err;
    double a;
    double b;

    if (kind <= XP3_NUM_DECIMAL) {
        *cmp = xp3DecimalCompare(left->u.dec, right->u.dec);
        return(XP3_ERR_OK);
    }
    err = numberToBinary(left, kind == XP3_NUM_FLOAT, &a);
    if (err == XP3_ERR_OK)
        err = numberToBinary(right, kind == XP3_NUM_FLOAT, &b);
    if (err != XP3_ERR_OK)
        return(err);
    if (isnan(a) || isnan(b))
        *cmp = XP3_UNORDERED;
    else
        *cmp = (a < b) ? -1 : (a > b) ? 1 : 0;
    return(XP3_ERR_OK);
}

static int
compareBytes(const xp3Atomic *left, const xp3Atomic *right) {
    size_t len = (left->u.bin.len < right->u.bin.len) ? left->u.bin.len :
                                                         right->u.bin.len;
    int cmp = (len > 0) ? memcmp(left->u.bin.data, right->u.bin.data, len) :
                          0;

    if (cmp != 0)
        return((cmp < 0) ? -1 : 1);
    if (left->u.bin.len == right->u.bin.len)
        return(0);
    return((left->u.bin.len < right->u.bin.len) ? -1 : 1);
}

/* Whether two durations are ordered: both xs:yearMonthDuration or
 * both xs:dayTimeDuration (F&O 3.1, 8.1); xs:duration has only
 * equality */
static int
durationsOrdered(xp3TypeId left, xp3TypeId right) {
    if (xp3TypeDerivesFrom(left, XP3_TYPE_YEAR_MONTH_DURATION) &&
        xp3TypeDerivesFrom(right, XP3_TYPE_YEAR_MONTH_DURATION))
        return(1);
    return(xp3TypeDerivesFrom(left, XP3_TYPE_DAY_TIME_DURATION) &&
           xp3TypeDerivesFrom(right, XP3_TYPE_DAY_TIME_DURATION));
}

/* Whether values of a primitive date or time type are ordered: the g*
 * types have only equality (F&O 3.1, 9.1) */
static int
dateTimeOrdered(xp3TypeId primitive) {
    return((primitive == XP3_TYPE_DATE_TIME) ||
           (primitive == XP3_TYPE_DATE) || (primitive == XP3_TYPE_TIME));
}

/*
 * Compare two values of one primitive date or time type by their
 * starting instants in UTC (F&O 3.1, 9.4), with the implicit timezone
 * only if one of them has no timezone (pairTimezone()).
 */
static xp3ErrCode
compareDateTimes(const xp3Atomic *left, const xp3Atomic *right,
                 xp3Clock *clock, int *cmp) {
    int timezone;
    xp3Instant leftInstant;
    xp3Instant rightInstant;
    xp3ErrCode err = pairTimezone(left, right, clock, &timezone);

    if (err != XP3_ERR_OK)
        return(err);
    leftInstant = xp3DateTimeInstant(left->type, &left->u.dt, timezone);
    rightInstant = xp3DateTimeInstant(right->type, &right->u.dt, timezone);
    *cmp = xp3InstantCompare(&leftInstant, &rightInstant);
    return(XP3_ERR_OK);
}

/* The value comparison operator for a general comparison operator */
static xp3AstOp
valueOperator(xp3AstOp op) {
    switch (op) {
        case XP3_OP_GEN_EQ:
            return(XP3_OP_VAL_EQ);
        case XP3_OP_GEN_NE:
            return(XP3_OP_VAL_NE);
        case XP3_OP_GEN_LT:
            return(XP3_OP_VAL_LT);
        case XP3_OP_GEN_LE:
            return(XP3_OP_VAL_LE);
        case XP3_OP_GEN_GT:
            return(XP3_OP_VAL_GT);
        case XP3_OP_GEN_GE:
            return(XP3_OP_VAL_GE);
        default:
            return(op);
    }
}

/**
 * Compare two atomic values with a value comparison operator (XPath
 * 3.1, 3.7.1), or the value comparison a general comparison operator
 * stands for. Untyped values compare as strings; the caller converts
 * them first where a general comparison needs another type. Strings
 * compare by code point, with each byte that isn't UTF-8 as U+FFFD.
 * Numbers are promoted to a common type, and NaN is unequal to
 * everything. Durations of any type are equal if their months and
 * seconds are, and only two xs:yearMonthDuration or two
 * xs:dayTimeDuration values are ordered. Dates and times compare with
 * values of the same primitive type, by their instants in UTC, and
 * the g* types only for equality.
 *
 * @param op      the operator: eq, ne, lt, le, gt, ge, or =, != ...
 * @param left    the first value
 * @param right   the second value
 * @param level   the language level: ordering binary values is new in
 *                XPath 3.1
 * @param clock   gives the implicit timezone, for dates and times
 *                without one; NULL for none
 * @param result  set to 1 or 0
 * @returns XP3_ERR_OK, XP3_ERR_XPTY0004 if the values can't be
 *          compared with this operator, XP3_ERR_NEEDS_CONTEXT if they
 *          need the implicit timezone and the clock has none, or
 *          XP3_ERR_NO_MEMORY
 */
xp3ErrCode
xp3CompareAtomics(xp3AstOp op, const xp3Atomic *left, const xp3Atomic *right,
                  xp3LanguageLevel level, xp3Clock *clock, int *result) {
    xp3CompareClass leftClass = compareClass(left->type);
    xp3CompareClass rightClass = compareClass(right->type);
    int ordering;
    int cmp = 0;
    xp3ErrCode err;

    *result = 0;
    op = valueOperator(op);
    ordering = (op != XP3_OP_VAL_EQ) && (op != XP3_OP_VAL_NE);
    if ((leftClass != rightClass) || (leftClass == XP3_CMP_OTHER))
        return(XP3_ERR_XPTY0004);

    switch (leftClass) {
        case XP3_CMP_NUMERIC:
            err = compareNumbers(left, right, &cmp);
            if (err != XP3_ERR_OK)
                return(err);
            break;
        case XP3_CMP_STRING:
            /* By code point, a byte that isn't UTF-8 as U+FFFD */
            cmp = xp3CompareChars(left->u.str, right->u.str, 0);
            break;
        case XP3_CMP_BOOLEAN:
            cmp = (left->u.boolean != 0) - (right->u.boolean != 0);
            break;
        case XP3_CMP_HEX_BINARY:
        case XP3_CMP_BASE64_BINARY:
            if (ordering && (level < XP3_XPATH_31))
                return(XP3_ERR_XPTY0004);
            cmp = compareBytes(left, right);
            break;
        case XP3_CMP_DURATION:
            if (ordering && !durationsOrdered(left->type, right->type))
                return(XP3_ERR_XPTY0004);
            cmp = xp3DurationCompare(&left->u.dur, &right->u.dur);
            break;
        case XP3_CMP_DATE_TIME: {
            xp3TypeId primitive = xp3TypePrimitive(left->type);

            if (primitive != xp3TypePrimitive(right->type))
                return(XP3_ERR_XPTY0004);
            if (ordering && !dateTimeOrdered(primitive))
                return(XP3_ERR_XPTY0004);
            err = compareDateTimes(left, right, clock, &cmp);
            if (err != XP3_ERR_OK)
                return(err);
            break;
        }
        default:
            /* QNames are equal or not, without order */
            if (ordering)
                return(XP3_ERR_XPTY0004);
            cmp = (xmlStrEqual(left->u.qname.uri, right->u.qname.uri) &&
                   xmlStrEqual(left->u.qname.local, right->u.qname.local)) ?
                  0 : 1;
            break;
    }

    if (cmp == XP3_UNORDERED) {
        *result = (op == XP3_OP_VAL_NE);
        return(XP3_ERR_OK);
    }
    switch (op) {
        case XP3_OP_VAL_EQ:
            *result = (cmp == 0);
            break;
        case XP3_OP_VAL_NE:
            *result = (cmp != 0);
            break;
        case XP3_OP_VAL_LT:
            *result = (cmp < 0);
            break;
        case XP3_OP_VAL_LE:
            *result = (cmp <= 0);
            break;
        case XP3_OP_VAL_GT:
            *result = (cmp > 0);
            break;
        default:
            *result = (cmp >= 0);
            break;
    }
    return(XP3_ERR_OK);
}

/*
 * Casting from strings (F&O 3.1, 19.2)
 */

static int
isSpace(int c) {
    return((c == ' ') || (c == '\t') || (c == '\n') || (c == '\r'));
}

/* A copy with whitespace collapsed: trimmed, and runs of it turned into
 * one space (XML Schema whiteSpace="collapse") */
static xmlChar *
collapseSpace(const xmlChar *str) {
    xmlChar *out = xmlMalloc((size_t) xmlStrlen(str) + 1);
    xmlChar *cur = out;
    int space = 0;

    if (out == NULL)
        return(NULL);
    while (isSpace(*str))
        str++;
    for (; *str != 0; str++) {
        if (isSpace(*str)) {
            space = 1;
            continue;
        }
        if (space)
            *cur++ = ' ';
        space = 0;
        *cur++ = *str;
    }
    *cur = 0;
    return(out);
}

/* A copy with tabs and line ends turned into spaces
 * (whiteSpace="replace") */
static xmlChar *
replaceSpace(const xmlChar *str) {
    xmlChar *out = xmlStrdup(str);
    xmlChar *cur;

    for (cur = out; (cur != NULL) && (*cur != 0); cur++) {
        if (isSpace(*cur))
            *cur = ' ';
    }
    return(out);
}

/* Whether a string is an XML Name, or an NCName if noColon is set. The
 * xp3chars.c tests are those of NCNames, so the colon is added here. */
static int
isName(const xmlChar *str, int noColon) {
    int first = 1;

    if (*str == 0)
        return(0);
    while (*str != 0) {
        int len = 4;
        int c = xp3GetUTF8Char(str, &len);

        if (c < 0)
            return(0);
        if (c == ':') {
            if (noColon)
                return(0);
        } else if (first ? !xp3IsNameStartChar(c) : !xp3IsNameChar(c)) {
            return(0);
        }
        first = 0;
        str += len;
    }
    return(1);
}

/* Whether a string is an NMTOKEN: one or more name characters */
static int
isNmtoken(const xmlChar *str) {
    if (*str == 0)
        return(0);
    while (*str != 0) {
        int len = 4;
        int c = xp3GetUTF8Char(str, &len);

        if ((c < 0) || ((c != ':') && !xp3IsNameChar(c)))
            return(0);
        str += len;
    }
    return(1);
}

static int
isAsciiAlpha(int c) {
    return(((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')));
}

/* Whether a string is an xs:language:
 * [a-zA-Z]{1,8}(-[a-zA-Z0-9]{1,8})* */
static int
isLanguage(const xmlChar *str) {
    int first = 1;

    for (;;) {
        int count = 0;

        while (isAsciiAlpha(*str) ||
               (!first && (*str >= '0') && (*str <= '9'))) {
            str++;
            count++;
        }
        if ((count < 1) || (count > 8))
            return(0);
        if (*str == 0)
            return(1);
        if (*str != '-')
            return(0);
        str++;
        first = 0;
    }
}

/* Parse xs:double or xs:float (XML Schema 1.1): digits with an optional
 * point and exponent, INF, +INF, -INF or NaN */
static xp3ErrCode
parseBinaryFloat(const xmlChar *str, int isFloat, double *value) {
    const xmlChar *cur = str;
    int digits = 0;

    if (xmlStrEqual(str, BAD_CAST "NaN")) {
        *value = xp3DoubleNaN();
        return(XP3_ERR_OK);
    }
    if (xmlStrEqual(str, BAD_CAST "INF") ||
        xmlStrEqual(str, BAD_CAST "+INF")) {
        *value = HUGE_VAL;
        return(XP3_ERR_OK);
    }
    if (xmlStrEqual(str, BAD_CAST "-INF")) {
        *value = -HUGE_VAL;
        return(XP3_ERR_OK);
    }
    if ((*cur == '+') || (*cur == '-'))
        cur++;
    for (; (*cur >= '0') && (*cur <= '9'); cur++)
        digits++;
    if (*cur == '.') {
        for (cur++; (*cur >= '0') && (*cur <= '9'); cur++)
            digits++;
    }
    if (digits == 0)
        return(XP3_ERR_FORG0001);
    if ((*cur == 'e') || (*cur == 'E')) {
        int exponentDigits = 0;

        cur++;
        if ((*cur == '+') || (*cur == '-'))
            cur++;
        for (; (*cur >= '0') && (*cur <= '9'); cur++)
            exponentDigits++;
        if (exponentDigits == 0)
            return(XP3_ERR_FORG0001);
    }
    if (*cur != 0)
        return(XP3_ERR_FORG0001);
    return(parseNumberString(str, isFloat, value));
}

static int
hexValue(int c) {
    if ((c >= '0') && (c <= '9'))
        return(c - '0');
    if ((c >= 'a') && (c <= 'f'))
        return(c - 'a' + 10);
    if ((c >= 'A') && (c <= 'F'))
        return(c - 'A' + 10);
    return(-1);
}

static int
base64Value(int c) {
    if ((c >= 'A') && (c <= 'Z'))
        return(c - 'A');
    if ((c >= 'a') && (c <= 'z'))
        return(c - 'a' + 26);
    if ((c >= '0') && (c <= '9'))
        return(c - '0' + 52);
    if (c == '+')
        return(62);
    if (c == '/')
        return(63);
    return(-1);
}

/* Parse xs:hexBinary: an even number of hex digits */
static xp3ErrCode
parseHex(const xmlChar *str, xp3Atomic **res) {
    size_t len = (size_t) xmlStrlen(str);
    unsigned char *data;
    size_t i;

    if (len % 2 != 0)
        return(XP3_ERR_FORG0001);
    data = xmlMalloc(len / 2 + 1);
    if (data == NULL)
        return(XP3_ERR_NO_MEMORY);
    for (i = 0; i < len; i += 2) {
        int high = hexValue(str[i]);
        int low = hexValue(str[i + 1]);

        if ((high < 0) || (low < 0)) {
            xmlFree(data);
            return(XP3_ERR_FORG0001);
        }
        data[i / 2] = (unsigned char) ((high << 4) | low);
    }
    *res = xp3AtomicNewBinary(XP3_TYPE_HEX_BINARY, data, len / 2);
    xmlFree(data);
    return((*res != NULL) ? XP3_ERR_OK : XP3_ERR_NO_MEMORY);
}

/*
 * Parse xs:base64Binary (XML Schema 1.1, 3.3.16): groups of four
 * characters, spaces allowed between them, and at the end "xx==" or
 * "xxx=" whose unused bits are zero.
 */
static xp3ErrCode
parseBase64(const xmlChar *str, xp3Atomic **res) {
    size_t len = (size_t) xmlStrlen(str);
    unsigned char *data;
    int quad[4];
    size_t chars = 0;
    size_t size = 0;
    int padding = 0;
    xp3ErrCode ret = XP3_ERR_FORG0001;

    data = xmlMalloc(len / 4 * 3 + 3);
    if (data == NULL)
        return(XP3_ERR_NO_MEMORY);
    for (; *str != 0; str++) {
        int value;

        if (*str == ' ')
            continue;
        if (*str == '=') {
            /* Padding only in the last two places of the last group */
            if ((chars % 4 < 2) || (padding && (chars % 4 != 3)))
                goto done;
            padding++;
            value = 0;
        } else {
            value = base64Value(*str);
            if ((value < 0) || padding)
                goto done;
        }
        quad[chars % 4] = value;
        chars++;
        if (chars % 4 == 0) {
            data[size++] = (unsigned char) ((quad[0] << 2) | (quad[1] >> 4));
            data[size++] = (unsigned char) (((quad[1] & 15) << 4) |
                                            (quad[2] >> 2));
            data[size++] = (unsigned char) (((quad[2] & 3) << 6) | quad[3]);
        }
    }
    if (chars % 4 != 0)
        goto done;
    if (padding == 2) {
        if ((quad[1] & 15) != 0)
            goto done;
        size -= 2;
    } else if (padding == 1) {
        if ((quad[2] & 3) != 0)
            goto done;
        size -= 1;
    }
    *res = xp3AtomicNewBinary(XP3_TYPE_BASE64_BINARY, data, size);
    ret = (*res != NULL) ? XP3_ERR_OK : XP3_ERR_NO_MEMORY;

done:
    xmlFree(data);
    return(ret);
}

/*
 * Whether an integer is in the range of an integer type: the facets of
 * the built-in types derived from xs:integer.
 */
static xp3ErrCode
checkIntegerRange(const xp3Decimal *dec, xp3TypeId type) {
    int sign = xp3DecimalSign(dec);
    int64_t min = INT64_MIN;
    int64_t max = INT64_MAX;
    int64_t value;
    int fits;

    switch (type) {
        case XP3_TYPE_INTEGER:
            return(XP3_ERR_OK);
        case XP3_TYPE_NON_POSITIVE_INTEGER:
            return((sign <= 0) ? XP3_ERR_OK : XP3_ERR_FORG0001);
        case XP3_TYPE_NEGATIVE_INTEGER:
            return((sign < 0) ? XP3_ERR_OK : XP3_ERR_FORG0001);
        case XP3_TYPE_NON_NEGATIVE_INTEGER:
            return((sign >= 0) ? XP3_ERR_OK : XP3_ERR_FORG0001);
        case XP3_TYPE_POSITIVE_INTEGER:
            return((sign > 0) ? XP3_ERR_OK : XP3_ERR_FORG0001);
        case XP3_TYPE_UNSIGNED_LONG: {
            xp3Decimal *limit;
            xp3ErrCode err;
            int cmp;

            if (sign < 0)
                return(XP3_ERR_FORG0001);
            err = xp3DecimalParse(BAD_CAST "18446744073709551615", 20, 1,
                                  &limit);
            if (err != XP3_ERR_OK)
                return(err);
            cmp = xp3DecimalCompare(dec, limit);
            xp3DecimalFree(limit);
            return((cmp <= 0) ? XP3_ERR_OK : XP3_ERR_FORG0001);
        }
        case XP3_TYPE_INT:
            min = INT32_MIN;
            max = INT32_MAX;
            break;
        case XP3_TYPE_SHORT:
            min = INT16_MIN;
            max = INT16_MAX;
            break;
        case XP3_TYPE_BYTE:
            min = INT8_MIN;
            max = INT8_MAX;
            break;
        case XP3_TYPE_UNSIGNED_INT:
            min = 0;
            max = UINT32_MAX;
            break;
        case XP3_TYPE_UNSIGNED_SHORT:
            min = 0;
            max = UINT16_MAX;
            break;
        case XP3_TYPE_UNSIGNED_BYTE:
            min = 0;
            max = UINT8_MAX;
            break;
        default:
            /* xs:long */
            break;
    }
    fits = (xp3DecimalToInt64(dec, &value) == 0);
    return((fits && (value >= min) && (value <= max)) ? XP3_ERR_OK :
                                                       XP3_ERR_FORG0001);
}

/* An integer value as an integer type, checking the range; takes over
 * dec */
static xp3ErrCode
newInteger(xp3TypeId type, xp3Decimal *dec, xp3Atomic **res) {
    xp3ErrCode err = checkIntegerRange(dec, type);

    if (err != XP3_ERR_OK) {
        xp3DecimalFree(dec);
        return(err);
    }
    return(newDecimal(type, dec, res));
}

/* A string-like type after whitespace processing, checked against the
 * lexical space of the type */
static xp3ErrCode
castToStringType(const xmlChar *str, xp3TypeId type, xp3Atomic **res) {
    int valid = 1;

    switch (type) {
        case XP3_TYPE_LANGUAGE:
            valid = isLanguage(str);
            break;
        case XP3_TYPE_NMTOKEN:
            valid = isNmtoken(str);
            break;
        case XP3_TYPE_NAME:
            valid = isName(str, 0);
            break;
        case XP3_TYPE_NCNAME:
        case XP3_TYPE_ID:
        case XP3_TYPE_IDREF:
        case XP3_TYPE_ENTITY:
            valid = isName(str, 1);
            break;
        default:
            break;
    }
    if (!valid)
        return(XP3_ERR_FORG0001);
    *res = xp3AtomicNewString(type, str, (size_t) xmlStrlen(str));
    return((*res != NULL) ? XP3_ERR_OK : XP3_ERR_NO_MEMORY);
}

/*
 * Cast a string to a type (F&O 3.1, 19.2): process whitespace as the
 * type says, then read the lexical form.
 */
static xp3ErrCode
castFromString(const xmlChar *str, xp3TypeId target, xp3Atomic **res) {
    xp3Representation rep = xp3TypeRepresentation(target);
    xmlChar *text;
    xp3Decimal *dec;
    xp3ErrCode err;
    double value;
    xp3DateTime dateTime;
    xp3Duration duration;

    if ((target == XP3_TYPE_STRING) || (target == XP3_TYPE_UNTYPED_ATOMIC))
        text = xmlStrdup(str);
    else if (target == XP3_TYPE_NORMALIZED_STRING)
        text = replaceSpace(str);
    else
        text = collapseSpace(str);
    if (text == NULL)
        return(XP3_ERR_NO_MEMORY);

    switch (rep) {
        case XP3_REP_STRING:
            err = castToStringType(text, target, res);
            break;
        case XP3_REP_BOOLEAN:
            if (xmlStrEqual(text, BAD_CAST "true") ||
                xmlStrEqual(text, BAD_CAST "1"))
                err = newBoolean(1, res);
            else if (xmlStrEqual(text, BAD_CAST "false") ||
                     xmlStrEqual(text, BAD_CAST "0"))
                err = newBoolean(0, res);
            else
                err = XP3_ERR_FORG0001;
            break;
        case XP3_REP_DECIMAL:
            err = xp3DecimalParse(text, (size_t) xmlStrlen(text),
                                  xp3TypeIsInteger(target), &dec);
            if (err == XP3_ERR_LIMIT)
                err = xp3TypeIsInteger(target) ? XP3_ERR_FOCA0003 :
                                                 XP3_ERR_FOCA0001;
            if (err != XP3_ERR_OK)
                break;
            if (xp3TypeIsInteger(target))
                err = newInteger(target, dec, res);
            else
                err = newDecimal(target, dec, res);
            break;
        case XP3_REP_FLOAT:
            err = parseBinaryFloat(text, 1, &value);
            if (err == XP3_ERR_OK)
                err = newFloat((float) value, res);
            break;
        case XP3_REP_DOUBLE:
            err = parseBinaryFloat(text, 0, &value);
            if (err == XP3_ERR_OK)
                err = newDouble(value, res);
            break;
        case XP3_REP_BINARY:
            if (target == XP3_TYPE_HEX_BINARY)
                err = parseHex(text, res);
            else
                err = parseBase64(text, res);
            break;
        case XP3_REP_DURATION:
            err = xp3DurationParse(target, text, &duration);
            if (err == XP3_ERR_OK)
                err = newDuration(target, &duration, res);
            break;
        case XP3_REP_DATE_TIME:
            err = xp3DateTimeParse(target, text, &dateTime);
            if (err == XP3_ERR_OK)
                err = newDateTime(target, &dateTime, res);
            break;
        default:
            /* QNames need the namespaces of the expression */
            err = XP3_ERR_UNSUPPORTED;
            break;
    }
    xmlFree(text);
    return(err);
}

/*
 * Casting between other types (F&O 3.1, 19.1 and 19.3)
 */

/* To xs:decimal or an integer type, from a number or boolean */
static xp3ErrCode
castToDecimal(const xp3Atomic *value, xp3TypeId target, xp3Atomic **res) {
    int toInteger = xp3TypeIsInteger(target);
    xp3Decimal *dec = NULL;
    xp3ErrCode err;

    switch (xp3TypeRepresentation(value->type)) {
        case XP3_REP_DECIMAL:
            if (toInteger && !xp3TypeIsInteger(value->type))
                err = xp3DecimalRound(value->u.dec, 0, XP3_ROUND_DOWN, &dec);
            else
                err = xp3DecimalCopy(value->u.dec, &dec);
            break;
        case XP3_REP_FLOAT:
        case XP3_REP_DOUBLE: {
            int isFloat = (value->type == XP3_TYPE_FLOAT);
            double number = isFloat ? (double) value->u.f : value->u.d;

            if (isnan(number) || isinf(number))
                return(XP3_ERR_FOCA0002);
            if (toInteger)
                err = integerFromBinary(trunc(number), &dec);
            else
                err = decimalFromBinary(number, isFloat, &dec);
            break;
        }
        case XP3_REP_BOOLEAN:
            dec = xp3DecimalFromInt64(value->u.boolean ? 1 : 0);
            err = (dec != NULL) ? XP3_ERR_OK : XP3_ERR_NO_MEMORY;
            break;
        default:
            return(XP3_ERR_XPTY0004);
    }
    if (err == XP3_ERR_LIMIT)
        err = toInteger ? XP3_ERR_FOCA0003 : XP3_ERR_FOCA0001;
    if (err != XP3_ERR_OK)
        return(err);
    if (toInteger)
        return(newInteger(target, dec, res));
    return(newDecimal(target, dec, res));
}

/* To xs:float or xs:double, from a number or boolean */
static xp3ErrCode
castToBinaryFloat(const xp3Atomic *value, xp3TypeId target,
                  xp3Atomic **res) {
    int toFloat = (target == XP3_TYPE_FLOAT);
    double number;
    xp3ErrCode err;

    switch (xp3TypeRepresentation(value->type)) {
        case XP3_REP_DECIMAL:
            err = decimalToBinary(value->u.dec, toFloat, &number);
            if (err != XP3_ERR_OK)
                return(err);
            break;
        case XP3_REP_FLOAT:
            number = value->u.f;
            break;
        case XP3_REP_DOUBLE:
            number = value->u.d;
            break;
        case XP3_REP_BOOLEAN:
            number = value->u.boolean ? 1 : 0;
            break;
        default:
            return(XP3_ERR_XPTY0004);
    }
    if (toFloat)
        return(newFloat((float) number, res));
    return(newDouble(number, res));
}

/**
 * Cast an atomic value to an atomic type (F&O 3.1, 19): from and to
 * strings, between numbers, booleans and binary types, and to the
 * types derived from them, checking their facets. Casting to the type
 * the value has gives the value itself.
 *
 * @param value   the value
 * @param target  the type, which is not abstract
 * @param res     where to store the result, with one reference
 * @returns XP3_ERR_OK, XP3_ERR_FORG0001 for a value that doesn't fit
 *          the type, XP3_ERR_XPTY0004 for a cast the table forbids,
 *          XP3_ERR_FOCA0001, XP3_ERR_FOCA0002 or XP3_ERR_FOCA0003 for
 *          numbers that can't be converted, XP3_ERR_FODT0001 for a
 *          year beyond +-2,147,483,647, XP3_ERR_FODT0002 for a
 *          duration whose months or seconds don't fit in 64 bits,
 *          XP3_ERR_UNSUPPORTED for QNames from strings, or
 *          XP3_ERR_NO_MEMORY
 */
xp3ErrCode
xp3CastAtomic(const xp3Atomic *value, xp3TypeId target, xp3Atomic **res) {
    xp3TypeId source = value->type;
    xp3Representation rep = xp3TypeRepresentation(target);
    xmlChar *str;
    xp3ErrCode err;
    xp3DateTime dateTime;
    xp3Duration duration;

    *res = NULL;
    if (source == target) {
        *res = xp3AtomicRef((xp3Atomic *) value);
        return(XP3_ERR_OK);
    }

    /* To strings: the canonical form, checked against derived types */
    if ((rep == XP3_REP_STRING) && (target != XP3_TYPE_ANY_URI)) {
        str = xp3AtomicToString(value);
        if (str == NULL)
            return(XP3_ERR_NO_MEMORY);
        err = castFromString(str, target, res);
        xmlFree(str);
        return(err);
    }

    /* From strings and untyped values: the lexical form */
    if (xp3TypeDerivesFrom(source, XP3_TYPE_STRING) ||
        (source == XP3_TYPE_UNTYPED_ATOMIC))
        return(castFromString(value->u.str, target, res));

    switch (rep) {
        case XP3_REP_DECIMAL:
            return(castToDecimal(value, target, res));
        case XP3_REP_FLOAT:
        case XP3_REP_DOUBLE:
            return(castToBinaryFloat(value, target, res));
        case XP3_REP_BOOLEAN:
            switch (xp3TypeRepresentation(source)) {
                case XP3_REP_DECIMAL:
                    return(newBoolean(xp3DecimalSign(value->u.dec) != 0,
                                      res));
                case XP3_REP_FLOAT:
                    return(newBoolean((value->u.f != 0) &&
                                      !isnan(value->u.f), res));
                case XP3_REP_DOUBLE:
                    return(newBoolean((value->u.d != 0) &&
                                      !isnan(value->u.d), res));
                default:
                    return(XP3_ERR_XPTY0004);
            }
        case XP3_REP_BINARY:
            if (xp3TypeRepresentation(source) != XP3_REP_BINARY)
                return(XP3_ERR_XPTY0004);
            *res = xp3AtomicNewBinary(target, value->u.bin.data,
                                      value->u.bin.len);
            return((*res != NULL) ? XP3_ERR_OK : XP3_ERR_NO_MEMORY);
        case XP3_REP_DURATION:
            if (xp3TypeRepresentation(source) != XP3_REP_DURATION)
                return(XP3_ERR_XPTY0004);
            xp3DurationConvert(&value->u.dur, target, &duration);
            return(newDuration(target, &duration, res));
        case XP3_REP_DATE_TIME:
            if (xp3TypeRepresentation(source) != XP3_REP_DATE_TIME)
                return(XP3_ERR_XPTY0004);
            err = xp3DateTimeConvert(source, &value->u.dt, target,
                                     &dateTime);
            if (err != XP3_ERR_OK)
                return(err);
            return(newDateTime(target, &dateTime, res));
        default:
            /* xs:anyURI and xs:QName only from strings or themselves */
            return(XP3_ERR_XPTY0004);
    }
}
