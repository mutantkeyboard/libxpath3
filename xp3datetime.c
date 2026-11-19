/*
 * xp3datetime.c: dates, times and durations
 *
 * Values of the date and time types keep the seven properties of F&O
 * 3.1, 9.2 in fixed-width fields, and durations are months plus
 * seconds and nanoseconds (doc/DESIGN.md 4.2), so no value allocates
 * beyond its xp3Atomic. Years are within +-2,147,483,647 and seconds
 * have nanoseconds: digits beyond them are truncated, a year beyond
 * them is FODT0001, and a duration whose months or seconds don't fit
 * in 64 bits is FODT0002.
 *
 * The calendar is the proleptic Gregorian calendar with a year 0, as
 * XML Schema 1.1 counts years: 0000 is 1 BCE and a leap year, and
 * -0001 is followed by 0000. Lexical forms are those of XML Schema 1.1
 * Part 2, 3.3.6 to 3.3.14, and the canonical forms those of F&O 3.1,
 * 19.1.1.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

/* clock_gettime(), gettimeofday(), localtime_r() and gmtime_r() are
 * POSIX, which a strict C99 build doesn't declare without this */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
  #define _POSIX_C_SOURCE 200809L
#endif

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#elif !defined(XP3_HAVE_CLOCK_GETTIME) && defined(XP3_HAVE_GETTIMEOFDAY)
  #include <sys/time.h>
#endif

#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

#include "private/xp3datetime.h"

#define XP3_SECONDS_PER_DAY 86400

/* Days in 400 years, after which the calendar repeats */
#define XP3_DAYS_PER_400_YEARS 146097

/* Days in each month of a common year */
static const int xp3MonthDays[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/* Days of a common year before the first of each month */
static const int xp3DaysBeforeMonth[12] = {
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
};

/*
 * Calendar
 */

/* a / b rounded down, for b > 0 */
static int64_t
floorDiv(int64_t a, int64_t b) {
    int64_t quotient = a / b;

    if ((a % b != 0) && (a < 0))
        quotient--;
    return(quotient);
}

/**
 * Whether a year is a leap year: divisible by 4, and by 400 if it is
 * divisible by 100. Year 0 is one, and so are -4 and 4.
 *
 * @param year  the year
 * @returns 1 or 0
 */
int
xp3IsLeapYear(int64_t year) {
    if (year % 4 != 0)
        return(0);
    if (year % 100 != 0)
        return(1);
    return(year % 400 == 0);
}

/**
 * Get the number of days in a month.
 *
 * @param year   the year
 * @param month  the month, 1 to 12
 * @returns 28 to 31
 */
int
xp3DaysInMonth(int64_t year, int month) {
    if ((month == 2) && xp3IsLeapYear(year))
        return(29);
    return(xp3MonthDays[month - 1]);
}

/* Days from 0000-01-01 to January 1 of a year: 365 a year, plus a day
 * for each leap year from 0 up to the year before, or minus a day for
 * each leap year from the year up to -1 */
static int64_t
daysBeforeYear(int64_t year) {
    int64_t last = year - 1;
    int64_t leapYears = floorDiv(last, 4) - floorDiv(last, 100) +
                        floorDiv(last, 400) + 1;

    return(365 * year + leapYears);
}

/* Days of a year before the first of a month */
static int64_t
daysBeforeMonth(int month, int leap) {
    int64_t days = xp3DaysBeforeMonth[month - 1];

    if ((month > 2) && leap)
        days++;
    return(days);
}

/**
 * Count the days from 0000-01-01 to a date, negative before it.
 *
 * @param year   the year, within a few billion
 * @param month  the month, 1 to 12
 * @param day    the day, 1 to the days of the month
 * @returns the day number
 */
int64_t
xp3DayNumber(int64_t year, int month, int day) {
    return(daysBeforeYear(year) +
           daysBeforeMonth(month, xp3IsLeapYear(year)) + day - 1);
}

/**
 * Get the date of a day number, the reverse of xp3DayNumber().
 *
 * @param days   the days since 0000-01-01, negative before it, within
 *               a few billion years
 * @param year   set to the year
 * @param month  set to the month, 1 to 12
 * @param day    set to the day of the month
 */
void
xp3DayToDate(int64_t days, int64_t *year, int *month, int *day) {
    int64_t guess = floorDiv(days * 400, XP3_DAYS_PER_400_YEARS);
    int64_t dayOfYear;
    int leap;
    int monthIndex;

    /* The guess is at most a year off */
    while (daysBeforeYear(guess) > days)
        guess--;
    while (daysBeforeYear(guess + 1) <= days)
        guess++;
    dayOfYear = days - daysBeforeYear(guess);
    leap = xp3IsLeapYear(guess);
    monthIndex = 12;
    while ((monthIndex > 1) &&
           (daysBeforeMonth(monthIndex, leap) > dayOfYear))
        monthIndex--;
    *year = guess;
    *month = monthIndex;
    *day = (int) (dayOfYear - daysBeforeMonth(monthIndex, leap)) + 1;
}

/*
 * Reading lexical forms
 */

/* The properties of a type, in the order of its lexical form */
#define XP3_PART_YEAR (1 << 0)
#define XP3_PART_MONTH (1 << 1)
#define XP3_PART_DAY (1 << 2)
#define XP3_PART_TIME (1 << 3)

/* Which properties a type has besides the timezone, 0 for a type that
 * isn't a date or time type */
static int
typeParts(xp3TypeId type) {
    switch (type) {
        case XP3_TYPE_DATE_TIME:
        case XP3_TYPE_DATE_TIME_STAMP:
            return(XP3_PART_YEAR | XP3_PART_MONTH | XP3_PART_DAY |
                   XP3_PART_TIME);
        case XP3_TYPE_DATE:
            return(XP3_PART_YEAR | XP3_PART_MONTH | XP3_PART_DAY);
        case XP3_TYPE_TIME:
            return(XP3_PART_TIME);
        case XP3_TYPE_G_YEAR_MONTH:
            return(XP3_PART_YEAR | XP3_PART_MONTH);
        case XP3_TYPE_G_YEAR:
            return(XP3_PART_YEAR);
        case XP3_TYPE_G_MONTH_DAY:
            return(XP3_PART_MONTH | XP3_PART_DAY);
        case XP3_TYPE_G_DAY:
            return(XP3_PART_DAY);
        case XP3_TYPE_G_MONTH:
            return(XP3_PART_MONTH);
        default:
            return(0);
    }
}

/* A position in a lexical form */
typedef struct {
    const xmlChar *cur;
    int overflow;               /* a valid form, but beyond the limits
                                 * of the representation */
} xp3Scanner;

static int
isDigit(int c) {
    return((c >= '0') && (c <= '9'));
}

/* Read exactly count digits as a number. Returns it, or -1. */
static int
scanDigits(xp3Scanner *scan, int count) {
    int value = 0;
    int i;

    for (i = 0; i < count; i++) {
        if (!isDigit(scan->cur[i]))
            return(-1);
        value = value * 10 + (scan->cur[i] - '0');
    }
    scan->cur += count;
    return(value);
}

/* Skip a character if it is c. Returns 0, or -1 if it isn't there. */
static int
scanChar(xp3Scanner *scan, int c) {
    if (*scan->cur != c)
        return(-1);
    scan->cur++;
    return(0);
}

/* Skip count hyphens. Returns 0 or -1. */
static int
scanHyphens(xp3Scanner *scan, int count) {
    int i;

    for (i = 0; i < count; i++) {
        if (scanChar(scan, '-') < 0)
            return(-1);
    }
    return(0);
}

/*
 * Read the digits after a decimal point, at least one, truncated to
 * nanoseconds. Sets *zero if every digit is 0, which "24:00:00.000"
 * needs. Returns 0 or -1.
 */
static int
scanFraction(xp3Scanner *scan, int32_t *nanos, int *zero) {
    int32_t value = 0;
    size_t digits = 0;

    *zero = 1;
    while (isDigit(*scan->cur)) {
        int digit = *scan->cur - '0';

        if (digits < 9)
            value = value * 10 + digit;
        if (digit != 0)
            *zero = 0;
        digits++;
        scan->cur++;
    }
    if (digits == 0)
        return(-1);
    while (digits < 9) {
        value *= 10;
        digits++;
    }
    *nanos = value;
    return(0);
}

/*
 * Read a year: an optional minus sign and at least four digits, more
 * only without a leading zero, so "-0000" is year 0 and "02026" is
 * invalid. A year beyond XP3_YEAR_MAX sets overflow. *leap says
 * whether it is a leap year, from the year modulo 400, which is known
 * even then. Returns 0 or -1.
 */
static int
scanYear(xp3Scanner *scan, int32_t *year, int *leap) {
    const xmlChar *start;
    int negative = 0;
    int64_t value = 0;
    int remainder = 0;
    size_t digits = 0;

    if (*scan->cur == '-') {
        negative = 1;
        scan->cur++;
    }
    start = scan->cur;
    while (isDigit(*scan->cur)) {
        int digit = *scan->cur - '0';

        remainder = (remainder * 10 + digit) % 400;
        if (value <= XP3_YEAR_MAX)
            value = value * 10 + digit;
        digits++;
        scan->cur++;
    }
    if (digits < 4)
        return(-1);
    if ((digits > 4) && (*start == '0'))
        return(-1);
    if (negative)
        remainder = (400 - remainder) % 400;
    *leap = xp3IsLeapYear(remainder);
    if (value > XP3_YEAR_MAX) {
        scan->overflow = 1;
        value = 0;
    }
    if (negative)
        value = -value;
    *year = (int32_t) value;
    return(0);
}

/*
 * Read the date properties a type has: "yyyy-mm-dd", "yyyy-mm",
 * "yyyy", "--mm-dd", "---dd" or "--mm", checking the day against the
 * month. A gMonthDay has no year, so February has 29 days. Returns 0
 * or -1.
 */
static int
scanDate(xp3Scanner *scan, int parts, xp3DateTime *value) {
    int leap = 1;
    int maxDay = 31;
    int month;
    int day;

    if (parts & XP3_PART_YEAR) {
        if (scanYear(scan, &value->year, &leap) < 0)
            return(-1);
    } else if (parts & XP3_PART_MONTH) {
        if (scanHyphens(scan, 2) < 0)
            return(-1);
    } else if (parts & XP3_PART_DAY) {
        if (scanHyphens(scan, 3) < 0)
            return(-1);
    }
    if (parts & XP3_PART_MONTH) {
        if ((parts & XP3_PART_YEAR) && (scanChar(scan, '-') < 0))
            return(-1);
        month = scanDigits(scan, 2);
        if ((month < 1) || (month > 12))
            return(-1);
        value->month = (unsigned char) month;
        maxDay = xp3MonthDays[month - 1];
        if ((month == 2) && leap)
            maxDay = 29;
    }
    if (parts & XP3_PART_DAY) {
        if ((parts & XP3_PART_MONTH) && (scanChar(scan, '-') < 0))
            return(-1);
        day = scanDigits(scan, 2);
        if ((day < 1) || (day > maxDay))
            return(-1);
        value->day = (unsigned char) day;
    }
    return(0);
}

/*
 * Read "hh:mm:ss" with an optional fraction. "24:00:00" with a zero
 * fraction reads as 00:00:00 and sets *endOfDay. Returns 0 or -1.
 */
static int
scanTime(xp3Scanner *scan, xp3DateTime *value, int *endOfDay) {
    int32_t nanos = 0;
    int zero = 1;
    int hour;
    int minute;
    int second;

    *endOfDay = 0;
    hour = scanDigits(scan, 2);
    if ((hour < 0) || (scanChar(scan, ':') < 0))
        return(-1);
    minute = scanDigits(scan, 2);
    if ((minute < 0) || (scanChar(scan, ':') < 0))
        return(-1);
    second = scanDigits(scan, 2);
    if (second < 0)
        return(-1);
    if (*scan->cur == '.') {
        scan->cur++;
        if (scanFraction(scan, &nanos, &zero) < 0)
            return(-1);
    }
    if (hour == 24) {
        if ((minute != 0) || (second != 0) || !zero)
            return(-1);
        *endOfDay = 1;
        hour = 0;
    }
    if ((hour > 23) || (minute > 59) || (second > 59))
        return(-1);
    value->hour = (unsigned char) hour;
    value->minute = (unsigned char) minute;
    value->second = (unsigned char) second;
    value->nanosecond = nanos;
    return(0);
}

/*
 * Read an optional timezone: "Z", or "+hh:mm" or "-hh:mm" up to 14:00
 * (XML Schema 1.1, D.3.1), so "+14:01" is invalid. "-00:00" is UTC.
 * Returns 0 or -1.
 */
static int
scanTimezone(xp3Scanner *scan, xp3DateTime *value) {
    int sign = 1;
    int hours;
    int minutes;
    int offset;

    if (*scan->cur == 'Z') {
        scan->cur++;
        value->hasTimezone = 1;
        value->timezone = 0;
        return(0);
    }
    if (*scan->cur == '-')
        sign = -1;
    else if (*scan->cur != '+')
        return(0);
    scan->cur++;
    hours = scanDigits(scan, 2);
    if ((hours < 0) || (scanChar(scan, ':') < 0))
        return(-1);
    minutes = scanDigits(scan, 2);
    if ((minutes < 0) || (minutes > 59))
        return(-1);
    offset = hours * 60 + minutes;
    if (offset > XP3_TIMEZONE_MAX)
        return(-1);
    value->hasTimezone = 1;
    value->timezone = (int16_t) (sign * offset);
    return(0);
}

/* Move a date to the next day. Returns 0, or -1 past the last year. */
static int
nextDay(xp3DateTime *value) {
    if (value->day < xp3DaysInMonth(value->year, value->month)) {
        value->day++;
        return(0);
    }
    value->day = 1;
    if (value->month < 12) {
        value->month++;
        return(0);
    }
    value->month = 1;
    if (value->year == XP3_YEAR_MAX)
        return(-1);
    value->year++;
    return(0);
}

/**
 * Read the lexical form of a date or time type (XML Schema 1.1 Part 2,
 * 3.3.7 to 3.3.14, and 3.4.28 for xs:dateTimeStamp, which needs a
 * timezone). The string has no whitespace around it; casting collapses
 * it first. Fractional seconds are truncated to nanoseconds. 24:00:00
 * is 00:00:00 for xs:time, and midnight of the next day for
 * xs:dateTime.
 *
 * @param type   the type: xs:dateTime, xs:dateTimeStamp, xs:date,
 *               xs:time or a g* type
 * @param str    the lexical form
 * @param value  set to the value
 * @returns XP3_ERR_OK, XP3_ERR_FORG0001 for a string that isn't the
 *          lexical form, or XP3_ERR_FODT0001 for a year beyond
 *          XP3_YEAR_MAX either way
 */
xp3ErrCode
xp3DateTimeParse(xp3TypeId type, const xmlChar *str, xp3DateTime *value) {
    int parts = typeParts(type);
    int endOfDay = 0;
    xp3Scanner scan;

    memset(value, 0, sizeof(*value));
    scan.cur = str;
    scan.overflow = 0;
    if (parts == 0)
        return(XP3_ERR_FORG0001);
    if (scanDate(&scan, parts, value) < 0)
        return(XP3_ERR_FORG0001);
    if (parts & XP3_PART_TIME) {
        if ((parts & XP3_PART_DAY) && (scanChar(&scan, 'T') < 0))
            return(XP3_ERR_FORG0001);
        if (scanTime(&scan, value, &endOfDay) < 0)
            return(XP3_ERR_FORG0001);
    }
    if (scanTimezone(&scan, value) < 0)
        return(XP3_ERR_FORG0001);
    if (*scan.cur != 0)
        return(XP3_ERR_FORG0001);
    if ((type == XP3_TYPE_DATE_TIME_STAMP) && !value->hasTimezone)
        return(XP3_ERR_FORG0001);
    if (scan.overflow)
        return(XP3_ERR_FODT0001);
    if (endOfDay && (parts & XP3_PART_DAY) && (nextDay(value) < 0))
        return(XP3_ERR_FODT0001);
    return(XP3_ERR_OK);
}

/* The fields of a duration, in the order of its lexical form */
enum {
    XP3_DUR_YEARS = 0,
    XP3_DUR_MONTHS,
    XP3_DUR_DAYS,
    XP3_DUR_HOURS,
    XP3_DUR_MINUTES,
    XP3_DUR_SECONDS,
    XP3_DUR_FIELDS
};

/* Read an unsigned integer of any length. A value beyond int64_t sets
 * overflow. Returns the number of digits read. */
static size_t
scanNumber(xp3Scanner *scan, int64_t *value) {
    size_t digits = 0;

    *value = 0;
    while (isDigit(*scan->cur)) {
        int digit = *scan->cur - '0';

        if (*value > (INT64_MAX - digit) / 10)
            scan->overflow = 1;
        else
            *value = *value * 10 + digit;
        digits++;
        scan->cur++;
    }
    return(digits);
}

/* The field a designator stands for, before or after the T, or -1 */
static int
durationField(int designator, int inTime) {
    if (!inTime) {
        switch (designator) {
            case 'Y':
                return(XP3_DUR_YEARS);
            case 'M':
                return(XP3_DUR_MONTHS);
            case 'D':
                return(XP3_DUR_DAYS);
            default:
                return(-1);
        }
    }
    switch (designator) {
        case 'H':
            return(XP3_DUR_HOURS);
        case 'M':
            return(XP3_DUR_MINUTES);
        case 'S':
            return(XP3_DUR_SECONDS);
        default:
            return(-1);
    }
}

/* Add value * factor to *total, all of them not negative. Returns 0,
 * or -1 if the sum doesn't fit in int64_t. */
static int
addScaled(int64_t *total, int64_t value, int64_t factor) {
    if (value > (INT64_MAX - *total) / factor)
        return(-1);
    *total += value * factor;
    return(0);
}

/* The fields that only xs:yearMonthDuration or only
 * xs:dayTimeDuration may have, as bits */
#define XP3_DUR_YEAR_MONTH_BITS \
    ((1 << XP3_DUR_YEARS) | (1 << XP3_DUR_MONTHS))
#define XP3_DUR_DAY_TIME_BITS \
    ((1 << XP3_DUR_DAYS) | (1 << XP3_DUR_HOURS) | \
     (1 << XP3_DUR_MINUTES) | (1 << XP3_DUR_SECONDS))

/**
 * Read the lexical form of a duration type (XML Schema 1.1 Part 2,
 * 3.3.6, 3.4.26 and 3.4.27): an optional minus sign, P, then at least
 * one field in order, with T before the hours, minutes and seconds and
 * never at the end. Only the seconds may have a fraction, with digits
 * on both sides of the point, as the patterns of 3.3.6.2 require.
 * Fractional seconds are truncated to nanoseconds, and "-P0D" is
 * PT0S.
 *
 * @param type   xs:duration, xs:yearMonthDuration or
 *               xs:dayTimeDuration
 * @param str    the lexical form
 * @param value  set to the value
 * @returns XP3_ERR_OK, XP3_ERR_FORG0001 for a string that isn't the
 *          lexical form, or XP3_ERR_FODT0002 if the months or the
 *          seconds don't fit in 64 bits
 */
xp3ErrCode
xp3DurationParse(xp3TypeId type, const xmlChar *str, xp3Duration *value) {
    int64_t fields[XP3_DUR_FIELDS];
    int32_t nanos = 0;
    int next = XP3_DUR_YEARS;
    int present = 0;
    int inTime = 0;
    int timeFields = 0;
    int negative = 0;
    int64_t months = 0;
    int64_t seconds = 0;
    xp3Scanner scan;

    memset(value, 0, sizeof(*value));
    memset(fields, 0, sizeof(fields));
    scan.cur = str;
    scan.overflow = 0;
    if (*scan.cur == '-') {
        negative = 1;
        scan.cur++;
    }
    if (scanChar(&scan, 'P') < 0)
        return(XP3_ERR_FORG0001);
    while (*scan.cur != 0) {
        int64_t number;
        int hasFraction = 0;
        int field;

        if (*scan.cur == 'T') {
            if (inTime)
                return(XP3_ERR_FORG0001);
            inTime = 1;
            next = XP3_DUR_HOURS;
            scan.cur++;
            continue;
        }
        if (scanNumber(&scan, &number) == 0)
            return(XP3_ERR_FORG0001);
        if (*scan.cur == '.') {
            int zero;

            scan.cur++;
            if (scanFraction(&scan, &nanos, &zero) < 0)
                return(XP3_ERR_FORG0001);
            hasFraction = 1;
        }
        field = durationField(*scan.cur, inTime);
        if (field < next)
            return(XP3_ERR_FORG0001);
        if (hasFraction && (field != XP3_DUR_SECONDS))
            return(XP3_ERR_FORG0001);
        scan.cur++;
        fields[field] = number;
        present |= 1 << field;
        next = field + 1;
        if (inTime)
            timeFields++;
    }
    if ((present == 0) || (inTime && (timeFields == 0)))
        return(XP3_ERR_FORG0001);
    if (xp3TypeDerivesFrom(type, XP3_TYPE_YEAR_MONTH_DURATION) &&
        (present & XP3_DUR_DAY_TIME_BITS))
        return(XP3_ERR_FORG0001);
    if (xp3TypeDerivesFrom(type, XP3_TYPE_DAY_TIME_DURATION) &&
        (present & XP3_DUR_YEAR_MONTH_BITS))
        return(XP3_ERR_FORG0001);
    if (scan.overflow)
        return(XP3_ERR_FODT0002);

    if ((addScaled(&months, fields[XP3_DUR_YEARS], 12) < 0) ||
        (addScaled(&months, fields[XP3_DUR_MONTHS], 1) < 0) ||
        (addScaled(&seconds, fields[XP3_DUR_DAYS],
                   XP3_SECONDS_PER_DAY) < 0) ||
        (addScaled(&seconds, fields[XP3_DUR_HOURS], 3600) < 0) ||
        (addScaled(&seconds, fields[XP3_DUR_MINUTES], 60) < 0) ||
        (addScaled(&seconds, fields[XP3_DUR_SECONDS], 1) < 0))
        return(XP3_ERR_FODT0002);
    if (negative) {
        months = -months;
        seconds = -seconds;
        nanos = -nanos;
    }
    value->months = months;
    value->seconds = seconds;
    value->nanoseconds = nanos;
    return(XP3_ERR_OK);
}

/*
 * Canonical forms (F&O 3.1, 19.1.1)
 */

/* Room for the longest form, a negative duration of the most months
 * and seconds, which has about 60 characters */
#define XP3_TEXT_MAX 96

typedef struct {
    char text[XP3_TEXT_MAX];
    size_t len;
} xp3Text;

static void
textInit(xp3Text *out) {
    out->len = 0;
    out->text[0] = 0;
}

static void
appendChar(xp3Text *out, char c) {
    if (out->len + 1 < sizeof(out->text)) {
        out->text[out->len] = c;
        out->len++;
    }
    out->text[out->len] = 0;
}

static void
appendText(xp3Text *out, const char *text) {
    while (*text != 0) {
        appendChar(out, *text);
        text++;
    }
}

/* A number with at least minDigits digits, padded with zeros */
static void
appendNumber(xp3Text *out, uint64_t value, int minDigits) {
    char digits[24];
    int count = 0;

    do {
        digits[count] = (char) ('0' + value % 10);
        count++;
        value /= 10;
    } while (value > 0);
    while (count < minDigits) {
        digits[count] = '0';
        count++;
    }
    while (count > 0) {
        count--;
        appendChar(out, digits[count]);
    }
}

/* The fraction of a second as ".ddd" without trailing zeros, nothing
 * for none */
static void
appendFraction(xp3Text *out, int32_t nanos) {
    char digits[9];
    int count = 9;
    int i;

    if (nanos == 0)
        return;
    for (i = 8; i >= 0; i--) {
        digits[i] = (char) ('0' + nanos % 10);
        nanos /= 10;
    }
    while (digits[count - 1] == '0')
        count--;
    appendChar(out, '.');
    for (i = 0; i < count; i++)
        appendChar(out, digits[i]);
}

/* A year with at least four digits, and a minus sign before 0 */
static void
appendYear(xp3Text *out, int32_t year) {
    int64_t magnitude = year;

    if (year < 0) {
        appendChar(out, '-');
        magnitude = -magnitude;
    }
    appendNumber(out, (uint64_t) magnitude, 4);
}

/* Nothing without a timezone, Z for UTC, else +hh:mm or -hh:mm */
static void
appendTimezone(xp3Text *out, const xp3DateTime *value) {
    int offset = value->timezone;

    if (!value->hasTimezone)
        return;
    if (offset == 0) {
        appendChar(out, 'Z');
        return;
    }
    if (offset < 0) {
        appendChar(out, '-');
        offset = -offset;
    } else {
        appendChar(out, '+');
    }
    appendNumber(out, (uint64_t) (offset / 60), 2);
    appendChar(out, ':');
    appendNumber(out, (uint64_t) (offset % 60), 2);
}

/**
 * Get the canonical form of a date or time value (F&O 3.1, 19.1.1):
 * its local value with at least four digits of year, the seconds
 * without trailing zeros of the fraction, and Z for UTC.
 *
 * @param type   the type of the value
 * @param value  the value
 * @returns the string, to be freed with xmlFree(), or NULL if memory
 *          allocation failed
 */
xmlChar *
xp3DateTimeToString(xp3TypeId type, const xp3DateTime *value) {
    int parts = typeParts(type);
    xp3Text out;

    textInit(&out);
    if (parts & XP3_PART_YEAR)
        appendYear(&out, value->year);
    else if (parts & XP3_PART_MONTH)
        appendText(&out, "--");
    else if (parts & XP3_PART_DAY)
        appendText(&out, "---");
    if (parts & XP3_PART_MONTH) {
        if (parts & XP3_PART_YEAR)
            appendChar(&out, '-');
        appendNumber(&out, value->month, 2);
    }
    if (parts & XP3_PART_DAY) {
        if (parts & XP3_PART_MONTH)
            appendChar(&out, '-');
        appendNumber(&out, value->day, 2);
    }
    if (parts & XP3_PART_TIME) {
        if (parts & XP3_PART_DAY)
            appendChar(&out, 'T');
        appendNumber(&out, value->hour, 2);
        appendChar(&out, ':');
        appendNumber(&out, value->minute, 2);
        appendChar(&out, ':');
        appendNumber(&out, value->second, 2);
        appendFraction(&out, value->nanosecond);
    }
    appendTimezone(&out, value);
    return(xmlStrdup(BAD_CAST out.text));
}

/* Months as years and months, each only if it isn't 0 */
static void
appendYearMonth(xp3Text *out, uint64_t months) {
    uint64_t years = months / 12;
    uint64_t rest = months % 12;

    if (years > 0) {
        appendNumber(out, years, 1);
        appendChar(out, 'Y');
    }
    if (rest > 0) {
        appendNumber(out, rest, 1);
        appendChar(out, 'M');
    }
}

/* Seconds as days, hours, minutes and seconds, each only if it isn't
 * 0, with T before the time */
static void
appendDayTime(xp3Text *out, uint64_t seconds, int32_t nanos) {
    uint64_t days = seconds / XP3_SECONDS_PER_DAY;
    uint64_t hours = seconds % XP3_SECONDS_PER_DAY / 3600;
    uint64_t minutes = seconds % 3600 / 60;
    uint64_t rest = seconds % 60;

    if (days > 0) {
        appendNumber(out, days, 1);
        appendChar(out, 'D');
    }
    if ((hours == 0) && (minutes == 0) && (rest == 0) && (nanos == 0))
        return;
    appendChar(out, 'T');
    if (hours > 0) {
        appendNumber(out, hours, 1);
        appendChar(out, 'H');
    }
    if (minutes > 0) {
        appendNumber(out, minutes, 1);
        appendChar(out, 'M');
    }
    if ((rest > 0) || (nanos > 0)) {
        appendNumber(out, rest, 1);
        appendFraction(out, nanos);
        appendChar(out, 'S');
    }
}

/* The magnitude of a field of a duration, which is never INT64_MIN */
static uint64_t
magnitude(int64_t value) {
    if (value < 0)
        return((uint64_t) -value);
    return((uint64_t) value);
}

/**
 * Get the canonical form of a duration (F&O 3.1, 19.1.1): the months
 * as years and months, the seconds as days, hours, minutes and
 * seconds, leaving out the fields that are 0. A zero duration is
 * "P0M" for xs:yearMonthDuration and "PT0S" for the others.
 *
 * @param type   the type of the value
 * @param value  the value
 * @returns the string, to be freed with xmlFree(), or NULL if memory
 *          allocation failed
 */
xmlChar *
xp3DurationToString(xp3TypeId type, const xp3Duration *value) {
    uint64_t months = magnitude(value->months);
    uint64_t seconds = magnitude(value->seconds);
    int32_t nanos = value->nanoseconds;
    int negative = (value->months < 0) || (value->seconds < 0) ||
                   (value->nanoseconds < 0);
    int hasYearMonth;
    int hasDayTime;
    xp3Text out;

    if (nanos < 0)
        nanos = -nanos;
    hasYearMonth = (months > 0);
    hasDayTime = (seconds > 0) || (nanos > 0);
    textInit(&out);
    if (negative)
        appendChar(&out, '-');
    appendChar(&out, 'P');
    if (xp3TypeDerivesFrom(type, XP3_TYPE_YEAR_MONTH_DURATION)) {
        if (hasYearMonth)
            appendYearMonth(&out, months);
        else
            appendText(&out, "0M");
    } else if (!hasYearMonth && !hasDayTime) {
        appendText(&out, "T0S");
    } else {
        if (hasYearMonth)
            appendYearMonth(&out, months);
        if (hasDayTime)
            appendDayTime(&out, seconds, nanos);
    }
    return(xmlStrdup(BAD_CAST out.text));
}

/*
 * Casts
 */

/* Whether a date or time type can be cast to another (F&O 3.1, 19.1.3
 * and 19.1.4), by their primitive types: to itself, xs:dateTime to
 * xs:date, xs:time and the g* types, and xs:date to xs:dateTime and
 * the g* types */
static int
castAllowed(xp3TypeId from, xp3TypeId to) {
    int toGregorian = (to == XP3_TYPE_G_YEAR_MONTH) ||
                      (to == XP3_TYPE_G_YEAR) ||
                      (to == XP3_TYPE_G_MONTH_DAY) ||
                      (to == XP3_TYPE_G_DAY) || (to == XP3_TYPE_G_MONTH);

    if (from == to)
        return(1);
    if (from == XP3_TYPE_DATE_TIME)
        return((to == XP3_TYPE_DATE) || (to == XP3_TYPE_TIME) ||
               toGregorian);
    if (from == XP3_TYPE_DATE)
        return((to == XP3_TYPE_DATE_TIME) || toGregorian);
    return(0);
}

/**
 * Cast a date or time value to another date or time type (F&O 3.1,
 * 19.1.4): the properties the target has are kept as they are, in the
 * value's own timezone, and the others dropped; an xs:date becomes the
 * start of its day.
 *
 * @param source  the type of the value
 * @param value   the value
 * @param target  the date or time type to cast to
 * @param result  set to the result
 * @returns XP3_ERR_OK, XP3_ERR_XPTY0004 for a cast the table doesn't
 *          allow, or XP3_ERR_FORG0001 for an xs:dateTimeStamp without
 *          a timezone
 */
xp3ErrCode
xp3DateTimeConvert(xp3TypeId source, const xp3DateTime *value,
                   xp3TypeId target, xp3DateTime *result) {
    int parts = typeParts(target);

    if (!castAllowed(xp3TypePrimitive(source), xp3TypePrimitive(target)))
        return(XP3_ERR_XPTY0004);
    if ((target == XP3_TYPE_DATE_TIME_STAMP) && !value->hasTimezone)
        return(XP3_ERR_FORG0001);
    *result = *value;
    if (!(parts & XP3_PART_YEAR))
        result->year = 0;
    if (!(parts & XP3_PART_MONTH))
        result->month = 0;
    if (!(parts & XP3_PART_DAY))
        result->day = 0;
    if (!(parts & XP3_PART_TIME)) {
        result->hour = 0;
        result->minute = 0;
        result->second = 0;
        result->nanosecond = 0;
    }
    return(XP3_ERR_OK);
}

/**
 * Cast a duration to a duration type (F&O 3.1, 19.1.3): to
 * xs:yearMonthDuration without the seconds, to xs:dayTimeDuration
 * without the months, to xs:duration as it is.
 *
 * @param value   the value
 * @param target  the duration type
 * @param result  set to the result
 */
void
xp3DurationConvert(const xp3Duration *value, xp3TypeId target,
                   xp3Duration *result) {
    *result = *value;
    if (xp3TypeDerivesFrom(target, XP3_TYPE_YEAR_MONTH_DURATION)) {
        result->seconds = 0;
        result->nanoseconds = 0;
    }
    if (xp3TypeDerivesFrom(target, XP3_TYPE_DAY_TIME_DURATION))
        result->months = 0;
}

/*
 * Comparing
 */

/**
 * Get the starting instant of a date or time value in UTC, by which
 * values of one primitive type are compared (F&O 3.1, 9.4). Missing
 * properties come from the templates of 9.4: an xs:time is on
 * 1972-12-31, a gYear is xxxx-01-01, a gYearMonth xxxx-xx-01, a
 * gMonthDay 1972-xx-xx, a gMonth 1972-xx-01 and a gDay 1972-12-xx,
 * all at 00:00:00 unless there is a time.
 *
 * @param type      the type of the value
 * @param value     the value
 * @param timezone  the implicit timezone in minutes, for a value
 *                  without a timezone
 * @returns the instant
 */
xp3Instant
xp3DateTimeInstant(xp3TypeId type, const xp3DateTime *value, int timezone) {
    int parts = typeParts(type);
    int64_t year = 1972;
    int month = 12;
    int day = 31;
    int64_t seconds;
    xp3Instant instant;

    if (parts & XP3_PART_YEAR) {
        year = value->year;
        month = 1;
        day = 1;
    }
    if (parts & XP3_PART_MONTH) {
        month = value->month;
        day = 1;
    }
    if (parts & XP3_PART_DAY)
        day = value->day;
    if (value->hasTimezone)
        timezone = value->timezone;
    seconds = xp3DayNumber(year, month, day) * XP3_SECONDS_PER_DAY +
              value->hour * 3600 + value->minute * 60 + value->second;
    instant.seconds = seconds - (int64_t) timezone * 60;
    instant.nanoseconds = value->nanosecond;
    return(instant);
}

/**
 * Compare two instants.
 *
 * @param left   the first instant
 * @param right  the second instant
 * @returns -1, 0 or 1
 */
int
xp3InstantCompare(const xp3Instant *left, const xp3Instant *right) {
    if (left->seconds != right->seconds) {
        if (left->seconds < right->seconds)
            return(-1);
        return(1);
    }
    if (left->nanoseconds != right->nanoseconds) {
        if (left->nanoseconds < right->nanoseconds)
            return(-1);
        return(1);
    }
    return(0);
}

/**
 * Compare two durations by months, then seconds, then nanoseconds:
 * equality for any two durations (F&O 3.1, 8.2.5), and the order of
 * two xs:yearMonthDuration or two xs:dayTimeDuration values, which
 * have only months or only seconds (8.2.1 to 8.2.4). The order of
 * other durations means nothing.
 *
 * @param left   the first duration
 * @param right  the second duration
 * @returns -1, 0 or 1
 */
int
xp3DurationCompare(const xp3Duration *left, const xp3Duration *right) {
    xp3Instant leftTime;
    xp3Instant rightTime;

    if (left->months != right->months) {
        if (left->months < right->months)
            return(-1);
        return(1);
    }
    leftTime.seconds = left->seconds;
    leftTime.nanoseconds = left->nanoseconds;
    rightTime.seconds = right->seconds;
    rightTime.nanoseconds = right->nanoseconds;
    return(xp3InstantCompare(&leftTime, &rightTime));
}

/*
 * The clock
 */

#if defined(_WIN32) || defined(XP3_HAVE_LOCALTIME_R)

/* A broken-down time as seconds since 0000-01-01T00:00:00 */
static int64_t
tmSeconds(const struct tm *time) {
    int64_t days = xp3DayNumber((int64_t) time->tm_year + 1900,
                                time->tm_mon + 1, time->tm_mday);

    return(days * XP3_SECONDS_PER_DAY + time->tm_hour * 3600 +
           time->tm_min * 60 + time->tm_sec);
}

/* The local offset at a time, in minutes east of UTC, from the
 * reentrant variants only, which threads can call at once; 0 if it
 * can't be told or is beyond 14:00 */
static int
localOffset(time_t when) {
    struct tm local;
    struct tm utc;
    int64_t minutes;

  #if defined(_WIN32)
    if ((localtime_s(&local, &when) != 0) || (gmtime_s(&utc, &when) != 0))
        return(0);
  #else
    if ((localtime_r(&when, &local) == NULL) ||
        (gmtime_r(&when, &utc) == NULL))
        return(0);
  #endif
    minutes = (tmSeconds(&local) - tmSeconds(&utc)) / 60;
    if ((minutes > XP3_TIMEZONE_MAX) || (minutes < -XP3_TIMEZONE_MAX))
        return(0);
    return((int) minutes);
}

#else

/* Without localtime_r(), UTC: localtime() shares its result between
 * threads */
static int
localOffset(time_t when) {
    (void) when;
    return(0);
}

#endif

/* Seconds from 1601-01-01, where Windows counts from, to 1970-01-01 */
#define XP3_FILETIME_UNIX_OFFSET INT64_C(11644473600)

/**
 * Read the system clock, to microseconds, and the local offset at that
 * moment.
 *
 * @param reading  set to the time and the offset
 */
void
xp3SystemClock(xp3ClockReading *reading) {
#if defined(_WIN32)
    FILETIME fileTime;
    ULARGE_INTEGER ticks;
#elif defined(XP3_HAVE_CLOCK_GETTIME)
    struct timespec now;
#elif defined(XP3_HAVE_GETTIMEOFDAY)
    struct timeval now;
#endif

    memset(reading, 0, sizeof(*reading));
#if defined(_WIN32)
  #if defined(_WIN32_WINNT) && (_WIN32_WINNT >= 0x0602)
    GetSystemTimePreciseAsFileTime(&fileTime);
  #else
    GetSystemTimeAsFileTime(&fileTime);
  #endif
    ticks.LowPart = fileTime.dwLowDateTime;
    ticks.HighPart = fileTime.dwHighDateTime;
    /* Ticks of 100 nanoseconds */
    reading->seconds = (int64_t) (ticks.QuadPart / 10000000) -
                       XP3_FILETIME_UNIX_OFFSET;
    reading->nanoseconds = (int32_t) (ticks.QuadPart % 10000000) * 100;
#elif defined(XP3_HAVE_CLOCK_GETTIME)
    if (clock_gettime(CLOCK_REALTIME, &now) == 0) {
        reading->seconds = (int64_t) now.tv_sec;
        reading->nanoseconds = (int32_t) now.tv_nsec;
    } else {
        reading->seconds = (int64_t) time(NULL);
    }
#elif defined(XP3_HAVE_GETTIMEOFDAY)
    gettimeofday(&now, NULL);
    reading->seconds = (int64_t) now.tv_sec;
    reading->nanoseconds = (int32_t) now.tv_usec * 1000;
#else
    reading->seconds = (int64_t) time(NULL);
#endif
    reading->nanoseconds -= reading->nanoseconds % 1000;
    reading->offset = localOffset((time_t) reading->seconds);
}

/* A clock reading as an xs:dateTime in the local offset */
static void
readingToDateTime(const xp3ClockReading *reading, xp3DateTime *value) {
    int64_t local = reading->seconds + (int64_t) reading->offset * 60;
    int64_t days = floorDiv(local, XP3_SECONDS_PER_DAY);
    int64_t secondOfDay = local - days * XP3_SECONDS_PER_DAY;
    int64_t year;
    int month;
    int day;

    xp3DayToDate(days + xp3DayNumber(1970, 1, 1), &year, &month, &day);
    memset(value, 0, sizeof(*value));
    value->year = (int32_t) year;
    value->month = (unsigned char) month;
    value->day = (unsigned char) day;
    value->hour = (unsigned char) (secondOfDay / 3600);
    value->minute = (unsigned char) (secondOfDay % 3600 / 60);
    value->second = (unsigned char) (secondOfDay % 60);
    value->nanosecond = reading->nanoseconds;
    value->hasTimezone = 1;
    value->timezone = (int16_t) reading->offset;
}

/**
 * Get the implicit timezone of an evaluation: the one the dynamic
 * context set, else the timezone of the current dateTime it set, else
 * the local offset when the clock is read, which happens once.
 *
 * @param clock    the clock of the evaluation, or NULL for none
 * @param minutes  set to the timezone in minutes east of UTC
 * @returns XP3_ERR_OK, or XP3_ERR_NEEDS_CONTEXT without a clock or
 *          while folding
 */
xp3ErrCode
xp3ClockTimezone(xp3Clock *clock, int *minutes) {
    *minutes = 0;
    if ((clock == NULL) || !clock->available)
        return(XP3_ERR_NEEDS_CONTEXT);
    if (!clock->hasTimezone) {
        if (!clock->hasNow) {
            xp3ClockReading reading;

            if (clock->source != NULL)
                clock->source(&reading);
            else
                xp3SystemClock(&reading);
            readingToDateTime(&reading, &clock->now);
            clock->hasNow = 1;
        }
        clock->timezone = clock->now.timezone;
        clock->hasTimezone = 1;
    }
    *minutes = clock->timezone;
    return(XP3_ERR_OK);
}
