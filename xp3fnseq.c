/*
 * xp3fnseq.c: the accessor, boolean, error, sequence and aggregate
 * functions
 *
 * Implementations of built-in functions of F&O 3.1,
 * https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/, bound
 * to their signatures in xp3func.c. The evaluator has applied the
 * function conversion rules to the arguments already, so each gets
 * values of its declared type (xp3func.h).
 *
 * Sequences are shared, not copied, where a result is an argument or
 * a range of integers: fn:subsequence((1 to 1000000000), 5) is a range
 * again, and fn:count, fn:sum, fn:avg, fn:min and fn:max never expand
 * one. Copying items counts a step per item, and results respect the
 * limit on sequence length, as everywhere in the evaluator.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <math.h>
#include <stdint.h>
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

/* Whether values of a type are durations, dates or times, which come
 * with Part 10 */
static int
isTemporal(xp3TypeId type) {
    xp3Representation rep = xp3TypeRepresentation(type);

    return((rep == XP3_REP_DURATION) || (rep == XP3_REP_DATE_TIME));
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

/*
 * fn:min and fn:max (F&O 3.1, 14.4.4 and 14.4.3): the value for which
 * op (lt or gt) holds against every other, compared as by the value
 * comparison. The value keeps its type, unless the sequence mixes
 * xs:decimal, xs:float and xs:double, which promotes it to xs:float or
 * xs:double; if there is NaN, the result is NaN. Strings need the
 * codepoint collation, which comes with the string functions.
 */
static xp3Value *
extremeValue(xp3FuncCall *call, const xp3Value *value, xp3AstOp op,
             const char *name) {
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
        } else if (xp3TypeDerivesFrom(atomic->type, XP3_TYPE_STRING) ||
                   (atomic->type == XP3_TYPE_ANY_URI)) {
            strings = 1;
        }
        /* The first value is compared with itself, so that a single
         * value without an order, such as a QName, is an error too */
        err = xp3CompareAtomics(op, atomic, (best != NULL) ? best : atomic,
                                level, &better);
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
    if (strings) {
        xp3FuncError(call, XP3_ERR_UNSUPPORTED, "%s of strings needs the "
                     "codepoint collation, which is not implemented yet",
                     name);
        goto error;
    }

    /* More than one numeric primitive type promotes all of them */
    if (numbers & (numbers - 1))
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
 * fn:min#1 (F&O 3.1, 14.4.4): the lowest value, after casting
 * xs:untypedAtomic to xs:double. The lowest of a range is its first
 * integer.
 *
 * @param call   the call
 * @param args   the argument, xs:anyAtomicType*
 * @param count  1
 * @returns the value or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnMin(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(extremeValue(call, args[0], XP3_OP_VAL_LT, "fn:min"));
}

/**
 * fn:max#1 (F&O 3.1, 14.4.3): the highest value, after casting
 * xs:untypedAtomic to xs:double. The highest of a range is its last
 * integer.
 *
 * @param call   the call
 * @param args   the argument, xs:anyAtomicType*
 * @param count  1
 * @returns the value or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnMax(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(extremeValue(call, args[0], XP3_OP_VAL_GT, "fn:max"));
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
