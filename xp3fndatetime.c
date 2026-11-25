/*
 * xp3fndatetime.c: the functions on dates, times and durations
 *
 * Implementations of the functions of F&O 3.1,
 * https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/, on
 * dates, times and durations, bound to their signatures in xp3func.c:
 * the components (8.3 and 9.5), fn:dateTime (9.3.1),
 * adjust-*-to-timezone (9.6), fn:parse-ietf-date (9.9.1) and the
 * clock (15.3 to 15.6). The evaluator has applied the function
 * conversion rules to the arguments already, so an xs:duration?
 * argument is the empty sequence or one value of a duration type.
 * fn:format-date, fn:format-time and fn:format-dateTime come with
 * fn:format-integer (doc/DESIGN.md 4.2).
 *
 * Components are those of the value as it is kept (xp3datetime.h):
 * the local value of a date or time, never normalized to UTC, and the
 * months and seconds of a duration, from which the components follow
 * by idiv and mod, which truncate toward zero as C's / and % do, so
 * every component has the sign of the duration.
 *
 * The clock and the implicit timezone come from the evaluation, which
 * reads them at most once (doc/DESIGN.md 5). Folding has neither, so
 * the functions that need them are context-dependent in the table and
 * never folded.
 *
 * Every call that does some work counts one step, and
 * fn:parse-ietf-date the bytes of its input too.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

/* A timezone in minutes as an xs:dayTimeDuration */
static xp3Value *
newTimezone(xp3FuncCall *call, int minutes) {
    xp3Duration duration;

    duration.months = 0;
    duration.seconds = (int64_t) minutes * 60;
    duration.nanoseconds = 0;
    return(xp3FuncAtomic(call, xp3AtomicNewDuration(
        XP3_TYPE_DAY_TIME_DURATION, &duration)));
}

/**
 * fn:timezone-from-dateTime#1, fn:timezone-from-date#1 and
 * fn:timezone-from-time#1 (F&O 3.1, 9.5.7, 9.5.11 and 9.5.15): the
 * timezone as an xs:dayTimeDuration from -PT14H to PT14H, or the empty
 * sequence for a value without one.
 *
 * @param call   the call
 * @param args   the xs:dateTime?, xs:date? or xs:time?
 * @param count  1
 * @returns an xs:dayTimeDuration or the empty sequence, or NULL on
 *          error
 */
xp3Value *
xp3FnTimezoneFrom(xp3FuncCall *call, xp3Value **args, int count) {
    const xp3Atomic *value = argAtomic(args[0]);

    (void) count;
    if (value == NULL)
        return(xp3ValueEmpty());
    if (xp3FuncSteps(call, 1) < 0)
        return(NULL);
    if (!value->u.dt.hasTimezone)
        return(xp3ValueEmpty());
    return(newTimezone(call, value->u.dt.timezone));
}

/*
 * Constructing a dateTime (F&O 3.1, 9.3.1)
 */

/**
 * fn:dateTime#2 (F&O 3.1, 9.3.1): the date of an xs:date with the time
 * of an xs:time, and the timezone that either has. Both having
 * different timezones is FORG0008; -00:00 is Z, so the two are the
 * same.
 *
 * @param call   the call
 * @param args   the xs:date? and the xs:time?
 * @param count  2
 * @returns an xs:dateTime or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnDateTime(xp3FuncCall *call, xp3Value **args, int count) {
    const xp3Atomic *date = argAtomic(args[0]);
    const xp3Atomic *time = argAtomic(args[1]);
    const xp3DateTime *dateValue;
    const xp3DateTime *timeValue;
    xp3DateTime result;

    (void) count;
    if ((date == NULL) || (time == NULL))
        return(xp3ValueEmpty());
    if (xp3FuncSteps(call, 1) < 0)
        return(NULL);
    dateValue = &date->u.dt;
    timeValue = &time->u.dt;
    if (dateValue->hasTimezone && timeValue->hasTimezone &&
        (dateValue->timezone != timeValue->timezone)) {
        xp3FuncError(call, XP3_ERR_FORG0008, "fn:dateTime got a date and "
                     "a time with different timezones");
        return(NULL);
    }
    result = *dateValue;
    result.hour = timeValue->hour;
    result.minute = timeValue->minute;
    result.second = timeValue->second;
    result.nanosecond = timeValue->nanosecond;
    if (!result.hasTimezone) {
        result.hasTimezone = timeValue->hasTimezone;
        result.timezone = timeValue->timezone;
    }
    return(xp3FuncAtomic(call, xp3AtomicNewDateTime(XP3_TYPE_DATE_TIME,
                                                    &result)));
}

/*
 * Adjusting to a timezone (F&O 3.1, 9.6)
 */

/* The minutes of +-PT14H, the largest timezones */
#define XP3_TIMEZONE_MAX_SECONDS ((int64_t) XP3_TIMEZONE_MAX * 60)

/*
 * The timezone a value is adjusted to: the $timezone argument, which
 * must be within -PT14H and PT14H in whole minutes (FODT0003), or the
 * implicit timezone without one. Returns 1 with *minutes set, 0 for
 * the empty sequence, which removes the timezone, or -1 after recording
 * an error.
 */
static int
targetTimezone(xp3FuncCall *call, const char *name, xp3Value **args,
               int count, int *minutes) {
    const xp3Atomic *value;
    const xp3Duration *duration;
    xp3ErrCode err;

    *minutes = 0;
    if (count == 1) {
        err = xp3ClockTimezone(xp3FuncClock(call), minutes);
        if (err != XP3_ERR_OK) {
            xp3FuncError(call, err, "%s needs the implicit timezone", name);
            return(-1);
        }
        return(1);
    }
    value = argAtomic(args[1]);
    if (value == NULL)
        return(0);
    duration = &value->u.dur;
    if ((duration->nanoseconds != 0) || (duration->seconds % 60 != 0) ||
        (duration->seconds > XP3_TIMEZONE_MAX_SECONDS) ||
        (duration->seconds < -XP3_TIMEZONE_MAX_SECONDS)) {
        xp3FuncError(call, XP3_ERR_FODT0003, "%s got a timezone that "
                     "isn't a whole number of minutes from -PT14H to "
                     "PT14H", name);
        return(-1);
    }
    *minutes = (int) (duration->seconds / 60);
    return(1);
}

/*
 * Adjust a date or time value to a timezone, or remove its timezone,
 * as F&O 3.1, 9.6.1 to 9.6.3 describe. A value without a timezone gets
 * the target timezone and keeps its local value. A value with one is
 * moved by the difference of the two timezones, so it stays the same
 * instant: an xs:date from its start, 00:00:00, which may land on the
 * day before or after, and an xs:time around midnight, as both go
 * through xp3DateTimeAddDuration(). Removing the timezone keeps the
 * local value.
 */
static xp3Value *
adjustToTimezone(xp3FuncCall *call, const char *name, xp3TypeId type,
                 xp3Value **args, int count) {
    const xp3Atomic *value = argAtomic(args[0]);
    const xp3DateTime *local;
    xp3DateTime result;
    xp3Duration shift;
    xp3ErrCode err;
    int target;
    int hasTarget;

    if (value == NULL)
        return(xp3ValueEmpty());
    hasTarget = targetTimezone(call, name, args, count, &target);
    if (hasTarget < 0)
        return(NULL);
    if (xp3FuncSteps(call, 1) < 0)
        return(NULL);
    local = &value->u.dt;
    result = *local;
    if (!hasTarget) {
        result.hasTimezone = 0;
        result.timezone = 0;
    } else if (!local->hasTimezone) {
        result.hasTimezone = 1;
        result.timezone = (int16_t) target;
    } else {
        shift.months = 0;
        shift.seconds = (int64_t) (target - local->timezone) * 60;
        shift.nanoseconds = 0;
        err = xp3DateTimeAddDuration(type, local, &shift, &result);
        if (err != XP3_ERR_OK) {
            xp3FuncError(call, err, "the result of %s is beyond the years "
                         "-2147483647 to 2147483647", name);
            return(NULL);
        }
        result.timezone = (int16_t) target;
    }
    return(xp3FuncAtomic(call, xp3AtomicNewDateTime(type, &result)));
}

/**
 * fn:adjust-dateTime-to-timezone#1 and #2 (F&O 3.1, 9.6.1): the same
 * instant in another timezone, or the local value without a timezone.
 * #1 adjusts to the implicit timezone.
 *
 * @param call   the call
 * @param args   the xs:dateTime?, and the xs:dayTimeDuration? timezone
 * @param count  1 or 2
 * @returns an xs:dateTime or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnAdjustDateTimeToTimezone(xp3FuncCall *call, xp3Value **args,
                              int count) {
    return(adjustToTimezone(call, "fn:adjust-dateTime-to-timezone",
                            XP3_TYPE_DATE_TIME, args, count));
}

/**
 * fn:adjust-date-to-timezone#1 and #2 (F&O 3.1, 9.6.2): the date in
 * another timezone that contains the start of the date, or the date
 * without a timezone. #1 adjusts to the implicit timezone.
 *
 * @param call   the call
 * @param args   the xs:date?, and the xs:dayTimeDuration? timezone
 * @param count  1 or 2
 * @returns an xs:date or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnAdjustDateToTimezone(xp3FuncCall *call, xp3Value **args, int count) {
    return(adjustToTimezone(call, "fn:adjust-date-to-timezone",
                            XP3_TYPE_DATE, args, count));
}

/**
 * fn:adjust-time-to-timezone#1 and #2 (F&O 3.1, 9.6.3): the time in
 * another timezone, wrapping around midnight, or the time without a
 * timezone. #1 adjusts to the implicit timezone.
 *
 * @param call   the call
 * @param args   the xs:time?, and the xs:dayTimeDuration? timezone
 * @param count  1 or 2
 * @returns an xs:time or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnAdjustTimeToTimezone(xp3FuncCall *call, xp3Value **args, int count) {
    return(adjustToTimezone(call, "fn:adjust-time-to-timezone",
                            XP3_TYPE_TIME, args, count));
}

/*
 * The clock (F&O 3.1, 15.3 to 15.6). The evaluation reads it at most
 * once, or takes the values the dynamic context set, so every call in
 * one evaluation gives the same values (doc/DESIGN.md 5). Folding has
 * no clock: these functions are context-dependent and never folded.
 */

/* The current dateTime of the evaluation, in the implicit timezone.
 * Returns 0, or -1 after recording an error. */
static int
currentDateTime(xp3FuncCall *call, const char *name, xp3DateTime *now) {
    xp3ErrCode err = xp3ClockNow(xp3FuncClock(call), now);

    if (err == XP3_ERR_OK)
        return(0);
    if (err == XP3_ERR_NEEDS_CONTEXT)
        xp3FuncError(call, err, "%s needs the clock", name);
    else
        xp3FuncError(call, err, "the current dateTime in the implicit "
                     "timezone is beyond the years -2147483647 to "
                     "2147483647");
    return(-1);
}

/* The date or the time of the current dateTime, as fn:current-date and
 * fn:current-time give them */
static xp3Value *
currentPart(xp3FuncCall *call, const char *name, xp3TypeId type) {
    xp3DateTime now;
    xp3DateTime part;
    xp3ErrCode err;

    if (xp3FuncSteps(call, 1) < 0)
        return(NULL);
    if (currentDateTime(call, name, &now) < 0)
        return(NULL);
    err = xp3DateTimeConvert(XP3_TYPE_DATE_TIME, &now, type, &part);
    if (err != XP3_ERR_OK) {
        xp3FuncError(call, err, NULL);
        return(NULL);
    }
    return(xp3FuncAtomic(call, xp3AtomicNewDateTime(type, &part)));
}

/**
 * fn:current-dateTime#0 (F&O 3.1, 15.3): the current dateTime of the
 * evaluation, an xs:dateTimeStamp in the implicit timezone, with the
 * microseconds of the system clock.
 *
 * @param call   the call
 * @param args   none
 * @param count  0
 * @returns an xs:dateTimeStamp, or NULL on error
 */
xp3Value *
xp3FnCurrentDateTime(xp3FuncCall *call, xp3Value **args, int count) {
    xp3DateTime now;

    (void) args;
    (void) count;
    if (xp3FuncSteps(call, 1) < 0)
        return(NULL);
    if (currentDateTime(call, "fn:current-dateTime", &now) < 0)
        return(NULL);
    return(xp3FuncAtomic(call, xp3AtomicNewDateTime(
        XP3_TYPE_DATE_TIME_STAMP, &now)));
}

/**
 * fn:current-date#0 (F&O 3.1, 15.4): the date of the current dateTime,
 * with its timezone.
 *
 * @param call   the call
 * @param args   none
 * @param count  0
 * @returns an xs:date, or NULL on error
 */
xp3Value *
xp3FnCurrentDate(xp3FuncCall *call, xp3Value **args, int count) {
    (void) args;
    (void) count;
    return(currentPart(call, "fn:current-date", XP3_TYPE_DATE));
}

/**
 * fn:current-time#0 (F&O 3.1, 15.5): the time of the current dateTime,
 * with its timezone.
 *
 * @param call   the call
 * @param args   none
 * @param count  0
 * @returns an xs:time, or NULL on error
 */
xp3Value *
xp3FnCurrentTime(xp3FuncCall *call, xp3Value **args, int count) {
    (void) args;
    (void) count;
    return(currentPart(call, "fn:current-time", XP3_TYPE_TIME));
}

/**
 * fn:implicit-timezone#0 (F&O 3.1, 15.6): the implicit timezone of the
 * evaluation as an xs:dayTimeDuration.
 *
 * @param call   the call
 * @param args   none
 * @param count  0
 * @returns an xs:dayTimeDuration, or NULL on error
 */
xp3Value *
xp3FnImplicitTimezone(xp3FuncCall *call, xp3Value **args, int count) {
    xp3ErrCode err;
    int minutes;

    (void) args;
    (void) count;
    if (xp3FuncSteps(call, 1) < 0)
        return(NULL);
    err = xp3ClockTimezone(xp3FuncClock(call), &minutes);
    if (err != XP3_ERR_OK) {
        xp3FuncError(call, err, "fn:implicit-timezone needs the clock");
        return(NULL);
    }
    return(newTimezone(call, minutes));
}

/*
 * fn:parse-ietf-date (F&O 3.1, 9.9.1)
 *
 * The input is read by the grammar of 9.9.1, one function per
 * production, ignoring case. A production that fails leaves the input
 * where it stopped; the optional parts that the grammar can only tell
 * by looking ahead, the space before a timezone and the tzname in
 * parentheses after an offset, go back to where they started.
 */

/* What the input gives, before it is checked to be a valid dateTime */
typedef struct {
    int year;                   /* as written, 0 to 9999 */
    int yearDigits;             /* 2 or 4 */
    int month;                  /* 1 to 12 */
    int day;                    /* as written, 0 to 99 */
    int hour;                   /* as written, 0 to 99 */
    int minute;
    int second;
    int32_t nanos;
    int timezone;               /* minutes east of UTC, as written */
    int timezoneMinutes;        /* the minutes part of an offset */
} xp3IetfDate;

/* The tznames of 9.9.1 and their offsets in minutes; "utc" comes before
 * "ut", which is a prefix of it */
static const struct {
    const char *name;
    int offset;
} xp3IetfZones[] = {
    { "utc", 0 }, { "ut", 0 }, { "gmt", 0 }, { "est", -300 },
    { "edt", -240 }, { "cst", -360 }, { "cdt", -300 }, { "mst", -420 },
    { "mdt", -360 }, { "pst", -480 }, { "pdt", -420 }
};

/* The daynames of 9.9.1, each full name before its abbreviation, which
 * is a prefix of it */
static const char *const xp3IetfDayNames[] = {
    "monday", "tuesday", "wednesday", "thursday", "friday", "saturday",
    "sunday", "mon", "tue", "wed", "thu", "fri", "sat", "sun"
};

static const char *const xp3IetfMonthNames[12] = {
    "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct",
    "nov", "dec"
};

/* Production S: x09, x0A, x0D or x20 */
static int
isIetfSpace(int c) {
    return((c == 0x20) || (c == 0x9) || (c == 0xA) || (c == 0xD));
}

static int
isIetfDigit(int c) {
    return((c >= '0') && (c <= '9'));
}

static int
isIetfLetter(int c) {
    return(((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')));
}

/* Skip whitespace. Returns the number of characters skipped. */
static int
skipIetfSpace(const xmlChar **cur) {
    int count = 0;

    while (isIetfSpace(**cur)) {
        (*cur)++;
        count++;
    }
    return(count);
}

/* Skip a word written in lower case if the input starts with it in
 * any case. Returns 1, or 0 if it doesn't. */
static int
matchIetfWord(const xmlChar **cur, const char *word) {
    size_t i;

    for (i = 0; word[i] != 0; i++) {
        int c = (*cur)[i];

        if ((c >= 'A') && (c <= 'Z'))
            c = c - 'A' + 'a';
        if (c != word[i])
            return(0);
    }
    *cur += i;
    return(1);
}

/* The number of digits at the input */
static int
countIetfDigits(const xmlChar *cur) {
    int count = 0;

    while (isIetfDigit(cur[count]))
        count++;
    return(count);
}

/* Read count digits as a number */
static int
readIetfNumber(const xmlChar **cur, int count) {
    int value = 0;
    int i;

    for (i = 0; i < count; i++) {
        value = value * 10 + (**cur - '0');
        (*cur)++;
    }
    return(value);
}

/* Read from minDigits to maxDigits digits, as daynum, hours, minutes,
 * seconds and year need. Returns the number of digits, or -1. */
static int
scanIetfNumber(const xmlChar **cur, int minDigits, int maxDigits,
               int *value) {
    int count = countIetfDigits(*cur);

    if ((count < minDigits) || (count > maxDigits))
        return(-1);
    *value = readIetfNumber(cur, count);
    return(count);
}

/* dsep ::= S | (S? "-" S?). Returns 0 or -1. */
static int
scanIetfSeparator(const xmlChar **cur) {
    int spaces = skipIetfSpace(cur);

    if (**cur == '-') {
        (*cur)++;
        skipIetfSpace(cur);
        return(0);
    }
    if (spaces == 0)
        return(-1);
    return(0);
}

/* monthname. Returns 0 or -1. */
static int
scanIetfMonth(const xmlChar **cur, xp3IetfDate *date) {
    int i;

    for (i = 0; i < 12; i++) {
        if (matchIetfWord(cur, xp3IetfMonthNames[i])) {
            date->month = i + 1;
            return(0);
        }
    }
    return(-1);
}

/* year ::= digit digit (digit digit)?. Returns 0 or -1. */
static int
scanIetfYear(const xmlChar **cur, xp3IetfDate *date) {
    int digits = scanIetfNumber(cur, 2, 4, &date->year);

    if ((digits != 2) && (digits != 4))
        return(-1);
    date->yearDigits = digits;
    return(0);
}

/* tzname. Returns 0 or -1. */
static int
scanIetfZoneName(const xmlChar **cur, int *offset) {
    size_t i;

    for (i = 0; i < sizeof(xp3IetfZones) / sizeof(xp3IetfZones[0]); i++) {
        if (matchIetfWord(cur, xp3IetfZones[i].name)) {
            *offset = xp3IetfZones[i].offset;
            return(0);
        }
    }
    return(-1);
}

/*
 * tzoffset ::= ("+"|"-") hours ":"? minutes?. With a colon, the hours
 * are before it and the minutes, if any, after it; without one, one to
 * four digits are H, HH, HMM or HHMM. The minutes default to 00.
 * Returns 0 or -1.
 */
static int
scanIetfOffset(const xmlChar **cur, xp3IetfDate *date) {
    int sign = 1;
    int digits;
    int hours = 0;
    int minutes = 0;

    if (**cur == '-')
        sign = -1;
    (*cur)++;
    digits = countIetfDigits(*cur);
    if ((*cur)[digits] == ':') {
        if ((digits < 1) || (digits > 2))
            return(-1);
        hours = readIetfNumber(cur, digits);
        (*cur)++;
        digits = countIetfDigits(*cur);
        if (digits == 2)
            minutes = readIetfNumber(cur, 2);
        else if (digits != 0)
            return(-1);
    } else if ((digits == 1) || (digits == 2)) {
        hours = readIetfNumber(cur, digits);
    } else if ((digits == 3) || (digits == 4)) {
        hours = readIetfNumber(cur, digits - 2);
        minutes = readIetfNumber(cur, 2);
    } else {
        return(-1);
    }
    date->timezoneMinutes = minutes;
    date->timezone = sign * (hours * 60 + minutes);
    return(0);
}

/* timezone ::= tzname | tzoffset (S? "(" S? tzname S? ")")?, where a
 * tzname after an offset is ignored. Returns 0 or -1. */
static int
scanIetfTimezone(const xmlChar **cur, xp3IetfDate *date) {
    const xmlChar *start;
    int ignored;

    if ((**cur != '+') && (**cur != '-'))
        return(scanIetfZoneName(cur, &date->timezone));
    if (scanIetfOffset(cur, date) < 0)
        return(-1);
    start = *cur;
    skipIetfSpace(cur);
    if (**cur != '(') {
        *cur = start;
        return(0);
    }
    (*cur)++;
    skipIetfSpace(cur);
    if (scanIetfZoneName(cur, &ignored) < 0)
        return(-1);
    skipIetfSpace(cur);
    if (**cur != ')')
        return(-1);
    (*cur)++;
    return(0);
}

/*
 * time ::= hours ":" minutes (":" seconds)? (S? timezone)?, with
 * seconds ::= digit digit ("." digit+)?. A timezone starts with a sign
 * or a letter, which tells it from the year of asctime after a space.
 * The fraction is truncated to nanoseconds. Returns 0 or -1.
 */
static int
scanIetfTime(const xmlChar **cur, xp3IetfDate *date) {
    const xmlChar *start;

    if (scanIetfNumber(cur, 1, 2, &date->hour) < 0)
        return(-1);
    if (**cur != ':')
        return(-1);
    (*cur)++;
    if (scanIetfNumber(cur, 2, 2, &date->minute) < 0)
        return(-1);
    if (**cur == ':') {
        (*cur)++;
        if (scanIetfNumber(cur, 2, 2, &date->second) < 0)
            return(-1);
        if (**cur == '.') {
            int digits = 0;

            (*cur)++;
            while (isIetfDigit(**cur)) {
                if (digits < 9)
                    date->nanos = date->nanos * 10 + (**cur - '0');
                digits++;
                (*cur)++;
            }
            if (digits == 0)
                return(-1);
            while (digits < 9) {
                date->nanos *= 10;
                digits++;
            }
        }
    }
    start = *cur;
    skipIetfSpace(cur);
    if ((**cur == '+') || (**cur == '-') || isIetfLetter(**cur))
        return(scanIetfTimezone(cur, date));
    *cur = start;
    return(0);
}

/*
 * input ::= S? (dayname ","? S)? ((datespec S time) | asctime) S?, with
 * datespec ::= daynum dsep monthname dsep year and
 * asctime ::= monthname dsep daynum S time S year.
 * Returns 0 or -1.
 */
static int
scanIetfDate(const xmlChar *str, xp3IetfDate *date) {
    const xmlChar *cur = str;
    size_t i;

    memset(date, 0, sizeof(*date));
    skipIetfSpace(&cur);
    for (i = 0; i < sizeof(xp3IetfDayNames) / sizeof(xp3IetfDayNames[0]);
         i++) {
        if (matchIetfWord(&cur, xp3IetfDayNames[i])) {
            if (*cur == ',')
                cur++;
            if (skipIetfSpace(&cur) == 0)
                return(-1);
            break;
        }
    }
    if (isIetfDigit(*cur)) {
        if (scanIetfNumber(&cur, 1, 2, &date->day) < 0)
            return(-1);
        if (scanIetfSeparator(&cur) < 0)
            return(-1);
        if (scanIetfMonth(&cur, date) < 0)
            return(-1);
        if (scanIetfSeparator(&cur) < 0)
            return(-1);
        if (scanIetfYear(&cur, date) < 0)
            return(-1);
        if (skipIetfSpace(&cur) == 0)
            return(-1);
        if (scanIetfTime(&cur, date) < 0)
            return(-1);
    } else {
        if (scanIetfMonth(&cur, date) < 0)
            return(-1);
        if (scanIetfSeparator(&cur) < 0)
            return(-1);
        if (scanIetfNumber(&cur, 1, 2, &date->day) < 0)
            return(-1);
        if (skipIetfSpace(&cur) == 0)
            return(-1);
        if (scanIetfTime(&cur, date) < 0)
            return(-1);
        if (skipIetfSpace(&cur) == 0)
            return(-1);
        if (scanIetfYear(&cur, date) < 0)
            return(-1);
    }
    skipIetfSpace(&cur);
    if (*cur != 0)
        return(-1);
    return(0);
}

/*
 * The xs:dateTime of what the input gives, by the rules of 9.9.1: a
 * two-digit year is in the 1900s, and a four-digit one is as written.
 * It must be a valid xs:dateTime: a day of the month, a time of day or
 * 24:00:00, which is midnight of the next day, and a timezone of at most
 * 14:00. Returns 0 or -1.
 */
static int
ietfToDateTime(const xp3IetfDate *date, xp3DateTime *value) {
    int year = date->year;
    int endOfDay = 0;

    memset(value, 0, sizeof(*value));
    if (date->yearDigits == 2)
        year += 1900;
    if ((date->day < 1) || (date->day > xp3DaysInMonth(year, date->month)))
        return(-1);
    if (date->hour == 24) {
        if ((date->minute != 0) || (date->second != 0) ||
            (date->nanos != 0))
            return(-1);
        endOfDay = 1;
    } else if (date->hour > 23) {
        return(-1);
    }
    if ((date->minute > 59) || (date->second > 59))
        return(-1);
    if ((date->timezoneMinutes > 59) ||
        (date->timezone > XP3_TIMEZONE_MAX) ||
        (date->timezone < -XP3_TIMEZONE_MAX))
        return(-1);
    value->year = year;
    value->month = (unsigned char) date->month;
    value->day = (unsigned char) date->day;
    value->minute = (unsigned char) date->minute;
    value->second = (unsigned char) date->second;
    value->nanosecond = date->nanos;
    value->hasTimezone = 1;
    value->timezone = (int16_t) date->timezone;
    if (endOfDay) {
        xp3DateTime start = *value;
        xp3Duration day;

        day.months = 0;
        day.seconds = XP3_SECONDS_PER_DAY;
        day.nanoseconds = 0;
        /* Years have at most four digits, so this never overflows */
        if (xp3DateTimeAddDuration(XP3_TYPE_DATE_TIME, &start, &day,
                                   value) != XP3_ERR_OK)
            return(-1);
    } else {
        value->hour = (unsigned char) date->hour;
    }
    return(0);
}

/**
 * fn:parse-ietf-date#1 (F&O 3.1, 9.9.1): the xs:dateTime of a date in
 * the formats of RFC 1123, RFC 850 or asctime(), with the timezone it
 * gives, or UTC without one. Anything the grammar doesn't match, or
 * that isn't a valid xs:dateTime, is FORG0010.
 *
 * @param call   the call
 * @param args   the string, xs:string?
 * @param count  1
 * @returns an xs:dateTime or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnParseIetfDate(xp3FuncCall *call, xp3Value **args, int count) {
    const xmlChar *str = xp3FuncString(args[0]);
    xp3IetfDate date;
    xp3DateTime value;

    (void) count;
    if (xp3ValueCount(args[0]) == 0)
        return(xp3ValueEmpty());
    if (xp3FuncSteps(call, 1) < 0)
        return(NULL);
    if (xp3FuncBytes(call, (uint64_t) strlen((const char *) str)) < 0)
        return(NULL);
    if ((scanIetfDate(str, &date) < 0) ||
        (ietfToDateTime(&date, &value) < 0)) {
        xp3FuncError(call, XP3_ERR_FORG0010, "fn:parse-ietf-date got a "
                     "string that isn't a valid date in the formats it "
                     "accepts");
        return(NULL);
    }
    return(xp3FuncAtomic(call, xp3AtomicNewDateTime(XP3_TYPE_DATE_TIME,
                                                    &value)));
}
