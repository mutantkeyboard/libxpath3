/*
 * xp3fnseq.c: the accessor, boolean, error, sequence, comparison and
 * aggregate functions
 *
 * Implementations of built-in functions of F&O 3.1,
 * https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/, bound
 * to their signatures in xp3func.c. The evaluator has applied the
 * function conversion rules to the arguments already, so each gets
 * values of its declared type (xp3func.h).
 *
 * Sequences are shared, not copied, where a result is an argument or
 * a range of integers: fn:subsequence((1 to 1000000000), 5) is a range
 * again, and fn:count, fn:sum, fn:avg, fn:min, fn:max and
 * fn:distinct-values never expand one. Copying items counts a step per
 * item, and results respect the limit on sequence length, as
 * everywhere in the evaluator.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

#include "private/xp3decimal.h"
#include "private/xp3double.h"
#include "private/xp3eval.h"
#include "private/xp3func.h"
#include "private/xp3nodes.h"
#include "private/xp3ops.h"
#include "private/xp3value.h"

/*
 * Helpers
 */

/* An xs:integer for a count or a position, which may exceed int64_t
 * for a long range */
static xp3Value *
newCount(xp3FuncCall *call, uint64_t count) {
    xp3Decimal *dec;
    char buf[32];
    int len;

    if (count <= INT64_MAX)
        return(xp3FuncAtomic(call, xp3AtomicNewInteger((int64_t) count)));
    len = snprintf(buf, sizeof(buf), "%llu", (unsigned long long) count);
    if (xp3DecimalParse(BAD_CAST buf, (size_t) len, 1, &dec) !=
        XP3_ERR_OK) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(NULL);
    }
    return(xp3FuncAtomic(call, xp3AtomicNewDecimal(XP3_TYPE_INTEGER, dec)));
}

/* The only atomic value of an argument converted to a type without an
 * occurrence indicator, with a reference, or NULL after recording an
 * error. A range of one integer allocates it. */
static xp3Atomic *
argAtomic(xp3FuncCall *call, const xp3Value *value) {
    xp3Item item;

    if (xp3ValueItemAt(value, 0, &item) <= 0) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(NULL);
    }
    return(item.u.atomic);
}

/*
 * An xs:integer argument as a position: clamped to the range of
 * int64_t, which is beyond the length of any sequence. Returns 0, or
 * -1 after recording an error.
 */
static int
argPosition(xp3FuncCall *call, const xp3Value *value, int64_t *position) {
    xp3Atomic *atomic = argAtomic(call, value);

    if (atomic == NULL)
        return(-1);
    if (xp3DecimalToInt64(atomic->u.dec, position) < 0)
        *position = (xp3DecimalSign(atomic->u.dec) < 0) ? INT64_MIN :
                                                          INT64_MAX;
    xp3AtomicUnref(atomic);
    return(0);
}

/*
 * Add the items of value from index from up to before index to, in
 * order or reversed, counting a step per item. The length is checked
 * before copying, so a long range fails at once. Returns 0, or -1
 * after recording an error.
 */
static int
addItems(xp3FuncCall *call, xp3SeqBuilder *builder, const xp3Value *value,
         uint64_t from, uint64_t to, int reverse) {
    uint64_t i;

    if (to <= from)
        return(0);
    if (to - from > (uint64_t) (builder->max - builder->count)) {
        xp3FuncError(call, XP3_ERR_LIMIT, "a sequence would have more "
                     "than %lu items", (unsigned long) builder->max);
        return(-1);
    }
    if (xp3FuncSteps(call, to - from) < 0)
        return(-1);
    for (i = 0; i < to - from; i++) {
        xp3Item item;

        if (xp3ValueItemAt(value, reverse ? to - 1 - i : from + i,
                           &item) <= 0) {
            xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
            return(-1);
        }
        if (xp3FuncBuilderAdd(call, builder, item) < 0)
            return(-1);
    }
    return(0);
}

/* A part of a range, from index from to index last (0-based) */
static xp3Value *
subRange(xp3FuncCall *call, const xp3Value *range, uint64_t from,
         uint64_t last) {
    xp3Value *result;

    if (xp3ValueRange((int64_t) ((uint64_t) range->u.range.first + from),
                      (int64_t) ((uint64_t) range->u.range.first + last),
                      &result) != XP3_ERR_OK) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(NULL);
    }
    return(result);
}

/*
 * The items of value from index from up to before index to, then
 * those of inserts, then the items from index rest on. A range stays
 * a range if that is all there is.
 */
static xp3Value *
spliceItems(xp3FuncCall *call, const xp3Value *value, uint64_t from,
            uint64_t to, const xp3Value *inserts, uint64_t rest) {
    uint64_t count = xp3ValueCount(value);
    xp3SeqBuilder builder;

    if ((value->shape == XP3_SEQ_RANGE) && (inserts == NULL)) {
        if (from == to) {
            from = rest;
            to = count;
            rest = count;
        }
        if (rest == count) {
            if (from >= to)
                return(xp3ValueEmpty());
            return(subRange(call, value, from, to - 1));
        }
    }
    xp3FuncBuilderInit(call, &builder);
    if ((addItems(call, &builder, value, from, to, 0) < 0) ||
        ((inserts != NULL) &&
         (addItems(call, &builder, inserts, 0, xp3ValueCount(inserts),
                   0) < 0)) ||
        (addItems(call, &builder, value, rest, count, 0) < 0)) {
        xp3SeqBuilderClear(&builder);
        return(NULL);
    }
    return(xp3FuncBuilderFinish(call, &builder));
}

/* Whether values of a type are durations, dates or times, which come
 * with Part 10 */
static int
isTemporal(xp3TypeId type) {
    xp3Representation rep = xp3TypeRepresentation(type);

    return((rep == XP3_REP_DURATION) || (rep == XP3_REP_DATE_TIME));
}

/* The context item as a sequence, for the functions whose argument
 * defaults to it */
static xp3Value *
contextValue(xp3FuncCall *call) {
    xp3Value *value;
    xp3Item item;

    if (xp3FuncFocus(call, &item, NULL, NULL) < 0)
        return(NULL);
    value = xp3ValueSingle(xp3ItemRef(item));
    if (value == NULL)
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
    return(value);
}

/*
 * Accessors
 */

/**
 * fn:string#0 and #1 (F&O 3.1, 2.3): the string value of a node, an
 * atomic value cast to xs:string, or "" for the empty sequence. The
 * argument of fn:string#0 is the context item.
 *
 * @param call   the call
 * @param args   the argument, item()?
 * @param count  0 or 1
 * @returns an xs:string, or NULL on error
 */
xp3Value *
xp3FnString(xp3FuncCall *call, xp3Value **args, int count) {
    xmlChar *str = NULL;
    xp3Atomic *atomic;
    xp3ErrCode err;
    xp3Item item;
    size_t length = 0;

    if (count == 0) {
        if (xp3FuncFocus(call, &item, NULL, NULL) < 0)
            return(NULL);
        item = xp3ItemRef(item);
    } else if (xp3ValueCount(args[0]) == 0) {
        return(xp3FuncAtomic(call, xp3AtomicNewString(XP3_TYPE_STRING,
                                                      BAD_CAST "", 0)));
    } else if (xp3ValueItemAt(args[0], 0, &item) <= 0) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(NULL);
    }

    switch (item.kind) {
        case XP3_ITEM_NODE:
            err = xp3NodeStringValue(item.u.node, &str, &length);
            if (err == XP3_ERR_LIMIT)
                xp3FuncError(call, err, "the string value of a node is "
                             "longer than 2 GB");
            else if (err != XP3_ERR_OK)
                xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
            break;
        case XP3_ITEM_ATOMIC:
            str = xp3AtomicToString(item.u.atomic);
            if (str == NULL)
                xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
            else
                length = strlen((const char *) str);
            break;
        default:
            xp3FuncError(call, XP3_ERR_FOTY0014, "a function item has no "
                         "string value");
            break;
    }
    xp3ItemUnref(item);
    if (str == NULL)
        return(NULL);
    atomic = xp3AtomicNewString(XP3_TYPE_STRING, str, length);
    xmlFree(str);
    return(xp3FuncAtomic(call, atomic));
}

/**
 * fn:data#0 and #1 (F&O 3.1, 2.4): the argument atomized, or the
 * context item for fn:data#0. Function items other than arrays raise
 * FOTY0013.
 *
 * @param call   the call
 * @param args   the argument, item()*
 * @param count  0 or 1
 * @returns the atomic values, or NULL on error
 */
xp3Value *
xp3FnData(xp3FuncCall *call, xp3Value **args, int count) {
    xp3Value *value = (count == 0) ? contextValue(call) :
                                     xp3ValueRef(args[0]);

    if (value == NULL)
        return(NULL);
    return(xp3FuncAtomize(call, value));
}

/*
 * Errors
 */

/**
 * fn:error#0 to #3 (F&O 3.1, 3.1.1): raise an error with the code
 * given, err:FOER0000 if there is none, and the description as its
 * message. The error object of fn:error#3 is ignored: the error API
 * has no place for it, and nothing in XPath can catch the error to
 * read it.
 *
 * @param call   the call
 * @param args   the code (xs:QName?), the description (xs:string) and
 *               the error object (item()*)
 * @param count  0 to 3
 * @returns NULL, always
 */
xp3Value *
xp3FnError(xp3FuncCall *call, xp3Value **args, int count) {
    xp3Atomic *code = NULL;
    xp3Atomic *description = NULL;

    if ((count >= 1) && (xp3ValueCount(args[0]) > 0)) {
        code = argAtomic(call, args[0]);
        if (code == NULL)
            return(NULL);
    }
    if (count >= 2) {
        description = argAtomic(call, args[1]);
        if (description == NULL) {
            xp3AtomicUnref(code);
            return(NULL);
        }
    }
    xp3FuncUserError(call, (code != NULL) ? code->u.qname.uri : NULL,
                     (code != NULL) ? code->u.qname.local : NULL,
                     (description != NULL) ? description->u.str :
                     BAD_CAST "fn:error was called");
    xp3AtomicUnref(code);
    xp3AtomicUnref(description);
    return(NULL);
}

/*
 * Numbers
 */

/**
 * fn:number#0 and #1 (F&O 3.1, 4.5.1): the argument cast to xs:double,
 * or NaN if it is empty or the cast fails. The argument of
 * fn:number#0 is the context item, atomized.
 *
 * @param call   the call
 * @param args   the argument, xs:anyAtomicType?
 * @param count  0 or 1
 * @returns an xs:double, or NULL on error
 */
xp3Value *
xp3FnNumber(xp3FuncCall *call, xp3Value **args, int count) {
    xp3Value *value;
    xp3Atomic *atomic = NULL;
    xp3Atomic *result = NULL;
    xp3ErrCode err;

    if (count == 0) {
        value = contextValue(call);
        if (value != NULL)
            value = xp3FuncAtomize(call, value);
        if (value == NULL)
            return(NULL);
        /* Without a schema, a node has one typed value */
    } else {
        value = xp3ValueRef(args[0]);
    }
    if (xp3ValueCount(value) == 0) {
        xp3ValueUnref(value);
        return(xp3FuncAtomic(call, xp3AtomicNewDouble(xp3DoubleNaN())));
    }
    atomic = argAtomic(call, value);
    xp3ValueUnref(value);
    if (atomic == NULL)
        return(NULL);

    err = xp3CastAtomic(atomic, XP3_TYPE_DOUBLE, &result);
    xp3AtomicUnref(atomic);
    if (err == XP3_ERR_NO_MEMORY) {
        xp3FuncError(call, err, NULL);
        return(NULL);
    }
    if (err != XP3_ERR_OK)
        return(xp3FuncAtomic(call, xp3AtomicNewDouble(xp3DoubleNaN())));
    return(xp3FuncAtomic(call, result));
}

/*
 * Booleans
 */

/**
 * fn:true#0 (F&O 3.1, 7.1.1).
 *
 * @param call   the call
 * @param args   none
 * @param count  0
 * @returns true, or NULL on error
 */
xp3Value *
xp3FnTrue(xp3FuncCall *call, xp3Value **args, int count) {
    (void) args;
    (void) count;
    return(xp3FuncAtomic(call, xp3AtomicNewBoolean(1)));
}

/**
 * fn:false#0 (F&O 3.1, 7.1.2).
 *
 * @param call   the call
 * @param args   none
 * @param count  0
 * @returns false, or NULL on error
 */
xp3Value *
xp3FnFalse(xp3FuncCall *call, xp3Value **args, int count) {
    (void) args;
    (void) count;
    return(xp3FuncAtomic(call, xp3AtomicNewBoolean(0)));
}

/* The effective boolean value of an argument, or -1 on error */
static int
effectiveBoolean(xp3FuncCall *call, const xp3Value *value) {
    xp3ErrCode err;
    int result;

    err = xp3EffectiveBoolean(value, &result);
    if (err == XP3_ERR_OK)
        return(result);
    if (err == XP3_ERR_FORG0006)
        xp3FuncError(call, err, "the argument has no effective boolean "
                     "value: it is not empty, does not start with a node, "
                     "and is not a single boolean, string or number");
    else
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
    return(-1);
}

/**
 * fn:boolean#1 (F&O 3.1, 7.3.1): the effective boolean value of the
 * argument (XPath 3.1, 2.4.3).
 *
 * @param call   the call
 * @param args   the argument, item()*
 * @param count  1
 * @returns an xs:boolean, or NULL on error
 */
xp3Value *
xp3FnBoolean(xp3FuncCall *call, xp3Value **args, int count) {
    int value = effectiveBoolean(call, args[0]);

    (void) count;
    if (value < 0)
        return(NULL);
    return(xp3FuncAtomic(call, xp3AtomicNewBoolean(value)));
}

/**
 * fn:not#1 (F&O 3.1, 7.3.2): the negated effective boolean value of
 * the argument.
 *
 * @param call   the call
 * @param args   the argument, item()*
 * @param count  1
 * @returns an xs:boolean, or NULL on error
 */
xp3Value *
xp3FnNot(xp3FuncCall *call, xp3Value **args, int count) {
    int value = effectiveBoolean(call, args[0]);

    (void) count;
    if (value < 0)
        return(NULL);
    return(xp3FuncAtomic(call, xp3AtomicNewBoolean(!value)));
}

/*
 * General functions on sequences
 */

/**
 * fn:empty#1 (F&O 3.1, 14.1.1).
 *
 * @param call   the call
 * @param args   the argument, item()*
 * @param count  1
 * @returns an xs:boolean, or NULL on error
 */
xp3Value *
xp3FnEmpty(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(xp3FuncAtomic(call,
                         xp3AtomicNewBoolean(xp3ValueCount(args[0]) == 0)));
}

/**
 * fn:exists#1 (F&O 3.1, 14.1.2).
 *
 * @param call   the call
 * @param args   the argument, item()*
 * @param count  1
 * @returns an xs:boolean, or NULL on error
 */
xp3Value *
xp3FnExists(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(xp3FuncAtomic(call,
                         xp3AtomicNewBoolean(xp3ValueCount(args[0]) > 0)));
}

/**
 * fn:head#1 (F&O 3.1, 14.1.3): the first item, if any.
 *
 * @param call   the call
 * @param args   the argument, item()*
 * @param count  1
 * @returns the item or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnHead(xp3FuncCall *call, xp3Value **args, int count) {
    xp3Value *value;
    xp3Item item;

    (void) count;
    if (xp3ValueCount(args[0]) <= 1)
        return(xp3ValueRef(args[0]));
    if (xp3ValueItemAt(args[0], 0, &item) <= 0) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(NULL);
    }
    value = xp3ValueSingle(item);
    if (value == NULL)
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
    return(value);
}

/**
 * fn:tail#1 (F&O 3.1, 14.1.4): all items but the first.
 *
 * @param call   the call
 * @param args   the argument, item()*
 * @param count  1
 * @returns the items, or NULL on error
 */
xp3Value *
xp3FnTail(xp3FuncCall *call, xp3Value **args, int count) {
    uint64_t length = xp3ValueCount(args[0]);

    (void) count;
    if (length <= 1)
        return(xp3ValueEmpty());
    return(spliceItems(call, args[0], 0, 0, NULL, 1));
}

/**
 * fn:insert-before#3 (F&O 3.1, 14.1.5): the items of $target with
 * those of $inserts before position $position; at the start for a
 * position below 1, at the end for one past the last item.
 *
 * @param call   the call
 * @param args   $target (item()*), $position (xs:integer) and
 *               $inserts (item()*)
 * @param count  3
 * @returns the items, or NULL on error
 */
xp3Value *
xp3FnInsertBefore(xp3FuncCall *call, xp3Value **args, int count) {
    uint64_t length = xp3ValueCount(args[0]);
    uint64_t before;
    int64_t position;

    (void) count;
    if (argPosition(call, args[1], &position) < 0)
        return(NULL);
    if (xp3ValueCount(args[2]) == 0)
        return(xp3ValueRef(args[0]));
    if (length == 0)
        return(xp3ValueRef(args[2]));
    if (position < 1)
        before = 0;
    else if ((uint64_t) position - 1 > length)
        before = length;
    else
        before = (uint64_t) position - 1;
    return(spliceItems(call, args[0], 0, before, args[2], before));
}

/**
 * fn:remove#2 (F&O 3.1, 14.1.6): the items of $target without the one
 * at $position; all of them if there is no such position.
 *
 * @param call   the call
 * @param args   $target (item()*) and $position (xs:integer)
 * @param count  2
 * @returns the items, or NULL on error
 */
xp3Value *
xp3FnRemove(xp3FuncCall *call, xp3Value **args, int count) {
    uint64_t length = xp3ValueCount(args[0]);
    int64_t position;

    (void) count;
    if (argPosition(call, args[1], &position) < 0)
        return(NULL);
    if ((position < 1) || ((uint64_t) position > length))
        return(xp3ValueRef(args[0]));
    return(spliceItems(call, args[0], 0, (uint64_t) position - 1, NULL,
                       (uint64_t) position));
}

/**
 * fn:reverse#1 (F&O 3.1, 14.1.7): the items in reverse order.
 *
 * @param call   the call
 * @param args   the argument, item()*
 * @param count  1
 * @returns the items, or NULL on error
 */
xp3Value *
xp3FnReverse(xp3FuncCall *call, xp3Value **args, int count) {
    xp3SeqBuilder builder;

    (void) count;
    if (xp3ValueCount(args[0]) <= 1)
        return(xp3ValueRef(args[0]));
    xp3FuncBuilderInit(call, &builder);
    if (addItems(call, &builder, args[0], 0, xp3ValueCount(args[0]),
                 1) < 0) {
        xp3SeqBuilderClear(&builder);
        return(NULL);
    }
    return(xp3FuncBuilderFinish(call, &builder));
}

/* fn:round on a double: to the nearest integer, halves up (F&O 3.1,
 * 4.4.4). floor(x + 0.5) would round 0.49999999999999994 to 1. */
static double
roundHalfUp(double value) {
    double result = floor(value);

    if (value - result >= 0.5)
        result += 1;
    return(result);
}

/* The xs:double of an argument converted to xs:double */
static double
argDouble(const xp3Value *value) {
    /* Converted to one xs:double, which is never a range */
    return(value->u.single.u.atomic->u.d);
}

/**
 * fn:subsequence#2 and #3 (F&O 3.1, 14.1.8): the items at the
 * positions p with round($startingLoc) <= p and, with a length,
 * p < round($startingLoc) + round($length), compared as doubles. So
 * NaN selects nothing, a start of -INF with a length of INF selects
 * nothing either (the sum is NaN), and -INF without a length selects
 * everything. A range gives a range.
 *
 * @param call   the call
 * @param args   $sourceSeq (item()*), $startingLoc (xs:double) and
 *               $length (xs:double)
 * @param count  2 or 3
 * @returns the items, or NULL on error
 */
xp3Value *
xp3FnSubsequence(xp3FuncCall *call, xp3Value **args, int count) {
    uint64_t length = xp3ValueCount(args[0]);
    double start = roundHalfUp(argDouble(args[1]));
    double end = HUGE_VAL;
    uint64_t first;
    uint64_t last;

    if (count == 3)
        end = start + roundHalfUp(argDouble(args[2]));
    /* Each test is false for NaN; a start of 2^64 is past any end */
    if (!(start < end) || !(end > 1) || !(start <= (double) length) ||
        (start >= 18446744073709551616.0) || (length == 0))
        return(xp3ValueEmpty());
    first = (start <= 1) ? 1 : (uint64_t) start;
    if (end - 1 >= (double) length)
        last = length;
    else
        last = (uint64_t) (end - 1);
    /* Doubles round positions past 2^53 */
    if (last > length)
        last = length;
    if (first > last)
        return(xp3ValueEmpty());
    if ((first == 1) && (last == length))
        return(xp3ValueRef(args[0]));
    if (args[0]->shape == XP3_SEQ_RANGE)
        return(subRange(call, args[0], first - 1, last - 1));
    return(spliceItems(call, args[0], first - 1, last, NULL, length));
}

/*
 * Comparing values in sequences
 */

/* Whether values of a type compare as strings: xs:string and the types
 * derived from it, xs:anyURI, and xs:untypedAtomic, which F&O 3.1,
 * 14.2 compares as if it were a string */
static int
isStringType(xp3TypeId type) {
    return(xp3TypeDerivesFrom(type, XP3_TYPE_STRING) ||
           (type == XP3_TYPE_ANY_URI) || (type == XP3_TYPE_UNTYPED_ATOMIC));
}

static int
isNaNValue(const xp3Atomic *atomic) {
    switch (xp3TypeRepresentation(atomic->type)) {
        case XP3_REP_FLOAT:
            return(isnan(atomic->u.f));
        case XP3_REP_DOUBLE:
            return(isnan(atomic->u.d));
        default:
            return(0);
    }
}

/*
 * Whether two atomic values are equal as F&O 3.1, 14.2 compares them:
 * with eq, strings under the collation, and values that eq can't
 * compare are unequal. If nanEqual is set, NaN equals NaN, as for
 * fn:deep-equal and fn:distinct-values; fn:index-of uses eq alone.
 * Returns 1 or 0, or -1 after recording an error.
 */
static int
atomicsEqual(xp3FuncCall *call, const xp3Atomic *left,
             const xp3Atomic *right, xp3Collation collation, int nanEqual) {
    xp3ErrCode err;
    int equal;

    if (isTemporal(left->type) || isTemporal(right->type)) {
        xp3FuncError(call, XP3_ERR_UNSUPPORTED, "comparing dates, times "
                     "and durations is not implemented yet");
        return(-1);
    }
    if (isStringType(left->type) && isStringType(right->type))
        return(xp3CollationCompare(collation, left->u.str,
                                   right->u.str) == 0);
    if (nanEqual && isNaNValue(left) && isNaNValue(right))
        return(1);
    err = xp3CompareAtomics(XP3_OP_VAL_EQ, left, right, xp3FuncLevel(call),
                            &equal);
    if (err == XP3_ERR_OK)
        return(equal);
    if (err == XP3_ERR_XPTY0004)
        return(0);
    xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
    return(-1);
}

/* The collation of a function whose last parameter is the optional
 * collation, after arity others */
static int
callCollation(xp3FuncCall *call, xp3Value **args, int count, int arity,
              xp3Collation *collation) {
    return(xp3FuncCollation(call, (count > arity) ? args[arity] : NULL,
                            collation));
}

/*
 * fn:distinct-values keeps a hash table of the values kept so far, so
 * that each value costs constant expected time, whatever the input.
 *
 * Strings, binary values, QNames and booleans are keyed by a hash of
 * all that their equality looks at, strings folded as the collation
 * compares them, so distinct values share a key only when the hash
 * collides.
 *
 * Numbers need more, because eq promotes them (xp3CompareAtomics()):
 * two decimals compare exactly, a decimal and a double as doubles, a
 * decimal and a float as floats, the decimal rounded once to either,
 * and a float and a double as doubles, which is exact for the float.
 * Equal numbers so need not share any one rounded value, and many
 * distinct decimals share their nearest double. Kept decimals are in
 * the table by their exact value, and each double value has an entry
 * that marks which kept numbers equal it: a double or a float of that
 * value, a decimal whose nearest double it is, or a decimal whose
 * nearest float it is. A new number takes at most three lookups to
 * know whether a kept number is equal to it, with eq's own answer, so
 * the result is the same as comparing it with each kept value in turn,
 * also where eq is not transitive.
 */

/* Kinds of entries; entries of different kinds never match */
typedef enum {
    XP3_HASH_NONE = 0,          /* equal to nothing: not in the table */
    XP3_HASH_STRING,
    XP3_HASH_BOOLEAN,
    XP3_HASH_HEX_BINARY,
    XP3_HASH_BASE64_BINARY,
    XP3_HASH_QNAME,
    XP3_HASH_DECIMAL,           /* a kept decimal, by its exact value */
    XP3_HASH_DOUBLE             /* a double value, with marks */
} xp3HashClass;

/* Marks of an XP3_HASH_DOUBLE entry: the kept numbers equal to it */
#define XP3_MARK_DOUBLE (1 << 0)    /* an xs:double of that value */
#define XP3_MARK_FLOAT (1 << 1)     /* an xs:float of that value */
#define XP3_MARK_BY_DOUBLE (1 << 2) /* a decimal whose double it is */
#define XP3_MARK_BY_FLOAT (1 << 3)  /* a decimal whose float it is */

/* Integers up to this size convert to xs:float exactly */
#define XP3_FLOAT_EXACT 16777216

typedef struct {
    uint32_t cls;
    uint32_t marks;
    uint64_t key;
    size_t item;                /* the kept value, by its index in the
                                 * result */
    size_t next;                /* the next entry of the bucket + 1, 0
                                 * at the end */
} xp3HashEntry;

typedef struct {
    xp3HashEntry *entries;
    size_t count;
    size_t cap;
    size_t *heads;
    size_t mask;
} xp3HashTable;

/* A number as eq promotes it */
typedef struct {
    double asDouble;
    double asFloat;             /* decimals only: the float, widened */
    uint64_t exact;             /* decimals only: a hash of the value */
} xp3NumberKeys;

/* FNV-1a over bytes, folded as the collation compares them */
static uint64_t
hashBytes(uint64_t hash, const unsigned char *data, size_t len,
          xp3Collation collation) {
    size_t i;

    for (i = 0; i < len; i++) {
        unsigned char c = data[i];

        if ((collation == XP3_COLLATION_HTML_ASCII) && (c >= 'A') &&
            (c <= 'Z'))
            c = (unsigned char) (c - 'A' + 'a');
        hash = (hash ^ c) * 0x100000001B3ULL;
    }
    return(hash);
}

#define XP3_FNV_BASIS 0xCBF29CE484222325ULL

static uint64_t
hashString(uint64_t hash, const xmlChar *str, xp3Collation collation) {
    return(hashBytes(hash, str, strlen((const char *) str), collation));
}

/*
 * The key of a value that isn't a number, consistent with
 * atomicsEqual() with NaN equal: equal values get the same key. Sets
 * *cls to XP3_HASH_NONE for values that equal nothing. Returns 0, or
 * -1 after recording an error.
 */
static int
hashKey(xp3FuncCall *call, const xp3Atomic *atomic, xp3Collation collation,
        xp3HashClass *cls, uint64_t *key) {
    *cls = XP3_HASH_NONE;
    *key = 0;
    if (isTemporal(atomic->type)) {
        xp3FuncError(call, XP3_ERR_UNSUPPORTED, "comparing dates, times "
                     "and durations is not implemented yet");
        return(-1);
    }
    if (isStringType(atomic->type)) {
        *cls = XP3_HASH_STRING;
        *key = hashString(XP3_FNV_BASIS, atomic->u.str, collation);
        return(0);
    }
    switch (xp3TypeRepresentation(atomic->type)) {
        case XP3_REP_BOOLEAN:
            *cls = XP3_HASH_BOOLEAN;
            *key = (uint64_t) (atomic->u.boolean != 0);
            break;
        case XP3_REP_BINARY:
            *cls = (atomic->type == XP3_TYPE_HEX_BINARY) ?
                   XP3_HASH_HEX_BINARY : XP3_HASH_BASE64_BINARY;
            *key = hashBytes(XP3_FNV_BASIS, atomic->u.bin.data,
                             atomic->u.bin.len, XP3_COLLATION_CODEPOINT);
            break;
        case XP3_REP_QNAME:
            /* xs:NOTATION has no eq and stays XP3_HASH_NONE */
            if (atomic->type != XP3_TYPE_QNAME)
                break;
            *cls = XP3_HASH_QNAME;
            *key = hashString(XP3_FNV_BASIS,
                              (atomic->u.qname.uri != NULL) ?
                              atomic->u.qname.uri : BAD_CAST "",
                              XP3_COLLATION_CODEPOINT);
            /* A byte no URI has between the parts */
            *key = hashBytes(*key, (const unsigned char *) "", 1,
                             XP3_COLLATION_CODEPOINT);
            *key = hashString(*key, atomic->u.qname.local,
                              XP3_COLLATION_CODEPOINT);
            break;
        default:
            break;
    }
    return(0);
}

/*
 * A number as eq promotes it: a decimal rounded once to a double and
 * once to a float, through its string as xp3CompareAtomics() rounds
 * it, and hashed by its exact value. Small integers convert exactly
 * and skip the string. Returns 0, or -1 after recording NO_MEMORY.
 */
static int
numberKeys(xp3FuncCall *call, const xp3Atomic *atomic,
           xp3NumberKeys *keys) {
    xmlChar *str;
    int64_t whole;
    float single;
    int ret;

    keys->asFloat = 0;
    keys->exact = 0;
    switch (xp3TypeRepresentation(atomic->type)) {
        case XP3_REP_FLOAT:
            keys->asDouble = atomic->u.f;
            return(0);
        case XP3_REP_DOUBLE:
            keys->asDouble = atomic->u.d;
            return(0);
        default:
            break;
    }
    if ((xp3DecimalToInt64(atomic->u.dec, &whole) == 0) &&
        (whole >= -XP3_FLOAT_EXACT) && (whole <= XP3_FLOAT_EXACT)) {
        keys->asDouble = (double) whole;
        keys->asFloat = (double) whole;
        keys->exact = (uint64_t) whole;
        return(0);
    }
    str = xp3DecimalToString(atomic->u.dec);
    ret = -1;
    if ((str != NULL) &&
        (xp3DoubleParse((const char *) str, &keys->asDouble) == 0) &&
        (xp3FloatParse((const char *) str, &single) == 0)) {
        keys->asFloat = single;
        keys->exact = hashString(XP3_FNV_BASIS, str,
                                 XP3_COLLATION_CODEPOINT);
        ret = 0;
    }
    xmlFree(str);
    if (ret < 0)
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
    return(ret);
}

/* The key of a double value: its bits, with -0 as 0 and one NaN */
static uint64_t
doubleKey(double value) {
    uint64_t bits;

    if (isnan(value))
        return(0x7FF8000000000000ULL);
    if (value == 0)
        return(0);
    memcpy(&bits, &value, sizeof(bits));
    return(bits);
}

/* The bucket of a key */
static size_t
hashBucket(const xp3HashTable *table, xp3HashClass cls, uint64_t key) {
    uint64_t x = key ^ ((uint64_t) cls * 0x9E3779B97F4A7C15ULL);

    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return((size_t) x & table->mask);
}

/* The first entry with a class and key after the entry cursor - 1, or
 * the first one if cursor is 0; returns its index + 1, or 0 */
static size_t
hashFind(const xp3HashTable *table, xp3HashClass cls, uint64_t key,
         size_t cursor) {
    size_t next = (cursor == 0) ?
                  table->heads[hashBucket(table, cls, key)] :
                  table->entries[cursor - 1].next;

    while ((next != 0) && ((table->entries[next - 1].cls != cls) ||
                           (table->entries[next - 1].key != key)))
        next = table->entries[next - 1].next;
    return(next);
}

/* Add an entry. Returns 0, or -1 after recording NO_MEMORY. */
static int
hashAdd(xp3FuncCall *call, xp3HashTable *table, xp3HashClass cls,
        uint64_t key, size_t item, uint32_t marks) {
    size_t bucket = hashBucket(table, cls, key);
    xp3HashEntry *entry;

    if (table->count >= table->cap) {
        size_t cap = (table->cap > 0) ? table->cap * 2 : 16;
        xp3HashEntry *entries = NULL;

        if (cap <= SIZE_MAX / sizeof(xp3HashEntry))
            entries = xmlRealloc(table->entries,
                                 cap * sizeof(xp3HashEntry));
        if (entries == NULL) {
            xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
            return(-1);
        }
        table->entries = entries;
        table->cap = cap;
    }
    entry = &table->entries[table->count++];
    entry->cls = cls;
    entry->marks = marks;
    entry->key = key;
    entry->item = item;
    entry->next = table->heads[bucket];
    table->heads[bucket] = table->count;
    return(0);
}

/* Whether the entry of a double value has one of the marks */
static int
hasMark(const xp3HashTable *table, double value, uint32_t marks) {
    size_t found = hashFind(table, XP3_HASH_DOUBLE, doubleKey(value), 0);

    return((found != 0) && (table->entries[found - 1].marks & marks));
}

/* Mark the entry of a double value. Returns 0, or -1 after recording
 * NO_MEMORY. */
static int
addMark(xp3FuncCall *call, xp3HashTable *table, double value,
        uint32_t mark) {
    uint64_t key = doubleKey(value);
    size_t found = hashFind(table, XP3_HASH_DOUBLE, key, 0);

    if (found != 0) {
        table->entries[found - 1].marks |= mark;
        return(0);
    }
    return(hashAdd(call, table, XP3_HASH_DOUBLE, key, 0, mark));
}

/* Whether a kept number is equal to a number under eq */
static int
numberKept(const xp3HashTable *table, const xp3SeqBuilder *kept,
           const xp3Atomic *atomic, const xp3NumberKeys *keys) {
    size_t found = 0;

    switch (xp3TypeRepresentation(atomic->type)) {
        case XP3_REP_FLOAT:
            return(hasMark(table, keys->asDouble, XP3_MARK_DOUBLE |
                           XP3_MARK_FLOAT | XP3_MARK_BY_FLOAT));
        case XP3_REP_DOUBLE:
            return(hasMark(table, keys->asDouble, XP3_MARK_DOUBLE |
                           XP3_MARK_FLOAT | XP3_MARK_BY_DOUBLE));
        default:
            break;
    }
    while ((found = hashFind(table, XP3_HASH_DECIMAL, keys->exact,
                             found)) != 0) {
        const xp3Atomic *other =
            kept->items[table->entries[found - 1].item].u.atomic;

        if (xp3DecimalCompare(other->u.dec, atomic->u.dec) == 0)
            return(1);
    }
    return(hasMark(table, keys->asDouble, XP3_MARK_DOUBLE) ||
           hasMark(table, keys->asFloat, XP3_MARK_FLOAT));
}

/* Enter a kept number. Returns 0, or -1 after recording NO_MEMORY. */
static int
keepNumber(xp3FuncCall *call, xp3HashTable *table, const xp3Atomic *atomic,
           const xp3NumberKeys *keys, size_t item) {
    switch (xp3TypeRepresentation(atomic->type)) {
        case XP3_REP_FLOAT:
            return(addMark(call, table, keys->asDouble, XP3_MARK_FLOAT));
        case XP3_REP_DOUBLE:
            return(addMark(call, table, keys->asDouble, XP3_MARK_DOUBLE));
        default:
            break;
    }
    if ((hashAdd(call, table, XP3_HASH_DECIMAL, keys->exact, item,
                 0) < 0) ||
        (addMark(call, table, keys->asDouble, XP3_MARK_BY_DOUBLE) < 0) ||
        (addMark(call, table, keys->asFloat, XP3_MARK_BY_FLOAT) < 0))
        return(-1);
    return(0);
}

/*
 * Look a value up among those kept, and if none is equal to it, keep
 * it. Takes over the item's reference. Returns 0, or -1 after
 * recording an error.
 */
static int
distinctValue(xp3FuncCall *call, xp3HashTable *table, xp3SeqBuilder *kept,
              xp3Item item, xp3Collation collation) {
    const xp3Atomic *atomic = item.u.atomic;
    size_t index = kept->count;
    xp3NumberKeys keys;
    xp3HashClass cls;
    uint64_t key;
    size_t found = 0;

    if (xp3TypeIsNumeric(atomic->type)) {
        if (numberKeys(call, atomic, &keys) < 0)
            goto error;
        if (numberKept(table, kept, atomic, &keys)) {
            xp3ItemUnref(item);
            return(0);
        }
        if (xp3FuncBuilderAdd(call, kept, item) < 0)
            return(-1);
        return(keepNumber(call, table, atomic, &keys, index));
    }

    if (hashKey(call, atomic, collation, &cls, &key) < 0)
        goto error;
    while ((cls != XP3_HASH_NONE) &&
           ((found = hashFind(table, cls, key, found)) != 0)) {
        int equal = atomicsEqual(call,
            kept->items[table->entries[found - 1].item].u.atomic,
            atomic, collation, 1);

        if (equal < 0)
            goto error;
        if (equal) {
            xp3ItemUnref(item);
            return(0);
        }
    }
    if (xp3FuncBuilderAdd(call, kept, item) < 0)
        return(-1);
    if (cls == XP3_HASH_NONE)
        return(0);
    return(hashAdd(call, table, cls, key, index, 0));

error:
    xp3ItemUnref(item);
    return(-1);
}

/**
 * fn:distinct-values#1 and #2 (F&O 3.1, 14.2.1): the values without
 * those equal to an earlier one, in the order of the input. Values are
 * equal as fn:deep-equal compares them, so NaN equals NaN, and the
 * collation compares strings. eq is not transitive across numeric
 * types, so of three values where A eq B, B eq C and A ne C, the first
 * is kept and the others not, which the spec allows. A hash table
 * keeps the time linear (see above). A range of integers has no
 * duplicates.
 *
 * @param call   the call
 * @param args   the values (xs:anyAtomicType*) and the collation
 *               (xs:string)
 * @param count  1 or 2
 * @returns the values, or NULL on error
 */
xp3Value *
xp3FnDistinctValues(xp3FuncCall *call, xp3Value **args, int count) {
    uint64_t length = xp3ValueCount(args[0]);
    xp3HashTable table;
    xp3Collation collation;
    xp3SeqBuilder kept;
    size_t buckets = 16;
    xp3Iter iter;
    xp3Item item;
    int found;

    if (callCollation(call, args, count, 1, &collation) < 0)
        return(NULL);
    if ((length <= 1) || (args[0]->shape == XP3_SEQ_RANGE))
        return(xp3ValueRef(args[0]));
    if (xp3FuncSteps(call, length) < 0)
        return(NULL);
    /* Not a range, so the length is that of an array */
    while ((buckets < length) && (buckets <= SIZE_MAX / 2 / sizeof(size_t)))
        buckets *= 2;
    memset(&table, 0, sizeof(table));
    table.mask = buckets - 1;
    table.heads = xmlMalloc(buckets * sizeof(size_t));
    if (table.heads == NULL) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(NULL);
    }
    memset(table.heads, 0, buckets * sizeof(size_t));

    xp3FuncBuilderInit(call, &kept);
    xp3IterInit(&iter, args[0]);
    while ((found = xp3IterNext(&iter, &item)) > 0) {
        if (distinctValue(call, &table, &kept, item, collation) < 0)
            goto error;
    }
    if (found < 0) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        goto error;
    }
    xmlFree(table.entries);
    xmlFree(table.heads);
    return(xp3FuncBuilderFinish(call, &kept));

error:
    xp3SeqBuilderClear(&kept);
    xmlFree(table.entries);
    xmlFree(table.heads);
    return(NULL);
}

/**
 * fn:index-of#2 and #3 (F&O 3.1, 14.2.2): the positions, from 1, of
 * the values equal to the search value under eq, with strings compared
 * under the collation. Values that eq can't compare with it are
 * skipped, and NaN is found nowhere.
 *
 * @param call   the call
 * @param args   the values (xs:anyAtomicType*), the search value
 *               (xs:anyAtomicType) and the collation (xs:string)
 * @param count  2 or 3
 * @returns the xs:integer positions, or NULL on error
 */
xp3Value *
xp3FnIndexOf(xp3FuncCall *call, xp3Value **args, int count) {
    xp3Collation collation;
    xp3SeqBuilder builder;
    xp3Atomic *search;
    xp3Iter iter;
    xp3Item item;
    uint64_t position = 0;
    int found;

    if (callCollation(call, args, count, 2, &collation) < 0)
        return(NULL);
    search = argAtomic(call, args[1]);
    if (search == NULL)
        return(NULL);
    if (xp3FuncSteps(call, xp3ValueCount(args[0])) < 0) {
        xp3AtomicUnref(search);
        return(NULL);
    }
    xp3FuncBuilderInit(call, &builder);
    xp3IterInit(&iter, args[0]);
    while ((found = xp3IterNext(&iter, &item)) > 0) {
        int equal = atomicsEqual(call, item.u.atomic, search, collation, 0);
        xp3Atomic *atomic;

        xp3ItemUnref(item);
        position++;
        if (equal < 0)
            goto error;
        if (!equal)
            continue;
        /* A position beyond int64_t would take 2^63 steps to reach */
        atomic = xp3AtomicNewInteger((int64_t) position);
        if (atomic == NULL) {
            xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
            goto error;
        }
        if (xp3FuncBuilderAdd(call, &builder,
                              xp3ItemFromAtomic(atomic)) < 0)
            goto error;
    }
    if (found < 0) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        goto error;
    }
    xp3AtomicUnref(search);
    return(xp3FuncBuilderFinish(call, &builder));

error:
    xp3SeqBuilderClear(&builder);
    xp3AtomicUnref(search);
    return(NULL);
}

/* Whether two nodes have the same name: namespace URI and local name */
static int
sameName(const xmlNode *node1, const xmlNode *node2) {
    const xmlChar *uri1 = xp3NodeNamespaceUri(node1);
    const xmlChar *uri2 = xp3NodeNamespaceUri(node2);

    return(xmlStrEqual((uri1 != NULL) ? uri1 : BAD_CAST "",
                       (uri2 != NULL) ? uri2 : BAD_CAST "") &&
           xmlStrEqual(xp3NodeLocalName(node1), xp3NodeLocalName(node2)));
}

/* Whether the string values of two nodes are equal under the
 * collation. Returns 1 or 0, or -1 after recording an error. */
static int
sameStringValue(xp3FuncCall *call, xmlNodePtr node1, xmlNodePtr node2,
                xp3Collation collation) {
    xmlChar *value1 = NULL;
    xmlChar *value2 = NULL;
    size_t length;
    xp3ErrCode err;
    int ret = -1;

    err = xp3NodeStringValue(node1, &value1, &length);
    if (err == XP3_ERR_OK)
        err = xp3NodeStringValue(node2, &value2, &length);
    if (err == XP3_ERR_LIMIT)
        xp3FuncError(call, err, "the string value of a node is longer "
                     "than 2 GB");
    else if (err != XP3_ERR_OK)
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
    else
        ret = (xp3CollationCompare(collation, value1, value2) == 0);
    xmlFree(value1);
    xmlFree(value2);
    return(ret);
}

/* Order attributes by namespace URI, then local name */
static int
compareAttributeNames(const void *a, const void *b) {
    const xmlNode *attr1 = *(const xmlNode *const *) a;
    const xmlNode *attr2 = *(const xmlNode *const *) b;
    const xmlChar *uri1 = xp3NodeNamespaceUri(attr1);
    const xmlChar *uri2 = xp3NodeNamespaceUri(attr2);
    int cmp = xmlStrcmp((uri1 != NULL) ? uri1 : BAD_CAST "",
                        (uri2 != NULL) ? uri2 : BAD_CAST "");

    if (cmp != 0)
        return(cmp);
    return(xmlStrcmp(xp3NodeLocalName(attr1), xp3NodeLocalName(attr2)));
}

/* The attributes of an element, in an array to be freed, or NULL after
 * recording NO_MEMORY */
static xmlNodePtr *
attributeList(xp3FuncCall *call, xmlNodePtr element, size_t count) {
    xmlNodePtr *list = NULL;
    xmlNodePtr attr = NULL;
    size_t i = 0;

    if (count <= SIZE_MAX / sizeof(xmlNodePtr))
        list = xmlMalloc(count * sizeof(xmlNodePtr));
    if (list == NULL) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(NULL);
    }
    while ((attr = xp3AxisNext(XP3_AXIS_ATTRIBUTE, element, attr)) != NULL)
        list[i++] = attr;
    qsort(list, count, sizeof(xmlNodePtr), compareAttributeNames);
    return(list);
}

/* Elements with up to this many attributes are matched pairwise; more
 * are sorted by name first, so that matching takes n log n time */
#define XP3_FEW_ATTRIBUTES 8

/*
 * Whether two elements have the same attributes: as many, and for each
 * one of the other with the same name and value. Returns 1 or 0, or -1
 * after recording an error.
 */
static int
sameAttributes(xp3FuncCall *call, xmlNodePtr element1, xmlNodePtr element2,
               xp3Collation collation) {
    xmlNodePtr *list1;
    xmlNodePtr *list2;
    xmlNodePtr attr = NULL;
    size_t count1 = 0;
    size_t count2 = 0;
    size_t i;
    int ret = 1;

    while ((attr = xp3AxisNext(XP3_AXIS_ATTRIBUTE, element1, attr)) != NULL)
        count1++;
    while ((attr = xp3AxisNext(XP3_AXIS_ATTRIBUTE, element2, attr)) != NULL)
        count2++;
    if (count1 != count2)
        return(0);
    if (count1 <= XP3_FEW_ATTRIBUTES) {
        while ((ret > 0) &&
               ((attr = xp3AxisNext(XP3_AXIS_ATTRIBUTE, element1,
                                    attr)) != NULL)) {
            xmlNodePtr other = NULL;

            while (((other = xp3AxisNext(XP3_AXIS_ATTRIBUTE, element2,
                                         other)) != NULL) &&
                   !sameName(attr, other))
                ;
            ret = (other != NULL) ?
                  sameStringValue(call, attr, other, collation) : 0;
        }
        return(ret);
    }
    if (xp3FuncSteps(call, count1) < 0)
        return(-1);
    list1 = attributeList(call, element1, count1);
    if (list1 == NULL)
        return(-1);
    list2 = attributeList(call, element2, count2);
    if (list2 == NULL) {
        xmlFree(list1);
        return(-1);
    }
    for (i = 0; (ret > 0) && (i < count1); i++) {
        ret = sameName(list1[i], list2[i]) ?
              sameStringValue(call, list1[i], list2[i], collation) : 0;
    }
    xmlFree(list1);
    xmlFree(list2);
    return(ret);
}

/*
 * Whether two nodes are equal apart from their children: of the same
 * kind, elements with the same name and attributes, attributes and
 * processing instructions with the same name and value, and text and
 * comments with the same value. Returns 1 or 0, or -1 after recording
 * an error.
 */
static int
shallowEqual(xp3FuncCall *call, xmlNodePtr node1, xmlNodePtr node2,
             xp3Collation collation) {
    xp3NodeKind kind = xp3NodeGetKind(node1);

    if (kind != xp3NodeGetKind(node2))
        return(0);
    switch (kind) {
        case XP3_NODE_DOCUMENT:
            return(1);
        case XP3_NODE_ELEMENT:
            if (!sameName(node1, node2))
                return(0);
            return(sameAttributes(call, node1, node2, collation));
        case XP3_NODE_ATTRIBUTE:
        case XP3_NODE_PI:
            if (!sameName(node1, node2))
                return(0);
            return(sameStringValue(call, node1, node2, collation));
        default:
            return(sameStringValue(call, node1, node2, collation));
    }
}

/* The child of a node after cur (NULL for the first) that deep
 * equality looks at: elements and text, not comments and processing
 * instructions */
static xmlNodePtr
contentChild(xmlNodePtr parent, xmlNodePtr cur) {
    while ((cur = xp3AxisNext(XP3_AXIS_CHILD, parent, cur)) != NULL) {
        xp3NodeKind kind = xp3NodeGetKind(cur);

        if ((kind == XP3_NODE_ELEMENT) || (kind == XP3_NODE_TEXT))
            return(cur);
    }
    return(NULL);
}

/*
 * Whether two nodes are deep-equal (F&O 3.1, 14.2.3). Without a schema
 * every element is untyped, which counts as mixed content, so elements
 * and documents compare their element and text children in order.
 * Both trees are walked side by side through the parent pointers, not
 * by recursion, so a tree of any depth needs no stack. Strings, text
 * and attribute values included, compare under the collation; names
 * compare by namespace URI and local name. Returns 1 or 0, or -1 after
 * recording an error.
 */
static int
nodesDeepEqual(xp3FuncCall *call, xmlNodePtr top1, xmlNodePtr top2,
               xp3Collation collation) {
    xmlNodePtr parent1 = top1;
    xmlNodePtr parent2 = top2;
    xmlNodePtr child1;
    xmlNodePtr child2;
    xp3NodeKind kind;
    int ret;

    ret = shallowEqual(call, top1, top2, collation);
    kind = xp3NodeGetKind(top1);
    if ((ret <= 0) ||
        ((kind != XP3_NODE_ELEMENT) && (kind != XP3_NODE_DOCUMENT)))
        return(ret);

    child1 = contentChild(parent1, NULL);
    child2 = contentChild(parent2, NULL);
    while (1) {
        if ((child1 == NULL) || (child2 == NULL)) {
            if (child1 != child2)
                return(0);
            /* Both parents are done: on with their next siblings */
            if (parent1 == top1)
                return(1);
            child1 = contentChild(xp3NodeParent(parent1), parent1);
            child2 = contentChild(xp3NodeParent(parent2), parent2);
            parent1 = xp3NodeParent(parent1);
            parent2 = xp3NodeParent(parent2);
            continue;
        }
        if (xp3FuncSteps(call, 1) < 0)
            return(-1);
        ret = shallowEqual(call, child1, child2, collation);
        if (ret <= 0)
            return(ret);
        if (xp3NodeGetKind(child1) == XP3_NODE_ELEMENT) {
            parent1 = child1;
            parent2 = child2;
            child1 = contentChild(parent1, NULL);
            child2 = contentChild(parent2, NULL);
        } else {
            child1 = contentChild(parent1, child1);
            child2 = contentChild(parent2, child2);
        }
    }
}

/**
 * fn:deep-equal#2 and #3 (F&O 3.1, 14.2.3): whether two sequences have
 * the same length and deep-equal items at each position: atomic values
 * that are equal, NaN equal to NaN, and nodes as nodesDeepEqual()
 * compares them. An atomic value is never equal to a node. Function
 * items, maps and arrays come with Part 12; until then comparing one
 * raises UNSUPPORTED, where FOTY0015 or the comparison of maps and
 * arrays belongs.
 *
 * @param call   the call
 * @param args   the two sequences (item()*) and the collation
 *               (xs:string)
 * @param count  2 or 3
 * @returns an xs:boolean, or NULL on error
 */
xp3Value *
xp3FnDeepEqual(xp3FuncCall *call, xp3Value **args, int count) {
    uint64_t length = xp3ValueCount(args[0]);
    xp3Collation collation;
    uint64_t i;
    int equal = 1;

    if (callCollation(call, args, count, 2, &collation) < 0)
        return(NULL);
    if (length != xp3ValueCount(args[1]))
        return(xp3FuncAtomic(call, xp3AtomicNewBoolean(0)));
    if (xp3FuncSteps(call, length) < 0)
        return(NULL);
    for (i = 0; (equal > 0) && (i < length); i++) {
        xp3Item item1;
        xp3Item item2;

        if (xp3ValueItemAt(args[0], i, &item1) <= 0) {
            xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
            return(NULL);
        }
        if (xp3ValueItemAt(args[1], i, &item2) <= 0) {
            xp3ItemUnref(item1);
            xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
            return(NULL);
        }
        if ((item1.kind != XP3_ITEM_NODE) &&
            (item1.kind != XP3_ITEM_ATOMIC)) {
            xp3FuncError(call, XP3_ERR_UNSUPPORTED, "fn:deep-equal of "
                         "function items, maps and arrays is not "
                         "implemented yet");
            equal = -1;
        } else if ((item2.kind != XP3_ITEM_NODE) &&
                   (item2.kind != XP3_ITEM_ATOMIC)) {
            xp3FuncError(call, XP3_ERR_UNSUPPORTED, "fn:deep-equal of "
                         "function items, maps and arrays is not "
                         "implemented yet");
            equal = -1;
        } else if (item1.kind != item2.kind) {
            equal = 0;
        } else if (item1.kind == XP3_ITEM_ATOMIC) {
            equal = atomicsEqual(call, item1.u.atomic, item2.u.atomic,
                                 collation, 1);
        } else {
            equal = nodesDeepEqual(call, item1.u.node, item2.u.node,
                                   collation);
        }
        xp3ItemUnref(item1);
        xp3ItemUnref(item2);
    }
    if (equal < 0)
        return(NULL);
    return(xp3FuncAtomic(call, xp3AtomicNewBoolean(equal)));
}

/*
 * Cardinality
 */

/**
 * fn:zero-or-one#1 (F&O 3.1, 14.3.1): the argument, if it has at most
 * one item [err:FORG0003].
 *
 * @param call   the call
 * @param args   the argument, item()*
 * @param count  1
 * @returns the argument, or NULL on error
 */
xp3Value *
xp3FnZeroOrOne(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    if (xp3ValueCount(args[0]) > 1) {
        xp3FuncError(call, XP3_ERR_FORG0003, "fn:zero-or-one got a "
                     "sequence of more than one item");
        return(NULL);
    }
    return(xp3ValueRef(args[0]));
}

/**
 * fn:one-or-more#1 (F&O 3.1, 14.3.2): the argument, if it is not
 * empty [err:FORG0004].
 *
 * @param call   the call
 * @param args   the argument, item()*
 * @param count  1
 * @returns the argument, or NULL on error
 */
xp3Value *
xp3FnOneOrMore(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    if (xp3ValueCount(args[0]) == 0) {
        xp3FuncError(call, XP3_ERR_FORG0004, "fn:one-or-more got the "
                     "empty sequence");
        return(NULL);
    }
    return(xp3ValueRef(args[0]));
}

/**
 * fn:exactly-one#1 (F&O 3.1, 14.3.3): the argument, if it has exactly
 * one item [err:FORG0005].
 *
 * @param call   the call
 * @param args   the argument, item()*
 * @param count  1
 * @returns the argument, or NULL on error
 */
xp3Value *
xp3FnExactlyOne(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    if (xp3ValueCount(args[0]) != 1) {
        xp3FuncError(call, XP3_ERR_FORG0005, "fn:exactly-one got %s",
                     (xp3ValueCount(args[0]) == 0) ? "the empty sequence" :
                     "a sequence of more than one item");
        return(NULL);
    }
    return(xp3ValueRef(args[0]));
}

/**
 * fn:count#1 (F&O 3.1, 14.4.1): the number of items. A range is
 * counted from its bounds.
 *
 * @param call   the call
 * @param args   the argument, item()*
 * @param count  1
 * @returns an xs:integer, or NULL on error
 */
xp3Value *
xp3FnCount(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(newCount(call, xp3ValueCount(args[0])));
}

/*
 * Aggregate functions
 */

/*
 * A value as the aggregate functions take it (F&O 3.1, 14.4), with
 * xs:untypedAtomic cast to xs:double. Takes over the item's reference
 * and returns the value with one, or NULL after recording an error.
 */
static xp3Atomic *
aggregateValue(xp3FuncCall *call, xp3Item item) {
    xp3Atomic *atomic = item.u.atomic;
    xp3Atomic *result = NULL;
    xp3ErrCode err;

    if (atomic->type != XP3_TYPE_UNTYPED_ATOMIC)
        return(atomic);
    err = xp3CastAtomic(atomic, XP3_TYPE_DOUBLE, &result);
    if (err == XP3_ERR_FORG0001)
        xp3FuncError(call, err, "the untyped value \"%.40s\" is not a "
                     "valid xs:double", (const char *) atomic->u.str);
    else if (err != XP3_ERR_OK)
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
    xp3AtomicUnref(atomic);
    return(result);
}

/* Record the error of an operation of an aggregate function on two
 * values */
static void
aggregateError(xp3FuncCall *call, xp3ErrCode err, const char *name,
               const xp3Atomic *left, const xp3Atomic *right) {
    if (err == XP3_ERR_FOAR0002)
        xp3FuncError(call, err, "the result of %s is too large", name);
    else if (err == XP3_ERR_NO_MEMORY)
        xp3FuncError(call, err, NULL);
    else if (left->type == right->type)
        xp3FuncError(call, XP3_ERR_FORG0006, "%s got values of type "
                     "xs:%s, which have no order", name,
                     (const char *) xp3TypeLocalName(left->type));
    else
        xp3FuncError(call, XP3_ERR_FORG0006, "%s got values of the "
                     "incompatible types xs:%s and xs:%s", name,
                     (const char *) xp3TypeLocalName(left->type),
                     (const char *) xp3TypeLocalName(right->type));
}

/* An arithmetic operation of an aggregate function, or NULL after
 * recording an error */
static xp3Atomic *
aggregateArithmetic(xp3FuncCall *call, xp3AstOp op, const xp3Atomic *left,
                    const xp3Atomic *right, const char *name) {
    xp3Atomic *result = NULL;
    xp3ErrCode err = xp3Arithmetic(op, left, right, &result);

    if (err != XP3_ERR_OK)
        aggregateError(call, err, name, left, right);
    return(result);
}

/*
 * The sum of a range first to last, without expanding it:
 * (first + last) * n idiv 2 for n integers, or the average
 * (first + last) div 2, which is an xs:decimal as for any integers.
 */
static xp3Atomic *
rangeSum(xp3FuncCall *call, const xp3Value *range, int average,
         const char *name) {
    xp3Atomic *first = xp3AtomicNewInteger(range->u.range.first);
    xp3Atomic *last = xp3AtomicNewInteger(range->u.range.last);
    xp3Atomic *one = xp3AtomicNewInteger(1);
    xp3Atomic *two = xp3AtomicNewInteger(2);
    xp3Atomic *ends = NULL;
    xp3Atomic *difference = NULL;
    xp3Atomic *length = NULL;
    xp3Atomic *product = NULL;
    xp3Atomic *result = NULL;

    if ((first == NULL) || (last == NULL) || (one == NULL) ||
        (two == NULL)) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        goto done;
    }
    ends = aggregateArithmetic(call, XP3_OP_ADD, first, last, name);
    if (ends == NULL)
        goto done;
    if (average) {
        result = aggregateArithmetic(call, XP3_OP_DIV, ends, two, name);
        goto done;
    }
    difference = aggregateArithmetic(call, XP3_OP_SUB, last, first, name);
    if (difference != NULL)
        length = aggregateArithmetic(call, XP3_OP_ADD, difference, one,
                                     name);
    if (length != NULL)
        product = aggregateArithmetic(call, XP3_OP_MUL, ends, length, name);
    if (product != NULL)
        result = aggregateArithmetic(call, XP3_OP_IDIV, product, two, name);

done:
    xp3AtomicUnref(first);
    xp3AtomicUnref(last);
    xp3AtomicUnref(one);
    xp3AtomicUnref(two);
    xp3AtomicUnref(ends);
    xp3AtomicUnref(difference);
    xp3AtomicUnref(length);
    xp3AtomicUnref(product);
    return(result);
}

/*
 * Add up the values of a sequence with +, so that numbers are promoted
 * and overflow as for the operator. Only numbers can be added before
 * Part 10 brings durations. Sets *sum to NULL for the empty sequence.
 * Returns 0, or -1 after recording an error.
 */
static int
sumValues(xp3FuncCall *call, const xp3Value *value, const char *name,
          xp3Atomic **sum) {
    xp3Iter iter;
    xp3Item item;
    int found;

    *sum = NULL;
    if (xp3FuncSteps(call, xp3ValueCount(value)) < 0)
        return(-1);
    xp3IterInit(&iter, value);
    while ((found = xp3IterNext(&iter, &item)) > 0) {
        xp3Atomic *atomic = aggregateValue(call, item);
        xp3Atomic *next;

        if (atomic == NULL)
            goto error;
        if (!xp3TypeIsNumeric(atomic->type)) {
            if (isTemporal(atomic->type))
                xp3FuncError(call, XP3_ERR_UNSUPPORTED, "%s of durations "
                             "is not implemented yet", name);
            else
                xp3FuncError(call, XP3_ERR_FORG0006, "%s got a value of "
                             "type xs:%s, which is not a number", name,
                             (const char *) xp3TypeLocalName(atomic->type));
            xp3AtomicUnref(atomic);
            goto error;
        }
        if (*sum == NULL) {
            *sum = atomic;
            continue;
        }
        next = aggregateArithmetic(call, XP3_OP_ADD, *sum, atomic, name);
        xp3AtomicUnref(atomic);
        if (next == NULL)
            goto error;
        xp3AtomicUnref(*sum);
        *sum = next;
    }
    if (found < 0) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        goto error;
    }
    return(0);

error:
    xp3AtomicUnref(*sum);
    *sum = NULL;
    return(-1);
}

/**
 * fn:sum#1 and #2 (F&O 3.1, 14.4.5): the values added with +, after
 * casting xs:untypedAtomic to xs:double. A single value is returned
 * as it is. The sum of the empty sequence is $zero, or the
 * xs:integer 0 for fn:sum#1. A range is summed from its bounds.
 *
 * @param call   the call
 * @param args   $arg (xs:anyAtomicType*) and $zero
 *               (xs:anyAtomicType?)
 * @param count  1 or 2
 * @returns the sum, or NULL on error
 */
xp3Value *
xp3FnSum(xp3FuncCall *call, xp3Value **args, int count) {
    xp3Atomic *sum;

    if (xp3ValueCount(args[0]) == 0) {
        if (count == 2)
            return(xp3ValueRef(args[1]));
        return(xp3FuncAtomic(call, xp3AtomicNewInteger(0)));
    }
    if (args[0]->shape == XP3_SEQ_RANGE) {
        sum = rangeSum(call, args[0], 0, "fn:sum");
        return((sum != NULL) ? xp3FuncAtomic(call, sum) : NULL);
    }
    if (sumValues(call, args[0], "fn:sum", &sum) < 0)
        return(NULL);
    return(xp3FuncAtomic(call, sum));
}

/**
 * fn:avg#1 (F&O 3.1, 14.4.2): the sum of the values div their count,
 * so the average of integers is an xs:decimal, divided as by the div
 * operator (doc/DESIGN.md, D3). A range is averaged from its bounds.
 *
 * @param call   the call
 * @param args   the argument, xs:anyAtomicType*
 * @param count  1
 * @returns the average or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnAvg(xp3FuncCall *call, xp3Value **args, int count) {
    uint64_t length = xp3ValueCount(args[0]);
    xp3Atomic *sum;
    xp3Atomic *divisor;
    xp3Atomic *average;

    (void) count;
    if (length == 0)
        return(xp3ValueEmpty());
    if (args[0]->shape == XP3_SEQ_RANGE) {
        average = rangeSum(call, args[0], 1, "fn:avg");
        return((average != NULL) ? xp3FuncAtomic(call, average) : NULL);
    }
    if (sumValues(call, args[0], "fn:avg", &sum) < 0)
        return(NULL);
    /* Not a range, so the length is that of an array */
    divisor = xp3AtomicNewInteger((int64_t) length);
    if (divisor == NULL) {
        xp3AtomicUnref(sum);
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(NULL);
    }
    average = aggregateArithmetic(call, XP3_OP_DIV, sum, divisor, "fn:avg");
    xp3AtomicUnref(sum);
    xp3AtomicUnref(divisor);
    return((average != NULL) ? xp3FuncAtomic(call, average) : NULL);
}

/* Numeric primitive types in a sequence, for promotion in fn:min and
 * fn:max */
#define XP3_HAS_DECIMAL (1 << 0)
#define XP3_HAS_FLOAT (1 << 1)
#define XP3_HAS_DOUBLE (1 << 2)

/* Primitive string types in a sequence, for fn:min and fn:max */
#define XP3_HAS_STRING (1 << 0)
#define XP3_HAS_URI (1 << 1)

/* Whether a sequence has a value of xs:string or a type derived from
 * it, so that fn:min and fn:max use the collation */
static int
hasStrings(const xp3Value *value) {
    xp3Iter iter;
    xp3Item item;
    int strings = 0;

    xp3IterInit(&iter, value);
    while (!strings && (xp3IterNext(&iter, &item) > 0)) {
        strings = xp3TypeDerivesFrom(item.u.atomic->type, XP3_TYPE_STRING);
        xp3ItemUnref(item);
    }
    return(strings);
}

/* Compare two values for fn:min and fn:max: strings and URIs under
 * the collation, all others as by the value comparison */
static xp3ErrCode
orderValues(xp3AstOp op, const xp3Atomic *left, const xp3Atomic *right,
            xp3LanguageLevel level, xp3Collation collation, int *result) {
    int cmp;

    if (!isStringType(left->type) || !isStringType(right->type))
        return(xp3CompareAtomics(op, left, right, level, result));
    cmp = xp3CollationCompare(collation, left->u.str, right->u.str);
    *result = (op == XP3_OP_VAL_LT) ? (cmp < 0) : (cmp > 0);
    return(XP3_ERR_OK);
}

/*
 * fn:min and fn:max (F&O 3.1, 14.4.4 and 14.4.3): the value for which
 * op (lt or gt) holds against every other, compared as by the value
 * comparison. The value keeps its type, unless the sequence mixes
 * xs:decimal, xs:float and xs:double, which promotes it to xs:float or
 * xs:double, or xs:string and xs:anyURI, which makes a URI an
 * xs:string; if there is NaN, the result is NaN. Strings compare under the
 * collation; if there are only URIs, the spec ignores it.
 */
static xp3Value *
extremeValue(xp3FuncCall *call, const xp3Value *value, xp3AstOp op,
             xp3Collation collation, const char *name) {
    xp3LanguageLevel level = xp3FuncLevel(call);
    xp3Atomic *best = NULL;
    xp3Atomic *result;
    xp3TypeId target;
    xp3ErrCode err;
    xp3Iter iter;
    xp3Item item;
    int numbers = 0;
    int strings = 0;
    int nan = 0;
    int found;

    if (xp3ValueCount(value) == 0)
        return(xp3ValueEmpty());
    if (value->shape == XP3_SEQ_RANGE)
        return(xp3FuncAtomic(call, xp3AtomicNewInteger(
            (op == XP3_OP_VAL_LT) ? value->u.range.first :
                                    value->u.range.last)));
    if (xp3FuncSteps(call, xp3ValueCount(value)) < 0)
        return(NULL);
    if ((collation != XP3_COLLATION_CODEPOINT) && !hasStrings(value))
        collation = XP3_COLLATION_CODEPOINT;

    xp3IterInit(&iter, value);
    while ((found = xp3IterNext(&iter, &item)) > 0) {
        xp3Atomic *atomic = aggregateValue(call, item);
        int better;

        if (atomic == NULL)
            goto error;
        if (isTemporal(atomic->type)) {
            xp3FuncError(call, XP3_ERR_UNSUPPORTED, "%s of dates, times "
                         "and durations is not implemented yet", name);
            xp3AtomicUnref(atomic);
            goto error;
        }
        if (xp3TypeDerivesFrom(atomic->type, XP3_TYPE_DECIMAL)) {
            numbers |= XP3_HAS_DECIMAL;
        } else if (atomic->type == XP3_TYPE_FLOAT) {
            numbers |= XP3_HAS_FLOAT;
            nan |= isnan(atomic->u.f);
        } else if (atomic->type == XP3_TYPE_DOUBLE) {
            numbers |= XP3_HAS_DOUBLE;
            nan |= isnan(atomic->u.d);
        } else if (xp3TypeDerivesFrom(atomic->type, XP3_TYPE_STRING)) {
            strings |= XP3_HAS_STRING;
        } else if (atomic->type == XP3_TYPE_ANY_URI) {
            strings |= XP3_HAS_URI;
        }
        /* The first value is compared with itself, so that a single
         * value without an order, such as a QName, is an error too */
        err = orderValues(op, atomic, (best != NULL) ? best : atomic,
                          level, collation, &better);
        if (err != XP3_ERR_OK) {
            aggregateError(call, err, name, atomic,
                           (best != NULL) ? best : atomic);
            xp3AtomicUnref(atomic);
            goto error;
        }
        if ((best == NULL) || better) {
            xp3AtomicUnref(best);
            best = atomic;
        } else {
            xp3AtomicUnref(atomic);
        }
    }
    if (found < 0) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        goto error;
    }

    /* More than one numeric primitive type promotes all of them. With
     * strings and URIs, a URI becomes an xs:string, while a string
     * keeps its type, as QT3 expects (fn-max-18) */
    if ((strings == (XP3_HAS_STRING | XP3_HAS_URI)) &&
        (best->type == XP3_TYPE_ANY_URI))
        target = XP3_TYPE_STRING;
    else if (numbers & (numbers - 1))
        target = (numbers & XP3_HAS_DOUBLE) ? XP3_TYPE_DOUBLE :
                                              XP3_TYPE_FLOAT;
    else
        target = best->type;
    if (nan) {
        xp3AtomicUnref(best);
        if (target == XP3_TYPE_FLOAT)
            return(xp3FuncAtomic(call, xp3AtomicNewFloat(
                (float) xp3DoubleNaN())));
        return(xp3FuncAtomic(call, xp3AtomicNewDouble(xp3DoubleNaN())));
    }
    if (target == best->type)
        return(xp3FuncAtomic(call, best));
    err = xp3CastAtomic(best, target, &result);
    xp3AtomicUnref(best);
    if (err != XP3_ERR_OK) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(NULL);
    }
    return(xp3FuncAtomic(call, result));

error:
    xp3AtomicUnref(best);
    return(NULL);
}

/**
 * fn:min#1 and #2 (F&O 3.1, 14.4.4): the lowest value, after casting
 * xs:untypedAtomic to xs:double. The lowest of a range is its first
 * integer.
 *
 * @param call   the call
 * @param args   the values (xs:anyAtomicType*) and the collation
 *               (xs:string)
 * @param count  1 or 2
 * @returns the value or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnMin(xp3FuncCall *call, xp3Value **args, int count) {
    xp3Collation collation;

    if (callCollation(call, args, count, 1, &collation) < 0)
        return(NULL);
    return(extremeValue(call, args[0], XP3_OP_VAL_LT, collation, "fn:min"));
}

/**
 * fn:max#1 and #2 (F&O 3.1, 14.4.3): the highest value, after casting
 * xs:untypedAtomic to xs:double. The highest of a range is its last
 * integer.
 *
 * @param call   the call
 * @param args   the values (xs:anyAtomicType*) and the collation
 *               (xs:string)
 * @param count  1 or 2
 * @returns the value or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnMax(xp3FuncCall *call, xp3Value **args, int count) {
    xp3Collation collation;

    if (callCollation(call, args, count, 1, &collation) < 0)
        return(NULL);
    return(extremeValue(call, args[0], XP3_OP_VAL_GT, collation, "fn:max"));
}

/*
 * The focus
 */

/**
 * fn:position#0 (F&O 3.1, 15.1): the context position.
 *
 * @param call   the call
 * @param args   none
 * @param count  0
 * @returns an xs:integer, or NULL on error, XPDY0002 without a focus
 */
xp3Value *
xp3FnPosition(xp3FuncCall *call, xp3Value **args, int count) {
    uint64_t position;

    (void) args;
    (void) count;
    if (xp3FuncFocus(call, NULL, &position, NULL) < 0)
        return(NULL);
    return(newCount(call, position));
}

/**
 * fn:last#0 (F&O 3.1, 15.2): the context size.
 *
 * @param call   the call
 * @param args   none
 * @param count  0
 * @returns an xs:integer, or NULL on error, XPDY0002 without a focus
 */
xp3Value *
xp3FnLast(xp3FuncCall *call, xp3Value **args, int count) {
    uint64_t size;

    (void) args;
    (void) count;
    if (xp3FuncFocus(call, NULL, NULL, &size) < 0)
        return(NULL);
    return(newCount(call, size));
}
