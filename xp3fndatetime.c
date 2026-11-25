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
