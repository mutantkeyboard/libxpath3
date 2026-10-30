/*
 * xp3fnseq.c: the accessor, boolean, error and sequence functions
 *
 * Implementations of built-in functions of F&O 3.1,
 * https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/, bound
 * to their signatures in xp3func.c. The evaluator has applied the
 * function conversion rules to the arguments already, so each gets
 * values of its declared type (xp3func.h).
 *
 * Sequences are shared, not copied, where a result is an argument or
 * a range of integers: fn:subsequence((1 to 1000000000), 5) is a range
 * again, and fn:count never expands one. Copying items counts a step
 * per item, and results respect the limit on sequence length, as
 * everywhere in the evaluator.
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
