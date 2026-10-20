/**
 * @file
 *
 * @brief Operations on atomic values: arithmetic, comparison, casting
 * (internal)
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XP3OPS_H
#define XP3_XP3OPS_H

#include <libxpath3/xpath3.h>
#include "private/xp3ast.h"
#include "private/xp3error.h"
#include "private/xp3types.h"
#include "private/xp3value.h"

int
xp3TypeIsNumeric(xp3TypeId type);

int
xp3TypeIsInteger(xp3TypeId type);

xp3ErrCode
xp3Arithmetic(xp3AstOp op, const xp3Atomic *left, const xp3Atomic *right,
              xp3Atomic **res);

xp3ErrCode
xp3NegateNumber(const xp3Atomic *value, xp3Atomic **res);

xp3ErrCode
xp3CompareAtomics(xp3AstOp op, const xp3Atomic *left, const xp3Atomic *right,
                  xp3LanguageLevel level, int *result);

xp3ErrCode
xp3CastAtomic(const xp3Atomic *value, xp3TypeId target, xp3Atomic **res);

#endif /* XP3_XP3OPS_H */
