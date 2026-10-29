/**
 * @file
 *
 * @brief Arbitrary-precision decimal arithmetic (internal)
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XP3DECIMAL_H
#define XP3_XP3DECIMAL_H

#include <stddef.h>
#include <stdint.h>

#include <libxml/xmlstring.h>

#include "private/xp3error.h"

/*
 * Limit on the number of digits in a decimal, counting the digits
 * before and after the point, for example 7 for 1234.567. Results and
 * parsed values above it give XP3_ERR_LIMIT, so untrusted expressions
 * cannot make arithmetic allocate without bound. Multiplying two
 * numbers at the limit takes about a tenth of a second.
 */
#define XP3_DECIMAL_MAX_DIGITS 100000

/*
 * Digits after the point that division keeps at least. See
 * xp3DecimalDiv() for the full rule.
 */
#define XP3_DECIMAL_DIV_DIGITS 18

typedef struct _xp3Decimal xp3Decimal;

/* Rounding modes */
typedef enum {
    XP3_ROUND_FLOOR = 0,        /* toward negative infinity */
    XP3_ROUND_CEILING,          /* toward positive infinity */
    XP3_ROUND_DOWN,             /* toward zero, truncation */
    XP3_ROUND_HALF_DOWN,        /* nearest, ties toward zero */
    XP3_ROUND_HALF_UP,          /* nearest, ties away from zero */
    XP3_ROUND_HALF_EVEN,        /* nearest, ties to an even digit */
    XP3_ROUND_HALF_CEILING      /* nearest, ties toward positive
                                 * infinity, as fn:round */
} xp3RoundingMode;

xp3ErrCode
xp3DecimalParse(const xmlChar *str, size_t len, int integerOnly,
                xp3Decimal **res);

xp3Decimal *
xp3DecimalFromInt64(int64_t value);

int
xp3DecimalToInt64(const xp3Decimal *dec, int64_t *value);

xmlChar *
xp3DecimalToString(const xp3Decimal *dec);

double
xp3DecimalToDouble(const xp3Decimal *dec);

void
xp3DecimalFree(xp3Decimal *dec);

int
xp3DecimalSign(const xp3Decimal *dec);

size_t
xp3DecimalDigits(const xp3Decimal *dec);

int
xp3DecimalIsInteger(const xp3Decimal *dec);

int
xp3DecimalCompare(const xp3Decimal *a, const xp3Decimal *b);

xp3ErrCode
xp3DecimalCopy(const xp3Decimal *a, xp3Decimal **res);

xp3ErrCode
xp3DecimalNegate(const xp3Decimal *a, xp3Decimal **res);

xp3ErrCode
xp3DecimalAdd(const xp3Decimal *a, const xp3Decimal *b, xp3Decimal **res);

xp3ErrCode
xp3DecimalSub(const xp3Decimal *a, const xp3Decimal *b, xp3Decimal **res);

xp3ErrCode
xp3DecimalMul(const xp3Decimal *a, const xp3Decimal *b, xp3Decimal **res);

xp3ErrCode
xp3DecimalDiv(const xp3Decimal *a, const xp3Decimal *b, xp3Decimal **res);

xp3ErrCode
xp3DecimalDivTyped(const xp3Decimal *a, int aIsInteger, const xp3Decimal *b,
                   int bIsInteger, xp3Decimal **res);

xp3ErrCode
xp3DecimalIdiv(const xp3Decimal *a, const xp3Decimal *b,
               xp3Decimal **res);

xp3ErrCode
xp3DecimalMod(const xp3Decimal *a, const xp3Decimal *b, xp3Decimal **res);

xp3ErrCode
xp3DecimalRound(const xp3Decimal *a, int precision, xp3RoundingMode mode,
                xp3Decimal **res);

#endif /* XP3_XP3DECIMAL_H */
