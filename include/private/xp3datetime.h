/**
 * @file
 *
 * @brief Dates, times and durations: values, calendar and clock
 * (internal)
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XP3DATETIME_H
#define XP3_XP3DATETIME_H

#include <stdint.h>

#include <libxml/xmlstring.h>

#include "private/xp3error.h"
#include "private/xp3types.h"

/* The largest year, and minus the smallest (doc/DESIGN.md 4.2) */
#define XP3_YEAR_MAX 2147483647

/* The largest timezone offset in minutes, 14:00 either way */
#define XP3_TIMEZONE_MAX 840

#define XP3_NANOS_PER_SECOND 1000000000

/*
 * A value of xs:dateTime, xs:dateTimeStamp, xs:date, xs:time or one of
 * the g* types: the seven properties of F&O 3.1, 9.2, with the second
 * split into whole seconds and nanoseconds. The type says which
 * properties are present; absent ones are 0. The value keeps its own
 * timezone and is normalized to UTC only to compare it.
 */
typedef struct {
    int32_t year;               /* within +-XP3_YEAR_MAX, 0 is 1 BCE */
    int32_t nanosecond;         /* 0 to 999,999,999 */
    int16_t timezone;           /* minutes east of UTC */
    unsigned char hasTimezone;
    unsigned char month;        /* 1 to 12 */
    unsigned char day;          /* 1 to 31 */
    unsigned char hour;         /* 0 to 23 */
    unsigned char minute;
    unsigned char second;
} xp3DateTime;

/*
 * A value of xs:duration, xs:yearMonthDuration or xs:dayTimeDuration
 * (F&O 3.1, 8): months and seconds, all three fields with the same
 * sign. None is INT64_MIN, so each can be negated.
 */
typedef struct {
    int64_t months;
    int64_t seconds;
    int32_t nanoseconds;        /* -999,999,999 to 999,999,999 */
} xp3Duration;

/* A moment in UTC: seconds since 0000-01-01T00:00:00Z, negative
 * before it, and the fraction. Values are compared by it. */
typedef struct {
    int64_t seconds;
    int32_t nanoseconds;        /* 0 to 999,999,999 */
} xp3Instant;

/* What reading a clock gives: the time since 1970-01-01T00:00:00Z and
 * the local offset at that time */
typedef struct {
    int64_t seconds;
    int32_t nanoseconds;
    int offset;                 /* minutes east of UTC */
} xp3ClockReading;

/* A clock to read instead of the system clock, for tests */
typedef void (*xp3ClockSource)(xp3ClockReading *reading);

/*
 * The current dateTime and the implicit timezone of one evaluation
 * (doc/DESIGN.md 5). The dynamic context may set either. A value that
 * isn't set comes from reading the clock, at most once and only when
 * it is needed; both then stay the same for the whole evaluation.
 * Constant folding has no dynamic context: there the clock isn't
 * available, and asking for either value fails.
 */
typedef struct {
    int available;              /* 0 while folding */
    int hasNow;
    xp3DateTime now;            /* an xs:dateTime with a timezone */
    int hasTimezone;
    int timezone;               /* minutes east of UTC */
    xp3ClockSource source;      /* NULL for the system clock */
} xp3Clock;

/*
 * Calendar: the proleptic Gregorian calendar with year 0, as XML
 * Schema 1.1 counts years
 */
int
xp3IsLeapYear(int64_t year);

int
xp3DaysInMonth(int64_t year, int month);

int64_t
xp3DayNumber(int64_t year, int month, int day);

void
xp3DayToDate(int64_t days, int64_t *year, int *month, int *day);

/*
 * Lexical forms
 */
xp3ErrCode
xp3DateTimeParse(xp3TypeId type, const xmlChar *str, xp3DateTime *value);

xp3ErrCode
xp3DurationParse(xp3TypeId type, const xmlChar *str, xp3Duration *value);

xmlChar *
xp3DateTimeToString(xp3TypeId type, const xp3DateTime *value);

xmlChar *
xp3DurationToString(xp3TypeId type, const xp3Duration *value);

/*
 * Casts between the date and time types, and between the durations
 */
xp3ErrCode
xp3DateTimeConvert(xp3TypeId source, const xp3DateTime *value,
                   xp3TypeId target, xp3DateTime *result);

void
xp3DurationConvert(const xp3Duration *value, xp3TypeId target,
                   xp3Duration *result);

/*
 * Comparing
 */
xp3Instant
xp3DateTimeInstant(xp3TypeId type, const xp3DateTime *value, int timezone);

int
xp3InstantCompare(const xp3Instant *left, const xp3Instant *right);

int
xp3DurationCompare(const xp3Duration *left, const xp3Duration *right);

/*
 * Arithmetic
 */
void
xp3DurationNegate(const xp3Duration *value, xp3Duration *result);

xp3ErrCode
xp3DurationAdd(const xp3Duration *left, const xp3Duration *right,
               xp3Duration *result);

xp3ErrCode
xp3DateTimeAddDuration(xp3TypeId type, const xp3DateTime *value,
                       const xp3Duration *duration, xp3DateTime *result);

xp3ErrCode
xp3InstantDifference(const xp3Instant *left, const xp3Instant *right,
                     xp3Duration *result);

/*
 * The clock
 */
void
xp3SystemClock(xp3ClockReading *reading);

xp3ErrCode
xp3ClockTimezone(xp3Clock *clock, int *minutes);

xp3ErrCode
xp3ClockNow(xp3Clock *clock, xp3DateTime *now);

#endif /* XP3_XP3DATETIME_H */
