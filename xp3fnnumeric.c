/*
 * xp3fnnumeric.c: the functions on numeric values and the math
 * functions
 *
 * Implementations of the numeric functions of F&O 3.1,
 * https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/, bound
 * to their signatures in xp3func.c. The evaluator has applied the
 * function conversion rules to the arguments already, so an xs:numeric
 * argument is a number of any numeric type, and an untyped one has
 * been cast to xs:double.
 *
 * Integers and decimals are rounded exactly, through xp3Decimal.
 * Doubles and floats are rounded to whole numbers in binary, which is
 * exact too. With a precision, F&O 3.1, 4.4.4 rounds a double or float
 * as if converted to an xs:decimal without limits on its digits, so
 * here it is converted to its exact decimal value, at most 1074 digits
 * after the point, rounded, and cast back. The shortest decimal that
 * reads back as the double would round 35.425e0 to 35.43, not 35.42.
 *
 * The math functions (F&O 3.1, 4.8) are those of IEEE 754-2008, which
 * C99 Annex F gives libm for the special values too: log(0) is -INF,
 * sqrt(-0) is -0, pow(NaN, 0) and pow(-1, INF) are 1, and so on. Where
 * F&O differs from C, the function says so.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <math.h>
#include <stdint.h>

#include <libxml/xmlstring.h>

#include "private/xp3decimal.h"
#include "private/xp3func.h"
#include "private/xp3ops.h"
#include "private/xp3types.h"
#include "private/xp3value.h"

/*
 * Helpers
 */

/*
 * The type of a result (F&O 3.1, 4.4): the result of an argument of
 * xs:float, xs:double, xs:decimal or xs:integer has that type, and of
 * an argument of a type derived from one of them, the one it derives
 * from, so fn:abs(xs:negativeInteger(-1)) is an xs:integer.
 */
static xp3TypeId
resultType(xp3TypeId type) {
    if (xp3TypeIsInteger(type))
        return(XP3_TYPE_INTEGER);
    if (xp3TypeDerivesFrom(type, XP3_TYPE_DECIMAL))
        return(XP3_TYPE_DECIMAL);
    return(type);
}

/*
 * The only atomic value of an argument converted to a type with "?",
 * with a reference. Returns 1, 0 for the empty sequence, or -1 after
 * recording an error.
 */
static int
argNumber(xp3FuncCall *call, const xp3Value *value, xp3Atomic **atomic) {
    xp3Item item;

    *atomic = NULL;
    if (xp3ValueCount(value) == 0)
        return(0);
    if (xp3ValueItemAt(value, 0, &item) <= 0) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(-1);
    }
    *atomic = item.u.atomic;
    return(1);
}

/*
 * The precision of fn:round#2 and fn:round-half-to-even#2, clamped to
 * an int. No decimal has more than XP3_DECIMAL_MAX_DIGITS digits after
 * the point, nor a double more than 1074, so a larger precision keeps
 * every value as it is, and xp3DecimalRound() rounds anything to 0
 * below -XP3_DECIMAL_MAX_DIGITS. Returns 0, or -1 after recording an
 * error.
 */
static int
argPrecision(xp3FuncCall *call, const xp3Value *value, int *precision) {
    xp3Atomic *atomic;
    int64_t number;

    if (argNumber(call, value, &atomic) < 0)
        return(-1);
    if (xp3DecimalToInt64(atomic->u.dec, &number) < 0)
        number = (xp3DecimalSign(atomic->u.dec) < 0) ? INT64_MIN :
                                                       INT64_MAX;
    xp3AtomicUnref(atomic);
    if (number > XP3_DECIMAL_MAX_DIGITS + 1)
        number = XP3_DECIMAL_MAX_DIGITS + 1;
    else if (number < -XP3_DECIMAL_MAX_DIGITS - 1)
        number = -XP3_DECIMAL_MAX_DIGITS - 1;
    *precision = (int) number;
    return(0);
}

/* A double or float result of the type of the argument */
static xp3Value *
newBinary(xp3FuncCall *call, xp3TypeId type, double value) {
    if (type == XP3_TYPE_FLOAT)
        return(xp3FuncAtomic(call, xp3AtomicNewFloat((float) value)));
    return(xp3FuncAtomic(call, xp3AtomicNewDouble(value)));
}

/*
 * Round a finite double to a whole number. A fraction of exactly 0.5
 * is told apart from one just below it: value - floor(value) is exact
 * unless -1 < value < 0, where floor(value) is -1, and a difference
 * that only rounds to 0.5 then goes up in both modes, as it should.
 * A result of 0 has the sign of the argument (F&O 3.1, 4.4.4).
 */
static double
roundWhole(double value, xp3RoundingMode mode) {
    double result;
    double fraction;

    switch (mode) {
        case XP3_ROUND_FLOOR:
            return(floor(value));
        case XP3_ROUND_CEILING:
            /* Negative zero for -1 < value <= 0 (F&O 3.1, 4.4.2) */
            return(ceil(value));
        default:
            break;
    }
    result = floor(value);
    fraction = value - result;
    if ((fraction > 0.5) ||
        ((fraction == 0.5) &&
         ((mode == XP3_ROUND_HALF_CEILING) || (fmod(result, 2) != 0))))
        result += 1;
    if (result == 0)
        result = copysign(0.0, value);
    return(result);
}

/*
 * The exact value of a finite, nonzero double as a decimal. The double
 * is m * 2^e with an odd integer m of at most 53 bits, which is
 * m * 0.5^-e for a negative e: -e digits after the point. Sets
 * *fraction to that number of digits, 0 for a whole number, and
 * leaves *res NULL if precision is not below it, since rounding then
 * keeps the value.
 */
static xp3ErrCode
exactDecimal(double value, int precision, int *fraction, xp3Decimal **res) {
    xp3Decimal *result;
    xp3Decimal *power;
    xp3ErrCode ret = XP3_ERR_OK;
    int64_t mantissa;
    int exponent;
    int n;

    *res = NULL;
    mantissa = (int64_t) ldexp(frexp(value, &exponent), 53);
    exponent -= 53;
    while (mantissa % 2 == 0) {
        mantissa /= 2;
        exponent++;
    }
    *fraction = (exponent < 0) ? -exponent : 0;
    if (precision >= *fraction)
        return(XP3_ERR_OK);

    result = xp3DecimalFromInt64(mantissa);
    if (exponent >= 0)
        power = xp3DecimalFromInt64(2);
    else if (xp3DecimalParse(BAD_CAST "0.5", 3, 0, &power) != XP3_ERR_OK)
        power = NULL;
    if ((result == NULL) || (power == NULL)) {
        xp3DecimalFree(result);
        xp3DecimalFree(power);
        return(XP3_ERR_NO_MEMORY);
    }
    for (n = (exponent < 0) ? -exponent : exponent; n > 0; n >>= 1) {
        xp3Decimal *next;

        if (n & 1) {
            ret = xp3DecimalMul(result, power, &next);
            if (ret != XP3_ERR_OK)
                break;
            xp3DecimalFree(result);
            result = next;
        }
        if (n > 1) {
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
 * Round a double or float to a precision other than 0, through its
 * exact decimal value, and cast the result back to the type of the
 * argument (F&O 3.1, 4.4.4 and 4.4.5).
 */
static xp3Value *
roundBinary(xp3FuncCall *call, xp3Value *arg, const xp3Atomic *atomic,
            int precision, xp3RoundingMode mode) {
    int isFloat = (atomic->type == XP3_TYPE_FLOAT);
    double value = isFloat ? (double) atomic->u.f : atomic->u.d;
    xp3Decimal *exact;
    xp3Decimal *rounded;
    xp3Atomic *decimal;
    xp3Atomic *result;
    xp3ErrCode err;
    int fraction;

    err = exactDecimal(value, precision, &fraction, &exact);
    if (err != XP3_ERR_OK) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(NULL);
    }
    if (exact == NULL)
        return(xp3ValueRef(arg));
    /* Rounding a value of at most 1074 + 309 digits stays far below
     * the limit on digits, whatever the precision */
    err = xp3DecimalRound(exact, precision, mode, &rounded);
    xp3DecimalFree(exact);
    if (err != XP3_ERR_OK) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(NULL);
    }
    if (xp3DecimalSign(rounded) == 0) {
        xp3DecimalFree(rounded);
        return(newBinary(call, atomic->type, copysign(0.0, value)));
    }
    decimal = xp3AtomicNewDecimal(XP3_TYPE_DECIMAL, rounded);
    if (decimal == NULL) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(NULL);
    }
    /* Rounded once, from the decimal; a value beyond the range of the
     * type becomes infinity, as a cast does */
    err = xp3CastAtomic(decimal, atomic->type, &result);
    xp3AtomicUnref(decimal);
    if (err != XP3_ERR_OK) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(NULL);
    }
    return(xp3FuncAtomic(call, result));
}

/*
 * The functions other than fn:abs: round the argument to a precision
 * in a mode. NaN, infinities and zeros of type xs:double or xs:float
 * are returned as they are.
 */
static xp3Value *
roundNumber(xp3FuncCall *call, xp3Value **args, int count,
            xp3RoundingMode mode, const char *name) {
    xp3Value *result;
    xp3Atomic *atomic;
    xp3Decimal *dec;
    xp3ErrCode err;
    int precision = 0;
    int found;

    found = argNumber(call, args[0], &atomic);
    if (found <= 0)
        return((found == 0) ? xp3ValueEmpty() : NULL);
    if ((count == 2) && (argPrecision(call, args[1], &precision) < 0)) {
        xp3AtomicUnref(atomic);
        return(NULL);
    }

    if (xp3TypeRepresentation(atomic->type) == XP3_REP_DECIMAL) {
        err = xp3DecimalRound(atomic->u.dec, precision, mode, &dec);
        if (err == XP3_ERR_OK) {
            result = xp3FuncAtomic(call, xp3AtomicNewDecimal(
                resultType(atomic->type), dec));
        } else {
            if (err == XP3_ERR_LIMIT)
                xp3FuncError(call, XP3_ERR_FOAR0002, "the result of %s "
                             "has more than %d digits", name,
                             XP3_DECIMAL_MAX_DIGITS);
            else
                xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
            result = NULL;
        }
    } else {
        double value = (atomic->type == XP3_TYPE_FLOAT) ?
                       (double) atomic->u.f : atomic->u.d;

        if (isnan(value) || isinf(value) || (value == 0))
            result = xp3ValueRef(args[0]);
        else if (precision == 0)
            result = newBinary(call, atomic->type, roundWhole(value, mode));
        else
            result = roundBinary(call, args[0], atomic, precision, mode);
    }
    xp3AtomicUnref(atomic);
    return(result);
}

/*
 * Functions on numeric values
 */

/**
 * fn:abs#1 (F&O 3.1, 4.4.1): the absolute value. Both zeros of
 * xs:double and xs:float give positive zero, and both infinities
 * positive infinity.
 *
 * @param call   the call
 * @param args   the argument, xs:numeric?
 * @param count  1
 * @returns a number, the empty sequence, or NULL on error
 */
xp3Value *
xp3FnAbs(xp3FuncCall *call, xp3Value **args, int count) {
    xp3Value *result;
    xp3Atomic *atomic;
    xp3Decimal *dec;
    xp3ErrCode err;
    int found;

    (void) count;
    found = argNumber(call, args[0], &atomic);
    if (found <= 0)
        return((found == 0) ? xp3ValueEmpty() : NULL);
    switch (xp3TypeRepresentation(atomic->type)) {
        case XP3_REP_DECIMAL:
            if ((xp3DecimalSign(atomic->u.dec) >= 0) &&
                (resultType(atomic->type) == atomic->type)) {
                result = xp3ValueRef(args[0]);
                break;
            }
            if (xp3DecimalSign(atomic->u.dec) < 0)
                err = xp3DecimalNegate(atomic->u.dec, &dec);
            else
                err = xp3DecimalCopy(atomic->u.dec, &dec);
            if (err != XP3_ERR_OK) {
                xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
                result = NULL;
                break;
            }
            result = xp3FuncAtomic(call, xp3AtomicNewDecimal(
                resultType(atomic->type), dec));
            break;
        case XP3_REP_FLOAT:
            result = xp3FuncAtomic(call, xp3AtomicNewFloat(
                fabsf(atomic->u.f)));
            break;
        default:
            result = xp3FuncAtomic(call, xp3AtomicNewDouble(
                fabs(atomic->u.d)));
            break;
    }
    xp3AtomicUnref(atomic);
    return(result);
}

/**
 * fn:ceiling#1 (F&O 3.1, 4.4.2): the smallest whole number not below
 * the argument. A double or float between -1 and 0 gives negative
 * zero.
 *
 * @param call   the call
 * @param args   the argument, xs:numeric?
 * @param count  1
 * @returns a number, the empty sequence, or NULL on error
 */
xp3Value *
xp3FnCeiling(xp3FuncCall *call, xp3Value **args, int count) {
    return(roundNumber(call, args, count, XP3_ROUND_CEILING,
                       "fn:ceiling"));
}

/**
 * fn:floor#1 (F&O 3.1, 4.4.3): the largest whole number not above the
 * argument.
 *
 * @param call   the call
 * @param args   the argument, xs:numeric?
 * @param count  1
 * @returns a number, the empty sequence, or NULL on error
 */
xp3Value *
xp3FnFloor(xp3FuncCall *call, xp3Value **args, int count) {
    return(roundNumber(call, args, count, XP3_ROUND_FLOOR, "fn:floor"));
}

/**
 * fn:round#1 and #2 (F&O 3.1, 4.4.4): the nearest multiple of
 * 10^-$precision, the one toward positive infinity of two equally
 * near, so fn:round(-2.5) is -2. A double or float that rounds to 0
 * gives a zero of its sign, and fn:round(-0.4e0) is negative zero.
 * The precision of fn:round#1 is 0; fn:round(8452, -2) is 8500.
 *
 * @param call   the call
 * @param args   the argument (xs:numeric?) and $precision
 *               (xs:integer)
 * @param count  1 or 2
 * @returns a number, the empty sequence, or NULL on error
 */
xp3Value *
xp3FnRound(xp3FuncCall *call, xp3Value **args, int count) {
    return(roundNumber(call, args, count, XP3_ROUND_HALF_CEILING,
                       "fn:round"));
}

/**
 * fn:round-half-to-even#1 and #2 (F&O 3.1, 4.4.5): as fn:round, but
 * of two equally near values the one whose last digit is even, so
 * fn:round-half-to-even(2.5) is 2.
 *
 * @param call   the call
 * @param args   the argument (xs:numeric?) and $precision
 *               (xs:integer)
 * @param count  1 or 2
 * @returns a number, the empty sequence, or NULL on error
 */
xp3Value *
xp3FnRoundHalfToEven(xp3FuncCall *call, xp3Value **args, int count) {
    return(roundNumber(call, args, count, XP3_ROUND_HALF_EVEN,
                       "fn:round-half-to-even"));
}

/*
 * Trigonometric and exponential functions
 */

/* An xs:double result */
static xp3Value *
newDouble(xp3FuncCall *call, double value) {
    return(xp3FuncAtomic(call, xp3AtomicNewDouble(value)));
}

/* The xs:double of an argument converted to xs:double or xs:double?,
 * which is one item, never a range */
static double
argDouble(const xp3Value *value) {
    return(value->u.single.u.atomic->u.d);
}

/* A function of one xs:double? argument: the empty sequence for the
 * empty sequence */
static xp3Value *
mathApply(xp3FuncCall *call, const xp3Value *arg, double (*function)(double)) {
    if (xp3ValueCount(arg) == 0)
        return(xp3ValueEmpty());
    return(newDouble(call, function(argDouble(arg))));
}

/* exp10 is not in C99. pow(10, x) is exact for whole x in range and
 * within libm's error of pow otherwise. */
static double
exp10Double(double value) {
    return(pow(10, value));
}

/**
 * math:pi#0 (F&O 3.1, 4.8.1): the xs:double 3.141592653589793e0.
 *
 * @param call   the call
 * @param args   none
 * @param count  0
 * @returns an xs:double, or NULL on error
 */
xp3Value *
xp3MathPi(xp3FuncCall *call, xp3Value **args, int count) {
    (void) args;
    (void) count;
    return(newDouble(call, 3.141592653589793));
}

/**
 * math:exp#1 (F&O 3.1, 4.8.2): e to the power of the argument.
 *
 * @param call   the call
 * @param args   the argument, xs:double?
 * @param count  1
 * @returns an xs:double or the empty sequence, or NULL on error
 */
xp3Value *
xp3MathExp(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(mathApply(call, args[0], exp));
}

/**
 * math:exp10#1 (F&O 3.1, 4.8.3): ten to the power of the argument.
 *
 * @param call   the call
 * @param args   the argument, xs:double?
 * @param count  1
 * @returns an xs:double or the empty sequence, or NULL on error
 */
xp3Value *
xp3MathExp10(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(mathApply(call, args[0], exp10Double));
}

/**
 * math:log#1 (F&O 3.1, 4.8.4): the natural logarithm; -INF for zero,
 * NaN below it.
 *
 * @param call   the call
 * @param args   the argument, xs:double?
 * @param count  1
 * @returns an xs:double or the empty sequence, or NULL on error
 */
xp3Value *
xp3MathLog(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(mathApply(call, args[0], log));
}

/**
 * math:log10#1 (F&O 3.1, 4.8.5): the base-ten logarithm; -INF for
 * zero, NaN below it.
 *
 * @param call   the call
 * @param args   the argument, xs:double?
 * @param count  1
 * @returns an xs:double or the empty sequence, or NULL on error
 */
xp3Value *
xp3MathLog10(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(mathApply(call, args[0], log10));
}

/*
 * pown of IEEE 754-2008: x to the power of an integer, whose parity
 * decides the sign exactly. C's pow takes the exponent as a double,
 * which loses the parity of integers beyond 2^53: pow(-1, n) would be
 * 1 for every odd n above it. So the magnitude is pow(|x|, n), and an
 * odd n gives a negative x, including -0 and -INF, a negative result.
 * Above 2^53 the exponent is rounded to a double, which only matters
 * for an x within a few ulps of 1, where pow's own error is of the
 * same order. Beyond int64_t, |x|^n is 0, 1 or INF, and so is
 * pow(|x|, +-INF).
 */
static xp3Value *
powInteger(xp3FuncCall *call, double x, const xp3Decimal *n) {
    double exponent;
    double magnitude;
    int64_t small;
    int odd = 0;

    if (xp3DecimalToInt64(n, &small) == 0) {
        exponent = (double) small;
        odd = (small % 2 != 0);
    } else {
        exponent = (xp3DecimalSign(n) < 0) ? -HUGE_VAL : HUGE_VAL;
        if (signbit(x) && !isnan(x)) {
            xp3Decimal *two = xp3DecimalFromInt64(2);
            xp3Decimal *remainder = NULL;
            xp3ErrCode err = XP3_ERR_NO_MEMORY;

            if (two != NULL)
                err = xp3DecimalMod(n, two, &remainder);
            xp3DecimalFree(two);
            if (err != XP3_ERR_OK) {
                xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
                return(NULL);
            }
            odd = (xp3DecimalSign(remainder) != 0);
            xp3DecimalFree(remainder);
        }
    }
    magnitude = pow(fabs(x), exponent);
    if (signbit(x) && !isnan(x) && odd)
        magnitude = -magnitude;
    return(newDouble(call, magnitude));
}

/**
 * math:pow#2 (F&O 3.1, 4.8.6): $x to the power of $y. An xs:integer
 * $y, or a type derived from it, is exact, as pown of IEEE 754-2008
 * takes it; any other number is promoted to xs:double for pow, whose
 * special values are those of C99 Annex F: pow(-0e0, -3.0e0) is -INF,
 * pow(-2.5e0, 2.00000001e0) NaN.
 *
 * @param call   the call
 * @param args   $x (xs:double?) and $y (xs:numeric)
 * @param count  2
 * @returns an xs:double or the empty sequence, or NULL on error
 */
xp3Value *
xp3MathPow(xp3FuncCall *call, xp3Value **args, int count) {
    xp3Value *result;
    xp3Atomic *exponent;
    xp3Atomic *promoted;
    double x;
    double y;

    (void) count;
    if (xp3ValueCount(args[0]) == 0)
        return(xp3ValueEmpty());
    x = argDouble(args[0]);
    if (argNumber(call, args[1], &exponent) < 0)
        return(NULL);

    switch (xp3TypeRepresentation(exponent->type)) {
        case XP3_REP_DECIMAL:
            if (xp3TypeIsInteger(exponent->type)) {
                result = powInteger(call, x, exponent->u.dec);
                xp3AtomicUnref(exponent);
                return(result);
            }
            /* Rounded once, as the cast does */
            if (xp3CastAtomic(exponent, XP3_TYPE_DOUBLE, &promoted) !=
                XP3_ERR_OK) {
                xp3AtomicUnref(exponent);
                xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
                return(NULL);
            }
            y = promoted->u.d;
            xp3AtomicUnref(promoted);
            break;
        case XP3_REP_FLOAT:
            y = exponent->u.f;
            break;
        default:
            y = exponent->u.d;
            break;
    }
    xp3AtomicUnref(exponent);
    return(newDouble(call, pow(x, y)));
}

/**
 * math:sqrt#1 (F&O 3.1, 4.8.7): the non-negative square root; NaN
 * below zero, and -0 for -0.
 *
 * @param call   the call
 * @param args   the argument, xs:double?
 * @param count  1
 * @returns an xs:double or the empty sequence, or NULL on error
 */
xp3Value *
xp3MathSqrt(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(mathApply(call, args[0], sqrt));
}

/**
 * math:sin#1 (F&O 3.1, 4.8.8): the sine of an angle in radians.
 *
 * @param call   the call
 * @param args   the argument, xs:double?
 * @param count  1
 * @returns an xs:double or the empty sequence, or NULL on error
 */
xp3Value *
xp3MathSin(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(mathApply(call, args[0], sin));
}

/**
 * math:cos#1 (F&O 3.1, 4.8.9): the cosine of an angle in radians. The
 * notes of 4.8.9 say the result for a zero is the zero, copied from
 * math:sin; the examples, and IEEE, give 1.0e0, as cos does.
 *
 * @param call   the call
 * @param args   the argument, xs:double?
 * @param count  1
 * @returns an xs:double or the empty sequence, or NULL on error
 */
xp3Value *
xp3MathCos(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(mathApply(call, args[0], cos));
}

/**
 * math:tan#1 (F&O 3.1, 4.8.10): the tangent of an angle in radians.
 *
 * @param call   the call
 * @param args   the argument, xs:double?
 * @param count  1
 * @returns an xs:double or the empty sequence, or NULL on error
 */
xp3Value *
xp3MathTan(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(mathApply(call, args[0], tan));
}

/**
 * math:asin#1 (F&O 3.1, 4.8.11): the arc sine, from -pi/2 to pi/2;
 * NaN beyond -1 and 1.
 *
 * @param call   the call
 * @param args   the argument, xs:double?
 * @param count  1
 * @returns an xs:double or the empty sequence, or NULL on error
 */
xp3Value *
xp3MathAsin(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(mathApply(call, args[0], asin));
}

/**
 * math:acos#1 (F&O 3.1, 4.8.12): the arc cosine, from 0 to pi; NaN
 * beyond -1 and 1.
 *
 * @param call   the call
 * @param args   the argument, xs:double?
 * @param count  1
 * @returns an xs:double or the empty sequence, or NULL on error
 */
xp3Value *
xp3MathAcos(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(mathApply(call, args[0], acos));
}

/**
 * math:atan#1 (F&O 3.1, 4.8.13): the arc tangent, from -pi/2 to pi/2.
 *
 * @param call   the call
 * @param args   the argument, xs:double?
 * @param count  1
 * @returns an xs:double or the empty sequence, or NULL on error
 */
xp3Value *
xp3MathAtan(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(mathApply(call, args[0], atan));
}

/**
 * math:atan2#2 (F&O 3.1, 4.8.14): the angle of the point ($x, $y),
 * from -pi to pi, with the signs of zeros deciding the quadrant:
 * math:atan2(-0.0e0, -1) is -pi.
 *
 * @param call   the call
 * @param args   $y and $x, xs:double
 * @param count  2
 * @returns an xs:double, or NULL on error
 */
xp3Value *
xp3MathAtan2(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(newDouble(call, atan2(argDouble(args[0]), argDouble(args[1]))));
}
