/*
 * xp3decimal.c: arbitrary-precision decimal arithmetic
 *
 * A decimal is a sign, a scale and a coefficient: its value is
 * coefficient * 10^-scale. The coefficient is an unsigned integer in
 * base 10^9 limbs, least significant first. Every decimal is kept in
 * a normal form, so equal values have equal representations:
 *
 *   - zero has no limbs, scale 0 and no sign
 *   - the most significant limb is not 0
 *   - the scale is never negative
 *   - a positive scale means the coefficient does not end in 0, so
 *     1.50 is stored as 150 with scale 2 only for a moment, and then
 *     as 15 with scale 1
 *
 * Decimals are immutable. Every operation allocates a new one with
 * xmlMalloc(), and reports failure through an xp3ErrCode.
 *
 * Division of long magnitudes uses Algorithm D from Donald E. Knuth,
 * The Art of Computer Programming, Volume 2: Seminumerical Algorithms,
 * 3rd edition (Addison-Wesley, 1997), section 4.3.1 "The Classical
 * Algorithms". See magDivMod().
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

#include "private/xp3decimal.h"
#include "private/xp3double.h"

#define XP3_BASE 1000000000u
#define XP3_BASE_DIGITS 9

struct _xp3Decimal {
    int negative;           /* 1 if below zero */
    int scale;              /* digits after the point */
    size_t count;           /* number of limbs, 0 for zero */
    uint32_t limbs[];       /* coefficient, least significant first */
};

static const uint32_t xp3Pow10[XP3_BASE_DIGITS + 1] = {
    1u, 10u, 100u, 1000u, 10000u, 100000u, 1000000u, 10000000u,
    100000000u, 1000000000u
};

/*
 * Magnitudes: unsigned integers as arrays of limbs with a length.
 * Lengths are normalized, so the top limb of a nonzero magnitude is
 * not 0 and zero has length 0. Callers pass result arrays that are
 * large enough; each function says how large.
 */

static uint32_t *
limbsAlloc(size_t n) {
    if (n == 0)
        n = 1;
    if (n > SIZE_MAX / sizeof(uint32_t))
        return(NULL);
    return(xmlMalloc(n * sizeof(uint32_t)));
}

static size_t
magNorm(const uint32_t *a, size_t n) {
    while ((n > 0) && (a[n - 1] == 0))
        n--;
    return(n);
}

/* Number of decimal digits of a limb value, at least 1 */
static int
limbDigits(uint32_t v) {
    int n = 1;

    while ((n < XP3_BASE_DIGITS) && (v >= xp3Pow10[n]))
        n++;
    return(n);
}

/* Number of decimal digits of a magnitude, 0 for zero */
static size_t
magDigits(const uint32_t *a, size_t n) {
    if (n == 0)
        return(0);
    return((n - 1) * XP3_BASE_DIGITS + (size_t) limbDigits(a[n - 1]));
}

/* Number of decimal zeros the magnitude ends in, 0 for zero */
static size_t
magTrailingZeros(const uint32_t *a, size_t n) {
    size_t zeros = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        uint32_t v = a[i];

        if (v == 0) {
            zeros += XP3_BASE_DIGITS;
            continue;
        }
        while (v % 10 == 0) {
            v /= 10;
            zeros++;
        }
        return(zeros);
    }
    return(0);
}

static int
magCmp(const uint32_t *a, size_t an, const uint32_t *b, size_t bn) {
    size_t i;

    if (an != bn)
        return((an < bn) ? -1 : 1);
    for (i = an; i > 0; i--) {
        if (a[i - 1] != b[i - 1])
            return((a[i - 1] < b[i - 1]) ? -1 : 1);
    }
    return(0);
}

/* r = a + b. r holds max(an, bn) + 1 limbs. */
static size_t
magAdd(const uint32_t *a, size_t an, const uint32_t *b, size_t bn,
       uint32_t *r) {
    size_t n = (an > bn) ? an : bn;
    uint32_t carry = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        uint32_t s = carry;

        if (i < an)
            s += a[i];
        if (i < bn)
            s += b[i];
        carry = (s >= XP3_BASE);
        r[i] = carry ? s - XP3_BASE : s;
    }
    r[n] = carry;
    return(magNorm(r, n + 1));
}

/* r = a - b, where a >= b. r holds an limbs and may be a. */
static size_t
magSub(const uint32_t *a, size_t an, const uint32_t *b, size_t bn,
       uint32_t *r) {
    uint32_t borrow = 0;
    size_t i;

    for (i = 0; i < an; i++) {
        uint32_t sub = borrow + ((i < bn) ? b[i] : 0);

        if (a[i] >= sub) {
            r[i] = a[i] - sub;
            borrow = 0;
        } else {
            r[i] = a[i] + XP3_BASE - sub;
            borrow = 1;
        }
    }
    return(magNorm(r, an));
}

/* r = a * m + add, with m and add below the base. r holds an + 1
 * limbs and may be a. */
static size_t
magMulSmall(const uint32_t *a, size_t an, uint32_t m, uint32_t add,
            uint32_t *r) {
    uint64_t carry = add;
    size_t i;

    for (i = 0; i < an; i++) {
        uint64_t p = (uint64_t) a[i] * m + carry;

        r[i] = (uint32_t) (p % XP3_BASE);
        carry = p / XP3_BASE;
    }
    r[an] = (uint32_t) carry;
    return(magNorm(r, an + 1));
}

/* r = a * b, schoolbook. r holds an + bn limbs and is not a or b. */
static size_t
magMul(const uint32_t *a, size_t an, const uint32_t *b, size_t bn,
       uint32_t *r) {
    size_t i;
    size_t j;

    if ((an == 0) || (bn == 0))
        return(0);
    memset(r, 0, (an + bn) * sizeof(uint32_t));
    for (i = 0; i < an; i++) {
        uint64_t carry = 0;

        for (j = 0; j < bn; j++) {
            uint64_t p = (uint64_t) a[i] * b[j] + r[i + j] + carry;

            r[i + j] = (uint32_t) (p % XP3_BASE);
            carry = p / XP3_BASE;
        }
        r[i + bn] = (uint32_t) carry;
    }
    return(magNorm(r, an + bn));
}

/* q = a / m, returning a % m, with 0 < m <= the base. q holds an
 * limbs and may be a. */
static uint32_t
magDivSmall(const uint32_t *a, size_t an, uint32_t m, uint32_t *q,
            size_t *qn) {
    uint64_t rem = 0;
    size_t i;

    for (i = an; i > 0; i--) {
        uint64_t cur = rem * XP3_BASE + a[i - 1];

        q[i - 1] = (uint32_t) (cur / m);
        rem = cur % m;
    }
    *qn = magNorm(q, an);
    return((uint32_t) rem);
}

/* r = a * 10^k. r holds an + k / 9 + 1 limbs and is not a. */
static size_t
magShift10(const uint32_t *a, size_t an, size_t k, uint32_t *r) {
    size_t whole = k / XP3_BASE_DIGITS;

    if (an == 0)
        return(0);
    memset(r, 0, whole * sizeof(uint32_t));
    return(whole + magMulSmall(a, an, xp3Pow10[k % XP3_BASE_DIGITS], 0,
                               r + whole));
}

/*
 * q = a / b and r = a % b, for b != 0. q holds an limbs and r holds bn
 * limbs; neither is a or b. Returns 0, or -1 if memory allocation
 * failed.
 *
 * This is Algorithm D (division of nonnegative integers) from Donald
 * E. Knuth, The Art of Computer Programming, Volume 2: Seminumerical
 * Algorithms, 3rd edition, section 4.3.1, carried over to base 10^9.
 * Its steps map onto the code as follows:
 *
 *   D1  normalize: multiply both numbers by d = base / (top + 1), so
 *       the divisor's top limb is at least half the base
 *   D3  estimate each quotient limb from the top two limbs of the
 *       running remainder and the top limb of the divisor, then
 *       correct the estimate with the divisor's second limb; Knuth
 *       proves the estimate is then at most one too high
 *   D4  multiply and subtract the estimate times the divisor
 *   D5  D6  if that went negative, the estimate was one too high:
 *       decrement it and add the divisor back, which Knuth shows is
 *       rare (probability about 2 / base)
 *   D8  unnormalize: the remainder is what is left, divided by d
 */
static int
magDivMod(const uint32_t *a, size_t an, const uint32_t *b, size_t bn,
          uint32_t *q, size_t *qn, uint32_t *r, size_t *rn) {
    uint32_t *u;
    uint32_t *v;
    uint32_t d;
    size_t un;
    size_t j;

    if (magCmp(a, an, b, bn) < 0) {
        *qn = 0;
        memcpy(r, a, an * sizeof(uint32_t));
        *rn = an;
        return(0);
    }
    if (bn == 1) {
        r[0] = magDivSmall(a, an, b[0], q, qn);
        *rn = (r[0] != 0);
        return(0);
    }

    /* Normalize so the top limb of the divisor is at least half the
     * base, which keeps each quotient estimate at most 2 too high. */
    u = limbsAlloc(an + 1);
    v = limbsAlloc(bn + 1);
    if ((u == NULL) || (v == NULL)) {
        xmlFree(u);
        xmlFree(v);
        return(-1);
    }
    d = XP3_BASE / (b[bn - 1] + 1);
    magMulSmall(a, an, d, 0, u);
    if (magMulSmall(b, bn, d, 0, v) != bn) {
        /* Cannot happen: d keeps the divisor within bn limbs. */
        xmlFree(u);
        xmlFree(v);
        return(-1);
    }
    un = an + 1;

    memset(q, 0, an * sizeof(uint32_t));
    for (j = un - bn; j > 0; j--) {
        size_t k = j - 1;
        uint64_t num = (uint64_t) u[k + bn] * XP3_BASE + u[k + bn - 1];
        uint64_t qhat = num / v[bn - 1];
        uint64_t rhat = num % v[bn - 1];
        uint64_t carry = 0;
        uint32_t borrow = 0;
        size_t i;

        while ((qhat >= XP3_BASE) ||
               (qhat * v[bn - 2] > rhat * XP3_BASE + u[k + bn - 2])) {
            qhat--;
            rhat += v[bn - 1];
            if (rhat >= XP3_BASE)
                break;
        }
        /* A quotient limb is below the base; a larger estimate is
         * only ever one too high, which step D6 below corrects. */
        if (qhat >= XP3_BASE)
            qhat = XP3_BASE - 1;

        /* u[k .. k + bn] -= qhat * v */
        for (i = 0; i < bn; i++) {
            uint64_t p = qhat * v[i] + carry;
            uint32_t low = (uint32_t) (p % XP3_BASE);
            uint32_t sub = low + borrow;

            carry = p / XP3_BASE;
            if (u[k + i] >= sub) {
                u[k + i] -= sub;
                borrow = 0;
            } else {
                u[k + i] = u[k + i] + XP3_BASE - sub;
                borrow = 1;
            }
        }
        if ((uint64_t) u[k + bn] >= carry + borrow) {
            u[k + bn] -= (uint32_t) (carry + borrow);
        } else {
            /* The estimate was one too high: add the divisor back. */
            uint32_t c = 0;

            u[k + bn] = 0;
            qhat--;
            for (i = 0; i < bn; i++) {
                uint32_t s = u[k + i] + v[i] + c;

                c = (s >= XP3_BASE);
                u[k + i] = c ? s - XP3_BASE : s;
            }
        }
        q[k] = (uint32_t) qhat;
    }
    *qn = magNorm(q, an);

    /* The remainder is u / d. */
    magDivSmall(u, bn, d, r, rn);
    xmlFree(u);
    xmlFree(v);
    return(0);
}

/*
 * Decimals
 */

static xp3Decimal *
decimalAlloc(size_t count) {
    xp3Decimal *dec;

    if (count > (SIZE_MAX - offsetof(xp3Decimal, limbs)) / sizeof(uint32_t))
        return(NULL);
    dec = xmlMalloc(offsetof(xp3Decimal, limbs) + count * sizeof(uint32_t));
    if (dec == NULL)
        return(NULL);
    dec->negative = 0;
    dec->scale = 0;
    dec->count = count;
    return(dec);
}

/* Digits of a decimal for the limit: before plus after the point. */
static size_t
decimalDigits(size_t coefDigits, long scale) {
    if (scale <= 0)
        return(coefDigits + (size_t) -scale);
    return(((size_t) scale > coefDigits) ? (size_t) scale : coefDigits);
}

/*
 * Build a normalized decimal from a magnitude and a scale, which may
 * be negative here. Checks the digit limit.
 */
static xp3ErrCode
decimalMake(int negative, long scale, const uint32_t *mag, size_t n,
            xp3Decimal **res) {
    xp3Decimal *dec;
    size_t zeros;
    size_t strip;

    *res = NULL;
    n = magNorm(mag, n);
    if (n == 0) {
        dec = decimalAlloc(0);
        if (dec == NULL)
            return(XP3_ERR_NO_MEMORY);
        *res = dec;
        return(XP3_ERR_OK);
    }

    zeros = magTrailingZeros(mag, n);
    strip = 0;
    if (scale > 0)
        strip = (zeros < (size_t) scale) ? zeros : (size_t) scale;
    if (decimalDigits(magDigits(mag, n) - strip, scale - (long) strip) >
        XP3_DECIMAL_MAX_DIGITS)
        return(XP3_ERR_LIMIT);

    if (scale < 0) {
        size_t k = (size_t) -scale;

        dec = decimalAlloc(n + k / XP3_BASE_DIGITS + 1);
        if (dec == NULL)
            return(XP3_ERR_NO_MEMORY);
        dec->count = magShift10(mag, n, k, dec->limbs);
        dec->scale = 0;
    } else {
        size_t whole = strip / XP3_BASE_DIGITS;

        dec = decimalAlloc(n);
        if (dec == NULL)
            return(XP3_ERR_NO_MEMORY);
        /* Drop whole zero limbs, then the remaining zeros. */
        memcpy(dec->limbs, mag + whole, (n - whole) * sizeof(uint32_t));
        dec->count = n - whole;
        if (strip % XP3_BASE_DIGITS != 0)
            magDivSmall(dec->limbs, dec->count,
                        xp3Pow10[strip % XP3_BASE_DIGITS], dec->limbs,
                        &dec->count);
        dec->scale = (int) (scale - (long) strip);
    }
    dec->negative = negative;
    *res = dec;
    return(XP3_ERR_OK);
}

/* The coefficient aligned to a larger scale, in a new array. */
static uint32_t *
decimalAligned(const xp3Decimal *dec, int scale, size_t *n) {
    size_t k = (size_t) (scale - dec->scale);
    uint32_t *r = limbsAlloc(dec->count + k / XP3_BASE_DIGITS + 1);

    if (r != NULL)
        *n = magShift10(dec->limbs, dec->count, k, r);
    return(r);
}

/**
 * Parse the lexical form of an xs:decimal, or of an xs:integer if
 * integerOnly is set: an optional sign, then digits with an optional
 * point. Whitespace is not allowed; casts collapse it first.
 *
 * @param str          the text, not necessarily terminated
 * @param len          its length in bytes
 * @param integerOnly  reject a decimal point
 * @param res          where to store the decimal
 * @returns XP3_ERR_OK, XP3_ERR_FORG0001 for an invalid lexical form,
 *          XP3_ERR_LIMIT if there are too many digits, or
 *          XP3_ERR_NO_MEMORY
 */
xp3ErrCode
xp3DecimalParse(const xmlChar *str, size_t len, int integerOnly,
                xp3Decimal **res) {
    size_t i = 0;
    size_t intStart;
    size_t intEnd;
    size_t fracStart;
    size_t fracEnd;
    size_t first;
    size_t last;
    size_t sig;
    size_t count;
    size_t k;
    long scale;
    int negative = 0;
    xp3Decimal *dec;
    uint32_t *limbs;
    xp3ErrCode ret;

    *res = NULL;
    if (str == NULL)
        return(XP3_ERR_FORG0001);
    if ((i < len) && ((str[i] == '+') || (str[i] == '-'))) {
        negative = (str[i] == '-');
        i++;
    }
    intStart = i;
    while ((i < len) && (str[i] >= '0') && (str[i] <= '9'))
        i++;
    intEnd = i;
    fracStart = fracEnd = i;
    if ((i < len) && (str[i] == '.')) {
        if (integerOnly)
            return(XP3_ERR_FORG0001);
        i++;
        fracStart = i;
        while ((i < len) && (str[i] >= '0') && (str[i] <= '9'))
            i++;
        fracEnd = i;
    }
    if ((i != len) || ((intEnd == intStart) && (fracEnd == fracStart)))
        return(XP3_ERR_FORG0001);

    /*
     * The digits are the integer part followed by the fraction. Find
     * the first nonzero digit, and the last digit to keep: trailing
     * zeros of the fraction carry no value.
     */
    scale = (long) (fracEnd - fracStart);
    while ((fracEnd > fracStart) && (str[fracEnd - 1] == '0')) {
        fracEnd--;
        scale--;
    }
    first = intStart;
    while ((first < intEnd) && (str[first] == '0'))
        first++;
    if (first == intEnd) {
        first = fracStart;
        while ((first < fracEnd) && (str[first] == '0'))
            first++;
        if (first == fracEnd)
            return(decimalMake(0, 0, NULL, 0, res));
    }
    last = (fracEnd > fracStart) ? fracEnd : intEnd;

    sig = last - first;
    if ((first < intEnd) && (last > fracStart) && (fracEnd > fracStart))
        sig--;                      /* the point */
    if ((sig > XP3_DECIMAL_MAX_DIGITS) || (scale > XP3_DECIMAL_MAX_DIGITS))
        return(XP3_ERR_LIMIT);

    /* Fill the limbs from the least significant digit. */
    count = (sig + XP3_BASE_DIGITS - 1) / XP3_BASE_DIGITS;
    limbs = limbsAlloc(count);
    if (limbs == NULL)
        return(XP3_ERR_NO_MEMORY);
    memset(limbs, 0, count * sizeof(uint32_t));
    k = 0;
    for (i = last; i > first; i--) {
        if (str[i - 1] == '.')
            continue;
        limbs[k / XP3_BASE_DIGITS] +=
            (uint32_t) (str[i - 1] - '0') * xp3Pow10[k % XP3_BASE_DIGITS];
        k++;
    }

    ret = decimalMake(negative, scale, limbs, count, &dec);
    xmlFree(limbs);
    *res = dec;
    return(ret);
}

/**
 * Create a decimal from a 64-bit integer.
 *
 * @param value  the integer
 * @returns the decimal, or NULL if memory allocation failed
 */
xp3Decimal *
xp3DecimalFromInt64(int64_t value) {
    uint64_t mag;
    uint32_t limbs[3];
    size_t n = 0;
    xp3Decimal *dec;

    mag = (value < 0) ? (uint64_t) -(value + 1) + 1 : (uint64_t) value;
    while (mag > 0) {
        limbs[n++] = (uint32_t) (mag % XP3_BASE);
        mag /= XP3_BASE;
    }
    if (decimalMake(value < 0, 0, limbs, n, &dec) != XP3_ERR_OK)
        return(NULL);
    return(dec);
}

/**
 * Convert an integer-valued decimal to a 64-bit integer.
 *
 * @param dec    the decimal
 * @param value  where to store the integer
 * @returns 0, or -1 if the decimal has a fraction or does not fit
 */
int
xp3DecimalToInt64(const xp3Decimal *dec, int64_t *value) {
    uint64_t mag = 0;
    size_t i;

    if ((dec == NULL) || (dec->scale != 0) || (dec->count > 3))
        return(-1);
    for (i = dec->count; i > 0; i--) {
        if (mag > (UINT64_MAX - dec->limbs[i - 1]) / XP3_BASE)
            return(-1);
        mag = mag * XP3_BASE + dec->limbs[i - 1];
    }
    if (dec->negative) {
        if (mag > (uint64_t) INT64_MAX + 1)
            return(-1);
        *value = (mag == (uint64_t) INT64_MAX + 1) ? INT64_MIN :
                                                      -(int64_t) mag;
    } else {
        if (mag > (uint64_t) INT64_MAX)
            return(-1);
        *value = (int64_t) mag;
    }
    return(0);
}

/**
 * Convert a decimal to its canonical string: no exponent, no leading
 * or trailing zeros, and no point for integer values, for example
 * "-0.5", "3" or "1000".
 *
 * @param dec  the decimal
 * @returns the string, to be freed with xmlFree(), or NULL if memory
 *          allocation failed
 */
xmlChar *
xp3DecimalToString(const xp3Decimal *dec) {
    size_t digits;
    size_t len;
    size_t intLen;
    xmlChar *coef;
    xmlChar *ret;
    xmlChar *out;
    size_t i;
    char top[16];

    if (dec == NULL)
        return(NULL);
    if (dec->count == 0)
        return(xmlStrdup(BAD_CAST "0"));

    /* The coefficient's digits, most significant first */
    digits = magDigits(dec->limbs, dec->count);
    coef = xmlMalloc(digits + 1);
    if (coef == NULL)
        return(NULL);
    snprintf(top, sizeof(top), "%u", (unsigned) dec->limbs[dec->count - 1]);
    memcpy(coef, top, strlen(top));
    out = coef + strlen(top);
    for (i = dec->count - 1; i > 0; i--) {
        snprintf((char *) out, 10, "%09u", (unsigned) dec->limbs[i - 1]);
        out += XP3_BASE_DIGITS;
    }

    len = (size_t) dec->negative + digits + 3 +
          (((size_t) dec->scale > digits) ? (size_t) dec->scale - digits : 0);
    ret = xmlMalloc(len);
    if (ret == NULL) {
        xmlFree(coef);
        return(NULL);
    }
    out = ret;
    if (dec->negative)
        *out++ = '-';
    if (dec->scale == 0) {
        memcpy(out, coef, digits);
        out += digits;
    } else if (digits > (size_t) dec->scale) {
        intLen = digits - (size_t) dec->scale;
        memcpy(out, coef, intLen);
        out += intLen;
        *out++ = '.';
        memcpy(out, coef + intLen, (size_t) dec->scale);
        out += dec->scale;
    } else {
        *out++ = '0';
        *out++ = '.';
        for (i = digits; i < (size_t) dec->scale; i++)
            *out++ = '0';
        memcpy(out, coef, digits);
        out += digits;
    }
    *out = 0;
    xmlFree(coef);
    return(ret);
}

/**
 * Convert a decimal to the nearest double, as a cast does. Values
 * beyond the double range become infinity.
 *
 * @param dec  the decimal
 * @returns the double, or NaN if memory allocation failed
 */
double
xp3DecimalToDouble(const xp3Decimal *dec) {
    xmlChar *str = xp3DecimalToString(dec);
    double ret;

    if (str == NULL)
        return(xp3DoubleNaN());
    ret = xp3DoubleParse((const char *) str);
    xmlFree(str);
    return(ret);
}

/**
 * Free a decimal. NULL is ignored.
 *
 * @param dec  the decimal
 */
void
xp3DecimalFree(xp3Decimal *dec) {
    xmlFree(dec);
}

/**
 * Get the sign of a decimal.
 *
 * @param dec  the decimal
 * @returns -1, 0 or 1
 */
int
xp3DecimalSign(const xp3Decimal *dec) {
    if ((dec == NULL) || (dec->count == 0))
        return(0);
    return(dec->negative ? -1 : 1);
}

/**
 * Whether a decimal has no fractional part.
 *
 * @param dec  the decimal
 * @returns 1 or 0
 */
int
xp3DecimalIsInteger(const xp3Decimal *dec) {
    return((dec != NULL) && (dec->scale == 0));
}

/* The coefficient digit at a power of ten, without allocating */
static int
decimalDigitAt(const xp3Decimal *dec, long exponent) {
    long k = exponent + dec->scale;

    if ((k < 0) || ((size_t) k >= dec->count * XP3_BASE_DIGITS))
        return(0);
    return((int) ((dec->limbs[k / XP3_BASE_DIGITS] /
                   xp3Pow10[k % XP3_BASE_DIGITS]) % 10));
}

/* Power of ten of the most significant digit */
static long
decimalTopExponent(const xp3Decimal *dec) {
    return((long) magDigits(dec->limbs, dec->count) - 1 - dec->scale);
}

/**
 * Compare two decimals.
 *
 * @param a  the first decimal
 * @param b  the second decimal
 * @returns -1 if a < b, 0 if equal, 1 if a > b
 */
int
xp3DecimalCompare(const xp3Decimal *a, const xp3Decimal *b) {
    int sa = xp3DecimalSign(a);
    int sb = xp3DecimalSign(b);
    long ea;
    long eb;
    long e;
    long low;

    if (sa != sb)
        return((sa < sb) ? -1 : 1);
    if (sa == 0)
        return(0);

    /* Same sign: compare magnitudes, then flip for negatives. */
    ea = decimalTopExponent(a);
    eb = decimalTopExponent(b);
    if (ea != eb)
        return(((ea < eb) ? -1 : 1) * sa);
    low = -((a->scale > b->scale) ? a->scale : b->scale);
    for (e = ea; e >= low; e--) {
        int da = decimalDigitAt(a, e);
        int db = decimalDigitAt(b, e);

        if (da != db)
            return(((da < db) ? -1 : 1) * sa);
    }
    return(0);
}

/**
 * Negate a decimal.
 *
 * @param a    the decimal
 * @param res  where to store the result
 * @returns XP3_ERR_OK or XP3_ERR_NO_MEMORY
 */
xp3ErrCode
xp3DecimalNegate(const xp3Decimal *a, xp3Decimal **res) {
    xp3ErrCode ret = decimalMake(!a->negative, a->scale, a->limbs,
                                 a->count, res);

    return(ret);
}

/* a + b, with b's sign flipped if negateB is set */
static xp3ErrCode
decimalAddSigned(const xp3Decimal *a, const xp3Decimal *b, int negateB,
                 xp3Decimal **res) {
    int scale = (a->scale > b->scale) ? a->scale : b->scale;
    int negA = a->negative;
    int negB = (b->count != 0) ? (b->negative != negateB) : 0;
    uint32_t *x;
    uint32_t *y;
    uint32_t *r;
    size_t xn = 0;
    size_t yn = 0;
    size_t rn;
    int neg;
    xp3ErrCode ret;

    *res = NULL;
    x = decimalAligned(a, scale, &xn);
    y = decimalAligned(b, scale, &yn);
    r = limbsAlloc(((xn > yn) ? xn : yn) + 1);
    if ((x == NULL) || (y == NULL) || (r == NULL)) {
        ret = XP3_ERR_NO_MEMORY;
        goto done;
    }
    if (negA == negB) {
        rn = magAdd(x, xn, y, yn, r);
        neg = negA;
    } else if (magCmp(x, xn, y, yn) >= 0) {
        rn = magSub(x, xn, y, yn, r);
        neg = negA;
    } else {
        rn = magSub(y, yn, x, xn, r);
        neg = negB;
    }
    ret = decimalMake(neg, scale, r, rn, res);

done:
    xmlFree(x);
    xmlFree(y);
    xmlFree(r);
    return(ret);
}

/**
 * Add two decimals exactly.
 *
 * @param a    the first operand
 * @param b    the second operand
 * @param res  where to store the sum
 * @returns XP3_ERR_OK, XP3_ERR_LIMIT or XP3_ERR_NO_MEMORY
 */
xp3ErrCode
xp3DecimalAdd(const xp3Decimal *a, const xp3Decimal *b, xp3Decimal **res) {
    return(decimalAddSigned(a, b, 0, res));
}

/**
 * Subtract two decimals exactly.
 *
 * @param a    the minuend
 * @param b    the subtrahend
 * @param res  where to store the difference
 * @returns XP3_ERR_OK, XP3_ERR_LIMIT or XP3_ERR_NO_MEMORY
 */
xp3ErrCode
xp3DecimalSub(const xp3Decimal *a, const xp3Decimal *b, xp3Decimal **res) {
    return(decimalAddSigned(a, b, 1, res));
}

/**
 * Multiply two decimals exactly.
 *
 * @param a    the first operand
 * @param b    the second operand
 * @param res  where to store the product
 * @returns XP3_ERR_OK, XP3_ERR_LIMIT or XP3_ERR_NO_MEMORY
 */
xp3ErrCode
xp3DecimalMul(const xp3Decimal *a, const xp3Decimal *b, xp3Decimal **res) {
    uint32_t *r;
    size_t rn;
    xp3ErrCode ret;

    *res = NULL;
    r = limbsAlloc(a->count + b->count);
    if (r == NULL)
        return(XP3_ERR_NO_MEMORY);
    rn = magMul(a->limbs, a->count, b->limbs, b->count, r);
    ret = decimalMake(a->negative != b->negative,
                      (long) a->scale + b->scale, r, rn, res);
    xmlFree(r);
    return(ret);
}

/* Whether to add one to a truncated quotient magnitude q, given the
 * remainder r of den. Returns 1, 0, or -1 if memory allocation
 * failed. */
static int
roundUp(const uint32_t *q, size_t qn, const uint32_t *r, size_t rn,
        const uint32_t *den, size_t dn, int negative, xp3RoundingMode mode) {
    uint32_t *twice;
    size_t tn;
    int cmp;

    if (rn == 0)
        return(0);
    switch (mode) {
        case XP3_ROUND_DOWN:
            return(0);
        case XP3_ROUND_FLOOR:
            return(negative);
        case XP3_ROUND_CEILING:
            return(!negative);
        default:
            break;
    }

    /* Nearest: compare twice the remainder with the divisor. */
    twice = limbsAlloc(rn + 1);
    if (twice == NULL)
        return(-1);
    tn = magMulSmall(r, rn, 2, 0, twice);
    cmp = magCmp(twice, tn, den, dn);
    xmlFree(twice);
    if (cmp != 0)
        return(cmp > 0);
    switch (mode) {
        case XP3_ROUND_HALF_UP:
            return(1);
        case XP3_ROUND_HALF_EVEN:
            return((qn > 0) && (q[0] % 2 == 1));
        case XP3_ROUND_HALF_CEILING:
            return(!negative);
        default:
            return(0);
    }
}

/*
 * Quotient of num / den rounded to an integer magnitude by mode, as a
 * decimal with the given scale and sign.
 */
static xp3ErrCode
decimalQuotient(const uint32_t *num, size_t nn, const uint32_t *den,
                size_t dn, int negative, long scale, xp3RoundingMode mode,
                xp3Decimal **res) {
    uint32_t *q = limbsAlloc(nn + 1);
    uint32_t *r = limbsAlloc(dn);
    size_t qn = 0;
    size_t rn = 0;
    int up;
    xp3ErrCode ret = XP3_ERR_NO_MEMORY;

    if ((q == NULL) || (r == NULL) ||
        (magDivMod(num, nn, den, dn, q, &qn, r, &rn) < 0))
        goto done;
    up = roundUp(q, qn, r, rn, den, dn, negative, mode);
    if (up < 0)
        goto done;
    if (up)
        qn = magMulSmall(q, qn, 1, 1, q);
    ret = decimalMake(negative, scale, q, qn, res);

done:
    xmlFree(q);
    xmlFree(r);
    return(ret);
}

/* The scale without trailing zeros, negative for integers that end in
 * zeros: 1.50 has 1, 300 has -2. */
static long
decimalNormScale(const xp3Decimal *dec) {
    if (dec->scale > 0)
        return(dec->scale);
    return(-(long) magTrailingZeros(dec->limbs, dec->count));
}

/* Whether an integer is in [0, 2^31), where Saxon keeps its scale 0 */
static int
decimalIsSmallNatural(const xp3Decimal *dec) {
    int64_t v;

    return((xp3DecimalToInt64(dec, &v) == 0) && (v >= 0) &&
           (v < ((int64_t) 1 << 31)));
}

/* Division at the scale max(18, sa - sb + 18), half down */
static xp3ErrCode
decimalDivScaled(const xp3Decimal *a, long sa, const xp3Decimal *b, long sb,
                 xp3Decimal **res) {
    long scale;
    long e;
    uint32_t *num = NULL;
    uint32_t *den = NULL;
    const uint32_t *n = a->limbs;
    const uint32_t *d = b->limbs;
    size_t nn = a->count;
    size_t dn = b->count;
    xp3ErrCode ret;

    *res = NULL;
    if (b->count == 0)
        return(XP3_ERR_FOAR0001);
    if (a->count == 0)
        return(decimalMake(0, 0, NULL, 0, res));

    scale = sa - sb + XP3_DECIMAL_DIV_DIGITS;
    if (scale < XP3_DECIMAL_DIV_DIGITS)
        scale = XP3_DECIMAL_DIV_DIGITS;
    if (scale > XP3_DECIMAL_MAX_DIGITS)
        return(XP3_ERR_LIMIT);

    /*
     * a / b = (A / B) * 10^(sb - sa), so the coefficient at the target
     * scale is A * 10^(scale + sb - sa) / B.
     */
    e = scale + b->scale - a->scale;
    if (e >= 0) {
        if (magDigits(n, nn) + (size_t) e > 3 * XP3_DECIMAL_MAX_DIGITS)
            return(XP3_ERR_LIMIT);
        num = limbsAlloc(nn + (size_t) e / XP3_BASE_DIGITS + 1);
        if (num == NULL)
            return(XP3_ERR_NO_MEMORY);
        nn = magShift10(n, nn, (size_t) e, num);
        n = num;
    } else {
        den = limbsAlloc(dn + (size_t) -e / XP3_BASE_DIGITS + 1);
        if (den == NULL)
            return(XP3_ERR_NO_MEMORY);
        dn = magShift10(d, dn, (size_t) -e, den);
        d = den;
    }
    ret = decimalQuotient(n, nn, d, dn, a->negative != b->negative, scale,
                          XP3_ROUND_HALF_DOWN, res);
    xmlFree(num);
    xmlFree(den);
    return(ret);
}

/**
 * Divide two xs:decimal values, as the div operator does. Use
 * xp3DecimalDivTyped() when an operand is an xs:integer.
 *
 * The precision follows Saxon (doc/DESIGN.md 4.3, decision D3): with
 * s1 and s2 the scales of the operands without trailing zeros, which
 * are negative for values that end in zeros, the quotient is rounded
 * to max(18, s1 - s2 + 18) digits after the point, to nearest with
 * ties toward zero.
 *
 * @param a    the dividend
 * @param b    the divisor
 * @param res  where to store the quotient
 * @returns XP3_ERR_OK, XP3_ERR_FOAR0001 for division by zero,
 *          XP3_ERR_LIMIT or XP3_ERR_NO_MEMORY
 */
xp3ErrCode
xp3DecimalDiv(const xp3Decimal *a, const xp3Decimal *b, xp3Decimal **res) {
    return(xp3DecimalDivTyped(a, 0, b, 0, res));
}

/**
 * Divide two values, either of which may be an xs:integer, with
 * Saxon's precision (doc/DESIGN.md 4.3). Saxon takes the scale of an
 * integer operand as 0 instead of stripping its trailing zeros, except
 * when both operands are integers and either is negative or at least
 * 2^31: then both go through Java's BigInteger and are stripped. So
 * 1 div 300000 has 18 digits after the point, while 1 div 300000.0
 * and 1 div 3000000000 have 23 and 27. The rule was taken from
 * Saxon-HE 12.5 (Calculator.internalDecimalDivide, Int64Value.div and
 * isLong) and confirmed by measurement.
 *
 * @param a           the dividend
 * @param aIsInteger  whether the dividend is an xs:integer
 * @param b           the divisor
 * @param bIsInteger  whether the divisor is an xs:integer
 * @param res         where to store the quotient
 * @returns XP3_ERR_OK, XP3_ERR_FOAR0001 for division by zero,
 *          XP3_ERR_LIMIT or XP3_ERR_NO_MEMORY
 */
xp3ErrCode
xp3DecimalDivTyped(const xp3Decimal *a, int aIsInteger, const xp3Decimal *b,
                   int bIsInteger, xp3Decimal **res) {
    long sa = decimalNormScale(a);
    long sb = decimalNormScale(b);

    if (aIsInteger && bIsInteger) {
        if (decimalIsSmallNatural(a) && decimalIsSmallNatural(b))
            sa = sb = 0;
    } else {
        if (aIsInteger)
            sa = 0;
        if (bIsInteger)
            sb = 0;
    }
    return(decimalDivScaled(a, sa, b, sb, res));
}

/**
 * Divide two decimals and truncate the quotient to an integer, as the
 * idiv operator does.
 *
 * @param a    the dividend
 * @param b    the divisor
 * @param res  where to store the integer quotient
 * @returns XP3_ERR_OK, XP3_ERR_FOAR0001 for division by zero,
 *          XP3_ERR_LIMIT or XP3_ERR_NO_MEMORY
 */
xp3ErrCode
xp3DecimalIdiv(const xp3Decimal *a, const xp3Decimal *b,
               xp3Decimal **res) {
    int scale = (a->scale > b->scale) ? a->scale : b->scale;
    uint32_t *x;
    uint32_t *y;
    size_t xn = 0;
    size_t yn = 0;
    xp3ErrCode ret;

    *res = NULL;
    if (b->count == 0)
        return(XP3_ERR_FOAR0001);
    x = decimalAligned(a, scale, &xn);
    y = decimalAligned(b, scale, &yn);
    if ((x == NULL) || (y == NULL))
        ret = XP3_ERR_NO_MEMORY;
    else
        ret = decimalQuotient(x, xn, y, yn, a->negative != b->negative, 0,
                              XP3_ROUND_DOWN, res);
    xmlFree(x);
    xmlFree(y);
    return(ret);
}

/**
 * Remainder of a truncating division, as the mod operator does. The
 * result has the sign of the dividend.
 *
 * @param a    the dividend
 * @param b    the divisor
 * @param res  where to store the remainder
 * @returns XP3_ERR_OK, XP3_ERR_FOAR0001 for division by zero,
 *          XP3_ERR_LIMIT or XP3_ERR_NO_MEMORY
 */
xp3ErrCode
xp3DecimalMod(const xp3Decimal *a, const xp3Decimal *b, xp3Decimal **res) {
    int scale = (a->scale > b->scale) ? a->scale : b->scale;
    uint32_t *x;
    uint32_t *y;
    uint32_t *q = NULL;
    uint32_t *r = NULL;
    size_t xn = 0;
    size_t yn = 0;
    size_t qn;
    size_t rn;
    xp3ErrCode ret = XP3_ERR_NO_MEMORY;

    *res = NULL;
    if (b->count == 0)
        return(XP3_ERR_FOAR0001);
    x = decimalAligned(a, scale, &xn);
    y = decimalAligned(b, scale, &yn);
    if ((x == NULL) || (y == NULL))
        goto done;
    q = limbsAlloc(xn + 1);
    r = limbsAlloc(yn);
    if ((q == NULL) || (r == NULL) ||
        (magDivMod(x, xn, y, yn, q, &qn, r, &rn) < 0))
        goto done;
    ret = decimalMake(a->negative, scale, r, rn, res);

done:
    xmlFree(x);
    xmlFree(y);
    xmlFree(q);
    xmlFree(r);
    return(ret);
}

/**
 * Round a decimal to a number of digits after the point. A negative
 * precision rounds to tens, hundreds and so on, as in
 * fn:round(12345, -2), which is 12300.
 *
 * @param a          the decimal
 * @param precision  digits to keep after the point
 * @param mode       the rounding mode
 * @param res        where to store the result
 * @returns XP3_ERR_OK, XP3_ERR_LIMIT or XP3_ERR_NO_MEMORY
 */
xp3ErrCode
xp3DecimalRound(const xp3Decimal *a, int precision, xp3RoundingMode mode,
                xp3Decimal **res) {
    size_t k;
    size_t digits;
    uint32_t *den;
    size_t dn;
    xp3ErrCode ret;
    uint32_t one = 1;

    *res = NULL;
    if ((a->count == 0) || (precision >= a->scale))
        return(decimalMake(a->negative, a->scale, a->limbs, a->count, res));

    /* Drop k digits of the coefficient. Precisions far below the value
     * round it to 0 or to a power of ten beyond the digit limit. */
    if (precision < -XP3_DECIMAL_MAX_DIGITS)
        precision = -XP3_DECIMAL_MAX_DIGITS - 1;
    k = (size_t) a->scale + (size_t) ((precision < 0) ? -precision : 0) -
        (size_t) ((precision > 0) ? precision : 0);
    digits = magDigits(a->limbs, a->count);
    if (k > digits + 1) {
        /* The whole value is less than half a unit of the result. */
        int up = 0;

        if ((mode == XP3_ROUND_FLOOR) && a->negative)
            up = 1;
        else if ((mode == XP3_ROUND_CEILING) && !a->negative)
            up = 1;
        if (!up)
            return(decimalMake(0, 0, NULL, 0, res));
        return(decimalMake(a->negative, precision, &one, 1, res));
    }

    den = limbsAlloc(k / XP3_BASE_DIGITS + 2);
    if (den == NULL)
        return(XP3_ERR_NO_MEMORY);
    dn = magShift10(&one, 1, k, den);
    ret = decimalQuotient(a->limbs, a->count, den, dn, a->negative,
                          precision, mode, res);
    xmlFree(den);
    return(ret);
}
