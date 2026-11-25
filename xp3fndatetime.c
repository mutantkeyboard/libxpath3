/*
 * xp3fndatetime.c: the functions on dates, times and durations
 *
 * Implementations of the functions of F&O 3.1,
 * https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/, that
 * take dates, times and durations apart (8.3 and 9.5), bound to their
 * signatures in xp3func.c. The evaluator has applied the function
 * conversion rules to the arguments already, so an xs:duration?
 * argument is the empty sequence or one value of a duration type.
 *
 * Components are those of the value as it is kept (xp3datetime.h):
 * the local value of a date or time, never normalized to UTC, and the
 * months and seconds of a duration, from which the components follow
 * by idiv and mod, which truncate toward zero as C's / and % do, so
 * every component has the sign of the duration.
 *
 * Every call that does some work counts one step.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdint.h>
#include <stdio.h>

#include <libxml/xmlstring.h>

#include "private/xp3datetime.h"
#include "private/xp3decimal.h"
#include "private/xp3error.h"
#include "private/xp3func.h"
#include "private/xp3types.h"
#include "private/xp3value.h"

#define XP3_SECONDS_PER_DAY 86400

/*
 * Helpers
 */

/* The value of an argument of a date, time or duration type with "?",
 * which is one item: borrowed, or NULL for the empty sequence */
static const xp3Atomic *
argAtomic(const xp3Value *value) {
    if (xp3ValueCount(value) == 0)
        return(NULL);
    return(value->u.single.u.atomic);
}

/* An xs:integer result */
static xp3Value *
newInteger(xp3FuncCall *call, int64_t value) {
    return(xp3FuncAtomic(call, xp3AtomicNewInteger(value)));
}

/*
 * An xs:decimal result of whole seconds and nanoseconds with the same
 * sign, exact: the text "-ss.nnnnnnnnn" read as a decimal
 */
static xp3Value *
newSeconds(xp3FuncCall *call, int64_t seconds, int32_t nanos) {
    char text[48];
    const char *sign = "";
    unsigned long long whole;
    long fraction;
    xp3Decimal *dec = NULL;
    xp3ErrCode err;
    int len;

    if ((seconds < 0) || (nanos < 0)) {
        sign = "-";
        seconds = -seconds;
        nanos = -nanos;
    }
    whole = (unsigned long long) seconds;
    fraction = (long) nanos;
    len = snprintf(text, sizeof(text), "%s%llu.%09ld", sign, whole,
                   fraction);
    err = xp3DecimalParse(BAD_CAST text, (size_t) len, 0, &dec);
    if (err != XP3_ERR_OK) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(NULL);
    }
    return(xp3FuncAtomic(call, xp3AtomicNewDecimal(XP3_TYPE_DECIMAL, dec)));
}

/*
 * Components of durations (F&O 3.1, 8.3)
 */

/* A component of a duration, from its months and seconds */
typedef int64_t (*xp3DurationPart)(const xp3Duration *value);

static int64_t
yearsPart(const xp3Duration *value) {
    return(value->months / 12);
}

static int64_t
monthsPart(const xp3Duration *value) {
    return(value->months % 12);
}

static int64_t
daysPart(const xp3Duration *value) {
    return(value->seconds / XP3_SECONDS_PER_DAY);
}

static int64_t
hoursPart(const xp3Duration *value) {
    return(value->seconds % XP3_SECONDS_PER_DAY / 3600);
}

static int64_t
minutesPart(const xp3Duration *value) {
    return(value->seconds % 3600 / 60);
}

/* An xs:integer component of an xs:duration? argument, or the empty
 * sequence */
static xp3Value *
durationComponent(xp3FuncCall *call, const xp3Value *arg,
                  xp3DurationPart part) {
    const xp3Atomic *value = argAtomic(arg);

    if (value == NULL)
        return(xp3ValueEmpty());
    if (xp3FuncSteps(call, 1) < 0)
        return(NULL);
    return(newInteger(call, part(&value->u.dur)));
}

/**
 * fn:years-from-duration#1 (F&O 3.1, 8.3.1): the months idiv 12, so 0
 * for an xs:dayTimeDuration.
 *
 * @param call   the call
 * @param args   the duration, xs:duration?
 * @param count  1
 * @returns an xs:integer or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnYearsFromDuration(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(durationComponent(call, args[0], yearsPart));
}

/**
 * fn:months-from-duration#1 (F&O 3.1, 8.3.2): the months mod 12.
 *
 * @param call   the call
 * @param args   the duration, xs:duration?
 * @param count  1
 * @returns an xs:integer or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnMonthsFromDuration(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(durationComponent(call, args[0], monthsPart));
}

/**
 * fn:days-from-duration#1 (F&O 3.1, 8.3.3): the seconds idiv 86400, so
 * 0 for an xs:yearMonthDuration.
 *
 * @param call   the call
 * @param args   the duration, xs:duration?
 * @param count  1
 * @returns an xs:integer or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnDaysFromDuration(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(durationComponent(call, args[0], daysPart));
}

/**
 * fn:hours-from-duration#1 (F&O 3.1, 8.3.4): (the seconds mod 86400)
 * idiv 3600.
 *
 * @param call   the call
 * @param args   the duration, xs:duration?
 * @param count  1
 * @returns an xs:integer or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnHoursFromDuration(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(durationComponent(call, args[0], hoursPart));
}

/**
 * fn:minutes-from-duration#1 (F&O 3.1, 8.3.5): (the seconds mod 3600)
 * idiv 60.
 *
 * @param call   the call
 * @param args   the duration, xs:duration?
 * @param count  1
 * @returns an xs:integer or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnMinutesFromDuration(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(durationComponent(call, args[0], minutesPart));
}

/**
 * fn:seconds-from-duration#1 (F&O 3.1, 8.3.6): the seconds mod 60 with
 * the fraction, as an xs:decimal.
 *
 * @param call   the call
 * @param args   the duration, xs:duration?
 * @param count  1
 * @returns an xs:decimal or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnSecondsFromDuration(xp3FuncCall *call, xp3Value **args, int count) {
    const xp3Atomic *value = argAtomic(args[0]);

    (void) count;
    if (value == NULL)
        return(xp3ValueEmpty());
    if (xp3FuncSteps(call, 1) < 0)
        return(NULL);
    return(newSeconds(call, value->u.dur.seconds % 60,
                      value->u.dur.nanoseconds));
}

/*
 * Components of dates and times (F&O 3.1, 9.5). One implementation
 * serves the functions of xs:dateTime, xs:date and xs:time that take
 * the same component, as the signatures leave only the right type
 * through.
 */

/* A component of the local value of a date or time */
typedef int64_t (*xp3DateTimePart)(const xp3DateTime *value);

static int64_t
yearPart(const xp3DateTime *value) {
    return(value->year);
}

static int64_t
monthPart(const xp3DateTime *value) {
    return(value->month);
}

static int64_t
dayPart(const xp3DateTime *value) {
    return(value->day);
}

static int64_t
hourPart(const xp3DateTime *value) {
    return(value->hour);
}

static int64_t
minutePart(const xp3DateTime *value) {
    return(value->minute);
}

/* An xs:integer component of a date or time argument with "?", or the
 * empty sequence */
static xp3Value *
dateTimeComponent(xp3FuncCall *call, const xp3Value *arg,
                  xp3DateTimePart part) {
    const xp3Atomic *value = argAtomic(arg);

    if (value == NULL)
        return(xp3ValueEmpty());
    if (xp3FuncSteps(call, 1) < 0)
        return(NULL);
    return(newInteger(call, part(&value->u.dt)));
}

/**
 * fn:year-from-dateTime#1 and fn:year-from-date#1 (F&O 3.1, 9.5.1 and
 * 9.5.8): the year of the local value, negative before year 0.
 *
 * @param call   the call
 * @param args   the xs:dateTime? or xs:date?
 * @param count  1
 * @returns an xs:integer or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnYearFrom(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(dateTimeComponent(call, args[0], yearPart));
}

/**
 * fn:month-from-dateTime#1 and fn:month-from-date#1 (F&O 3.1, 9.5.2
 * and 9.5.9): the month of the local value, 1 to 12.
 *
 * @param call   the call
 * @param args   the xs:dateTime? or xs:date?
 * @param count  1
 * @returns an xs:integer or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnMonthFrom(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(dateTimeComponent(call, args[0], monthPart));
}

/**
 * fn:day-from-dateTime#1 and fn:day-from-date#1 (F&O 3.1, 9.5.3 and
 * 9.5.10): the day of the local value, 1 to 31.
 *
 * @param call   the call
 * @param args   the xs:dateTime? or xs:date?
 * @param count  1
 * @returns an xs:integer or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnDayFrom(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(dateTimeComponent(call, args[0], dayPart));
}

/**
 * fn:hours-from-dateTime#1 and fn:hours-from-time#1 (F&O 3.1, 9.5.4 and
 * 9.5.12): the hours of the local value, 0 to 23. 24:00:00 was read as
 * 00:00:00, of the next day for an xs:dateTime.
 *
 * @param call   the call
 * @param args   the xs:dateTime? or xs:time?
 * @param count  1
 * @returns an xs:integer or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnHoursFrom(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(dateTimeComponent(call, args[0], hourPart));
}

/**
 * fn:minutes-from-dateTime#1 and fn:minutes-from-time#1 (F&O 3.1, 9.5.5
 * and 9.5.13): the minutes of the local value, 0 to 59.
 *
 * @param call   the call
 * @param args   the xs:dateTime? or xs:time?
 * @param count  1
 * @returns an xs:integer or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnMinutesFrom(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(dateTimeComponent(call, args[0], minutePart));
}

/**
 * fn:seconds-from-dateTime#1 and fn:seconds-from-time#1 (F&O 3.1,
 * 9.5.6 and 9.5.14): the seconds of the local value with the fraction,
 * as an xs:decimal from 0 to less than 60.
 *
 * @param call   the call
 * @param args   the xs:dateTime? or xs:time?
 * @param count  1
 * @returns an xs:decimal or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnSecondsFrom(xp3FuncCall *call, xp3Value **args, int count) {
    const xp3Atomic *value = argAtomic(args[0]);

    (void) count;
    if (value == NULL)
        return(xp3ValueEmpty());
    if (xp3FuncSteps(call, 1) < 0)
        return(NULL);
    return(newSeconds(call, value->u.dt.second, value->u.dt.nanosecond));
}
