/*
 * testdatetime.c: tests for dates, times and durations
 *
 * Lexical forms and canonical forms of the 13 types, the calendar,
 * casts between the types, comparisons with and without timezones, the
 * clock, and a random property test: every value reads back from its
 * canonical form, comparisons agree with instants computed here by a
 * separate, slower calendar, and fn:distinct-values agrees with eq.
 * Expected comparisons are the examples of F&O 3.1, 8.2 and 9.4.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/xmlmemory.h>

#include <libxpath3/xpath3.h>
#include "private/xp3context.h"
#include "private/xp3datetime.h"
#include "private/xp3eval.h"
#include "private/xp3ops.h"
#include "private/xp3types.h"
#include "private/xp3value.h"
#include "testalloc.h"
#include "testtap.h"

static void
check(int cond, const char *what) {
    xp3TestCheck(cond, what);
}

static const char *
codeName(xp3ErrCode code) {
    const char *name = xp3ErrCodeName(code);

    if (name == NULL)
        return("?");
    return(name);
}

/* Cast a lexical form to a type, as from xs:string. Sets *code. */
static xp3Atomic *
castString(xp3TypeId type, const char *lexical, xp3ErrCode *code) {
    xp3Atomic *str = xp3AtomicNewString(XP3_TYPE_STRING, BAD_CAST lexical,
                                        strlen(lexical));
    xp3Atomic *result = NULL;

    if (str == NULL) {
        *code = XP3_ERR_NO_MEMORY;
        return(NULL);
    }
    *code = xp3CastAtomic(str, type, &result);
    xp3AtomicUnref(str);
    return(result);
}

/* A value that must be valid */
static xp3Atomic *
value(xp3TypeId type, const char *lexical) {
    xp3ErrCode code;
    xp3Atomic *atomic = castString(type, lexical, &code);

    if (atomic == NULL)
        xp3TestFail("xs:%s('%s') gave %s",
                    (const char *) xp3TypeLocalName(type), lexical,
                    codeName(code));
    return(atomic);
}

/* A clock that knows the implicit timezone and is never read */
static void
fixedClock(xp3Clock *clock, int timezone) {
    memset(clock, 0, sizeof(*clock));
    clock->available = 1;
    clock->hasTimezone = 1;
    clock->timezone = timezone;
}

/*
 * Lexical and canonical forms
 */

typedef struct {
    xp3TypeId type;
    const char *input;
    const char *canonical;
} xp3LexicalCase;

static const xp3LexicalCase lexicalCases[] = {
    { XP3_TYPE_DATE_TIME, "2026-11-19T11:25:47.050651+01:00",
      "2026-11-19T11:25:47.050651+01:00" },
    { XP3_TYPE_DATE_TIME, "2000-01-01T00:00:00.000Z",
      "2000-01-01T00:00:00Z" },
    { XP3_TYPE_DATE_TIME, "1999-12-31T24:00:00", "2000-01-01T00:00:00" },
    { XP3_TYPE_DATE_TIME, "2000-02-28T24:00:00.0", "2000-02-29T00:00:00" },
    { XP3_TYPE_DATE_TIME, "1900-02-28T24:00:00Z", "1900-03-01T00:00:00Z" },
    { XP3_TYPE_DATE_TIME, "-0001-12-31T24:00:00", "0000-01-01T00:00:00" },
    { XP3_TYPE_DATE_TIME, "2000-01-01T12:00:00-00:00",
      "2000-01-01T12:00:00Z" },
    { XP3_TYPE_DATE_TIME, "-0001-12-31T23:59:59.5+14:00",
      "-0001-12-31T23:59:59.5+14:00" },
    { XP3_TYPE_DATE_TIME, "-0000-01-01T00:00:00-14:00",
      "0000-01-01T00:00:00-14:00" },
    { XP3_TYPE_DATE_TIME, "12345-06-07T08:09:10.1234567890",
      "12345-06-07T08:09:10.123456789" },
    { XP3_TYPE_DATE_TIME, "2147483647-12-31T23:59:59.999999999999Z",
      "2147483647-12-31T23:59:59.999999999Z" },
    { XP3_TYPE_DATE_TIME, "-2147483647-01-01T00:00:00",
      "-2147483647-01-01T00:00:00" },
    { XP3_TYPE_DATE_TIME_STAMP, "2026-01-01T00:00:00+05:30",
      "2026-01-01T00:00:00+05:30" },
    { XP3_TYPE_DATE, "2000-02-29", "2000-02-29" },
    { XP3_TYPE_DATE, "1900-02-28", "1900-02-28" },
    { XP3_TYPE_DATE, "0000-02-29", "0000-02-29" },
    { XP3_TYPE_DATE, "-0004-02-29Z", "-0004-02-29Z" },
    { XP3_TYPE_DATE, "2026-01-01-14:00", "2026-01-01-14:00" },
    { XP3_TYPE_TIME, "24:00:00", "00:00:00" },
    { XP3_TYPE_TIME, "24:00:00.000+01:00", "00:00:00+01:00" },
    { XP3_TYPE_TIME, "23:59:59.9999999999", "23:59:59.999999999" },
    { XP3_TYPE_TIME, "00:00:00.1000-00:00", "00:00:00.1Z" },
    { XP3_TYPE_TIME, "12:00:00+14:00", "12:00:00+14:00" },
    { XP3_TYPE_G_YEAR_MONTH, "2026-11Z", "2026-11Z" },
    { XP3_TYPE_G_YEAR_MONTH, "-0001-02", "-0001-02" },
    { XP3_TYPE_G_YEAR, "2026", "2026" },
    { XP3_TYPE_G_YEAR, "-0000", "0000" },
    { XP3_TYPE_G_YEAR, "2026+05:30", "2026+05:30" },
    { XP3_TYPE_G_MONTH_DAY, "--02-29", "--02-29" },
    { XP3_TYPE_G_MONTH_DAY, "--12-25-14:00", "--12-25-14:00" },
    { XP3_TYPE_G_DAY, "---31", "---31" },
    { XP3_TYPE_G_DAY, "---01Z", "---01Z" },
    { XP3_TYPE_G_MONTH, "--12", "--12" },
    { XP3_TYPE_G_MONTH, "--01+01:00", "--01+01:00" },
    { XP3_TYPE_DURATION, "P1Y2M3DT4H5M6.7S", "P1Y2M3DT4H5M6.7S" },
    { XP3_TYPE_DURATION, "-P0D", "PT0S" },
    { XP3_TYPE_DURATION, "P0Y", "PT0S" },
    { XP3_TYPE_DURATION, "P14M", "P1Y2M" },
    { XP3_TYPE_DURATION, "PT36H", "P1DT12H" },
    { XP3_TYPE_DURATION, "P1YT0S", "P1Y" },
    { XP3_TYPE_DURATION, "-PT1.5S", "-PT1.5S" },
    { XP3_TYPE_DURATION, "PT0.0000000001S", "PT0S" },
    { XP3_TYPE_DURATION, "PT1.123456789123S", "PT1.123456789S" },
    { XP3_TYPE_DURATION, "P768614336404564650Y7M",
      "P768614336404564650Y7M" },
    { XP3_TYPE_DURATION, "PT9223372036854775807.999999999S",
      "P106751991167300DT15H30M7.999999999S" },
    { XP3_TYPE_YEAR_MONTH_DURATION, "P0M", "P0M" },
    { XP3_TYPE_YEAR_MONTH_DURATION, "P0Y", "P0M" },
    { XP3_TYPE_YEAR_MONTH_DURATION, "-P13M", "-P1Y1M" },
    { XP3_TYPE_DAY_TIME_DURATION, "P0D", "PT0S" },
    { XP3_TYPE_DAY_TIME_DURATION, "PT90M", "PT1H30M" },
    { XP3_TYPE_DAY_TIME_DURATION, "PT3600.5S", "PT1H0.5S" },
    { XP3_TYPE_DAY_TIME_DURATION, "-P1DT0.000000001S",
      "-P1DT0.000000001S" },
    /* Whitespace is collapsed as for other casts */
    { XP3_TYPE_DATE, " \t2026-01-01\n", "2026-01-01" },
    { XP3_TYPE_DAY_TIME_DURATION, " PT1S ", "PT1S" }
};

static void
testLexicalForms(void) {
    size_t i;

    for (i = 0; i < sizeof(lexicalCases) / sizeof(lexicalCases[0]); i++) {
        const xp3LexicalCase *test = &lexicalCases[i];
        xp3Atomic *atomic = value(test->type, test->input);
        xmlChar *str;

        if (atomic == NULL)
            continue;
        str = xp3AtomicToString(atomic);
        if ((str == NULL) ||
            (strcmp((const char *) str, test->canonical) != 0))
            xp3TestFail("xs:%s('%s') is %s, expected %s",
                        (const char *) xp3TypeLocalName(test->type),
                        test->input, (const char *) str, test->canonical);
        xmlFree(str);
        xp3AtomicUnref(atomic);
    }
}

typedef struct {
    xp3TypeId type;
    const char *input;
    xp3ErrCode code;
} xp3InvalidCase;

static const xp3InvalidCase invalidCases[] = {
    { XP3_TYPE_DATE_TIME, "", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "2026-01-01", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "02026-01-01T00:00:00", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "026-01-01T00:00:00", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "+2026-01-01T00:00:00", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "2026-1-01T00:00:00", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "2026-02-29T00:00:00", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "1900-02-29T00:00:00", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "-0001-02-29T00:00:00", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "2026-04-31T00:00:00", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "2026-13-01T00:00:00", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "2026-00-01T00:00:00", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "2026-01-32T00:00:00", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "2026-01-01t00:00:00", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "2026-01-01T24:00:01", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "2026-01-01T24:00:00.001", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "2026-01-01T23:60:00", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "2026-01-01T23:00:60", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "2026-01-01T00:00:00.", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "2026-01-01T00:00:00+14:01", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "2026-01-01T00:00:00+15:00", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "2026-01-01T00:00:00+01", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "2026-01-01T00:00:00+01:60", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "2026-01-01T00:00:00Zz", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME, "2026-01-01 T00:00:00", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE_TIME_STAMP, "2026-01-01T00:00:00", XP3_ERR_FORG0001 },
    { XP3_TYPE_DATE, "2026-01-01T00:00:00", XP3_ERR_FORG0001 },
    { XP3_TYPE_TIME, "1:00:00", XP3_ERR_FORG0001 },
    { XP3_TYPE_TIME, "24:00", XP3_ERR_FORG0001 },
    { XP3_TYPE_G_YEAR_MONTH, "2026-13", XP3_ERR_FORG0001 },
    { XP3_TYPE_G_YEAR, "26", XP3_ERR_FORG0001 },
    { XP3_TYPE_G_MONTH_DAY, "--02-30", XP3_ERR_FORG0001 },
    { XP3_TYPE_G_MONTH_DAY, "--13-01", XP3_ERR_FORG0001 },
    { XP3_TYPE_G_DAY, "---32", XP3_ERR_FORG0001 },
    { XP3_TYPE_G_DAY, "--31", XP3_ERR_FORG0001 },
    { XP3_TYPE_G_MONTH, "--00", XP3_ERR_FORG0001 },
    { XP3_TYPE_G_MONTH, "--12--", XP3_ERR_FORG0001 },
    { XP3_TYPE_DURATION, "P", XP3_ERR_FORG0001 },
    { XP3_TYPE_DURATION, "-P", XP3_ERR_FORG0001 },
    { XP3_TYPE_DURATION, "PT", XP3_ERR_FORG0001 },
    { XP3_TYPE_DURATION, "P1YT", XP3_ERR_FORG0001 },
    { XP3_TYPE_DURATION, "P1D1Y", XP3_ERR_FORG0001 },
    { XP3_TYPE_DURATION, "P1Y1Y", XP3_ERR_FORG0001 },
    { XP3_TYPE_DURATION, "PT1H1H", XP3_ERR_FORG0001 },
    { XP3_TYPE_DURATION, "PT1S1M", XP3_ERR_FORG0001 },
    { XP3_TYPE_DURATION, "PT1HT1M", XP3_ERR_FORG0001 },
    { XP3_TYPE_DURATION, "P1.5D", XP3_ERR_FORG0001 },
    { XP3_TYPE_DURATION, "PT.5S", XP3_ERR_FORG0001 },
    { XP3_TYPE_DURATION, "PT1.S", XP3_ERR_FORG0001 },
    { XP3_TYPE_DURATION, "P-1D", XP3_ERR_FORG0001 },
    { XP3_TYPE_DURATION, "+P1D", XP3_ERR_FORG0001 },
    { XP3_TYPE_DURATION, "1Y", XP3_ERR_FORG0001 },
    { XP3_TYPE_DURATION, "P1H", XP3_ERR_FORG0001 },
    { XP3_TYPE_DURATION, "PT1D", XP3_ERR_FORG0001 },
    { XP3_TYPE_YEAR_MONTH_DURATION, "P1D", XP3_ERR_FORG0001 },
    { XP3_TYPE_YEAR_MONTH_DURATION, "P1YT1S", XP3_ERR_FORG0001 },
    { XP3_TYPE_DAY_TIME_DURATION, "P1Y", XP3_ERR_FORG0001 },
    { XP3_TYPE_DAY_TIME_DURATION, "P0MT1S", XP3_ERR_FORG0001 },
    /* Valid forms beyond the limits */
    { XP3_TYPE_DATE, "2147483648-01-01", XP3_ERR_FODT0001 },
    { XP3_TYPE_DATE_TIME, "-2147483648-01-01T00:00:00", XP3_ERR_FODT0001 },
    { XP3_TYPE_DATE_TIME, "2147483647-12-31T24:00:00", XP3_ERR_FODT0001 },
    { XP3_TYPE_G_YEAR, "99999999999999999999", XP3_ERR_FODT0001 },
    { XP3_TYPE_DURATION, "PT9223372036854775808S", XP3_ERR_FODT0002 },
    { XP3_TYPE_DURATION, "P768614336404564650Y8M", XP3_ERR_FODT0002 },
    { XP3_TYPE_DAY_TIME_DURATION, "P106751991167301D", XP3_ERR_FODT0002 },
    { XP3_TYPE_YEAR_MONTH_DURATION, "P99999999999999999999Y",
      XP3_ERR_FODT0002 },
    /* An invalid form is FORG0001 also beyond the limits */
    { XP3_TYPE_DATE, "2147483648-02-30", XP3_ERR_FORG0001 },
    { XP3_TYPE_DURATION, "P99999999999999999999YT", XP3_ERR_FORG0001 }
};

static void
testInvalidForms(void) {
    size_t i;

    for (i = 0; i < sizeof(invalidCases) / sizeof(invalidCases[0]); i++) {
        const xp3InvalidCase *test = &invalidCases[i];
        xp3ErrCode code;
        xp3Atomic *atomic = castString(test->type, test->input, &code);

        if ((atomic != NULL) || (code != test->code))
            xp3TestFail("xs:%s('%s') gave %s, expected %s",
                        (const char *) xp3TypeLocalName(test->type),
                        test->input, codeName(code), codeName(test->code));
        xp3AtomicUnref(atomic);
    }
}

/*
 * Calendar
 */

static void
testCalendar(void) {
    int64_t day;
    int64_t year;
    int month;
    int dayOfMonth;
    int bad = 0;

    check(!xp3IsLeapYear(1900), "1900 is not a leap year");
    check(xp3IsLeapYear(2000), "2000 is a leap year");
    check(xp3IsLeapYear(0), "year 0 is a leap year");
    check(!xp3IsLeapYear(-1), "-1 is not a leap year");
    check(xp3IsLeapYear(-4), "-4 is a leap year");
    check(!xp3IsLeapYear(-100) && xp3IsLeapYear(-400),
          "-100 is not a leap year, -400 is");
    check(xp3DaysInMonth(2000, 2) == 29, "February 2000 has 29 days");
    check(xp3DaysInMonth(1900, 2) == 28, "February 1900 has 28 days");
    check(xp3DaysInMonth(0, 2) == 29, "February 0000 has 29 days");
    check(xp3DaysInMonth(2026, 4) == 30, "April has 30 days");

    check(xp3DayNumber(0, 1, 1) == 0, "0000-01-01 is day 0");
    check(xp3DayNumber(1, 1, 1) == 366, "year 0 has 366 days");
    check(xp3DayNumber(-1, 12, 31) == -1, "-0001-12-31 is the day before");
    check(xp3DayNumber(-4, 1, 1) == -1461, "four years back");
    check(xp3DayNumber(1970, 1, 1) == 719528, "1970-01-01");

    /* Every day of ten thousand years around year 0, and both ends */
    for (day = -1830000; day < 1830000; day++) {
        xp3DayToDate(day, &year, &month, &dayOfMonth);
        if (xp3DayNumber(year, month, dayOfMonth) != day)
            bad++;
    }
    check(bad == 0, "every day number reads back from its date");
    day = xp3DayNumber(XP3_YEAR_MAX, 12, 31);
    xp3DayToDate(day, &year, &month, &dayOfMonth);
    check((year == XP3_YEAR_MAX) && (month == 12) && (dayOfMonth == 31),
          "the last day");
    day = xp3DayNumber(-XP3_YEAR_MAX, 1, 1);
    xp3DayToDate(day, &year, &month, &dayOfMonth);
    check((year == -XP3_YEAR_MAX) && (month == 1) && (dayOfMonth == 1),
          "the first day");
}

/*
 * Casts between the types
 */

typedef struct {
    xp3TypeId source;
    const char *input;
    xp3TypeId target;
    const char *expected;       /* or the error code */
} xp3CastCase;

static const xp3CastCase castCases[] = {
    { XP3_TYPE_DATE_TIME, "2026-11-19T11:25:47.5+01:00", XP3_TYPE_DATE,
      "2026-11-19+01:00" },
    { XP3_TYPE_DATE_TIME, "2026-11-19T11:25:47.5+01:00", XP3_TYPE_TIME,
      "11:25:47.5+01:00" },
    { XP3_TYPE_DATE_TIME, "2026-11-19T11:25:47.5", XP3_TYPE_G_YEAR_MONTH,
      "2026-11" },
    { XP3_TYPE_DATE_TIME, "2026-11-19T11:25:47Z", XP3_TYPE_G_YEAR,
      "2026Z" },
    { XP3_TYPE_DATE_TIME, "2026-11-19T11:25:47Z", XP3_TYPE_G_MONTH_DAY,
      "--11-19Z" },
    { XP3_TYPE_DATE_TIME, "2026-11-19T11:25:47Z", XP3_TYPE_G_DAY,
      "---19Z" },
    { XP3_TYPE_DATE_TIME, "2026-11-19T11:25:47Z", XP3_TYPE_G_MONTH,
      "--11Z" },
    { XP3_TYPE_DATE_TIME, "2026-11-19T11:25:47Z",
      XP3_TYPE_DATE_TIME_STAMP, "2026-11-19T11:25:47Z" },
    { XP3_TYPE_DATE_TIME, "2026-11-19T11:25:47", XP3_TYPE_DATE_TIME_STAMP,
      "FORG0001" },
    { XP3_TYPE_DATE_TIME_STAMP, "2026-11-19T11:25:47Z", XP3_TYPE_DATE,
      "2026-11-19Z" },
    { XP3_TYPE_DATE, "2026-11-19", XP3_TYPE_DATE_TIME,
      "2026-11-19T00:00:00" },
    { XP3_TYPE_DATE, "2026-11-19-05:00", XP3_TYPE_DATE_TIME_STAMP,
      "2026-11-19T00:00:00-05:00" },
    { XP3_TYPE_DATE, "2026-11-19", XP3_TYPE_G_MONTH_DAY, "--11-19" },
    { XP3_TYPE_DATE, "2026-11-19", XP3_TYPE_TIME, "XPTY0004" },
    { XP3_TYPE_TIME, "12:00:00", XP3_TYPE_DATE_TIME, "XPTY0004" },
    { XP3_TYPE_G_YEAR, "2026", XP3_TYPE_DATE, "XPTY0004" },
    { XP3_TYPE_G_MONTH, "--11", XP3_TYPE_G_MONTH_DAY, "XPTY0004" },
    { XP3_TYPE_DATE, "2026-11-19", XP3_TYPE_DURATION, "XPTY0004" },
    { XP3_TYPE_DURATION, "P1Y2M3DT4H", XP3_TYPE_YEAR_MONTH_DURATION,
      "P1Y2M" },
    { XP3_TYPE_DURATION, "-P1Y2M3DT4H", XP3_TYPE_DAY_TIME_DURATION,
      "-P3DT4H" },
    { XP3_TYPE_YEAR_MONTH_DURATION, "P1Y", XP3_TYPE_DAY_TIME_DURATION,
      "PT0S" },
    { XP3_TYPE_DAY_TIME_DURATION, "P1D", XP3_TYPE_YEAR_MONTH_DURATION,
      "P0M" },
    { XP3_TYPE_DAY_TIME_DURATION, "P1D", XP3_TYPE_DURATION, "P1D" },
    { XP3_TYPE_YEAR_MONTH_DURATION, "P0M", XP3_TYPE_DURATION, "PT0S" },
    { XP3_TYPE_DURATION, "P1D", XP3_TYPE_DATE, "XPTY0004" },
    { XP3_TYPE_DURATION, "P1D", XP3_TYPE_DOUBLE, "XPTY0004" },
    { XP3_TYPE_DATE, "2026-11-19", XP3_TYPE_BOOLEAN, "XPTY0004" },
    { XP3_TYPE_DATE, "2026-11-19Z", XP3_TYPE_UNTYPED_ATOMIC,
      "2026-11-19Z" },
    { XP3_TYPE_DURATION, "PT36H", XP3_TYPE_STRING, "P1DT12H" },
    { XP3_TYPE_TIME, "24:00:00", XP3_TYPE_TOKEN, "00:00:00" },
    { XP3_TYPE_DATE, "2026-11-19", XP3_TYPE_ANY_URI, "XPTY0004" }
};

static void
testCasts(void) {
    size_t i;

    for (i = 0; i < sizeof(castCases) / sizeof(castCases[0]); i++) {
        const xp3CastCase *test = &castCases[i];
        xp3Atomic *source = value(test->source, test->input);
        xp3Atomic *result = NULL;
        xmlChar *str = NULL;
        const char *actual;
        xp3ErrCode code;

        if (source == NULL)
            continue;
        code = xp3CastAtomic(source, test->target, &result);
        if (code == XP3_ERR_OK) {
            str = xp3AtomicToString(result);
            actual = (const char *) str;
            if (result->type != test->target)
                xp3TestFail("cast of xs:%s('%s') has the wrong type",
                            (const char *) xp3TypeLocalName(test->source),
                            test->input);
        } else {
            actual = codeName(code);
        }
        if ((actual == NULL) || (strcmp(actual, test->expected) != 0))
            xp3TestFail("xs:%s('%s') cast as xs:%s is %s, expected %s",
                        (const char *) xp3TypeLocalName(test->source),
                        test->input,
                        (const char *) xp3TypeLocalName(test->target),
                        actual, test->expected);
        xmlFree(str);
        xp3AtomicUnref(result);
        xp3AtomicUnref(source);
    }
}

/*
 * Comparisons
 */

/* An expected result: true, false, or the error */
#define XP3_EXPECT_TRUE 1
#define XP3_EXPECT_FALSE 0
#define XP3_EXPECT_XPTY0004 (-1)

typedef struct {
    xp3AstOp op;
    xp3TypeId leftType;
    const char *left;
    xp3TypeId rightType;
    const char *right;
    int timezone;               /* the implicit timezone */
    int expected;
} xp3CompareCase;

#define EQ XP3_OP_VAL_EQ
#define LT XP3_OP_VAL_LT
#define GT XP3_OP_VAL_GT
#define T_DT XP3_TYPE_DATE_TIME
#define T_D XP3_TYPE_DATE
#define T_T XP3_TYPE_TIME
#define T_DUR XP3_TYPE_DURATION
#define T_YM XP3_TYPE_YEAR_MONTH_DURATION
#define T_DTD XP3_TYPE_DAY_TIME_DURATION

static const xp3CompareCase compareCases[] = {
    /* F&O 3.1, 9.4, with an implicit timezone of -05:00 */
    { EQ, T_DT, "2002-04-02T12:00:00-01:00", T_DT,
      "2002-04-02T17:00:00+04:00", -300, XP3_EXPECT_TRUE },
    { EQ, T_DT, "2002-04-02T12:00:00", T_DT, "2002-04-02T23:00:00+06:00",
      -300, XP3_EXPECT_TRUE },
    { EQ, T_DT, "2002-04-02T12:00:00", T_DT, "2002-04-02T17:00:00",
      -300, XP3_EXPECT_FALSE },
    { EQ, T_DT, "2002-04-02T23:00:00-04:00", T_DT,
      "2002-04-03T02:00:00-01:00", -300, XP3_EXPECT_TRUE },
    { EQ, T_DT, "1999-12-31T24:00:00", T_DT, "2000-01-01T00:00:00",
      -300, XP3_EXPECT_TRUE },
    { EQ, T_DT, "2005-04-04T24:00:00", T_DT, "2005-04-04T00:00:00",
      -300, XP3_EXPECT_FALSE },
    { LT, T_DT, "2004-12-25T12:00:00-14:00", T_DT,
      "2004-12-26T00:00:00+14:00", -300, XP3_EXPECT_FALSE },
    { GT, T_DT, "2004-12-25T12:00:00", T_DT, "2004-12-25T11:00:00Z",
      -300, XP3_EXPECT_TRUE },
    { EQ, XP3_TYPE_DATE_TIME_STAMP, "2026-01-01T00:00:00Z", T_DT,
      "2026-01-01T01:00:00+01:00", 0, XP3_EXPECT_TRUE },
    { EQ, T_D, "2004-12-25Z", T_D, "2004-12-25+07:00", -300,
      XP3_EXPECT_FALSE },
    { EQ, T_D, "2004-12-25-12:00", T_D, "2004-12-26+12:00", -300,
      XP3_EXPECT_TRUE },
    { LT, T_D, "2004-12-25Z", T_D, "2004-12-25-05:00", -300,
      XP3_EXPECT_TRUE },
    { GT, T_D, "-0001-12-31", T_D, "0000-01-01", 0, XP3_EXPECT_FALSE },
    { LT, T_D, "-0001-12-31", T_D, "0000-01-01", 0, XP3_EXPECT_TRUE },
    { EQ, T_T, "08:00:00+09:00", T_T, "17:00:00-06:00", -300,
      XP3_EXPECT_FALSE },
    { EQ, T_T, "21:30:00+10:30", T_T, "06:00:00-05:00", -300,
      XP3_EXPECT_TRUE },
    { EQ, T_T, "24:00:00+01:00", T_T, "00:00:00+01:00", -300,
      XP3_EXPECT_TRUE },
    { LT, T_T, "12:00:00", T_T, "23:00:00+13:00", -300,
      XP3_EXPECT_FALSE },
    { GT, T_T, "08:00:00+09:00", T_T, "17:00:00-06:00", -300,
      XP3_EXPECT_FALSE },
    { EQ, XP3_TYPE_G_YEAR_MONTH, "1986-02", XP3_TYPE_G_YEAR_MONTH,
      "1986-03", -300, XP3_EXPECT_FALSE },
    { EQ, XP3_TYPE_G_YEAR_MONTH, "1978-03", XP3_TYPE_G_YEAR_MONTH,
      "1986-03Z", -300, XP3_EXPECT_FALSE },
    { EQ, XP3_TYPE_G_YEAR, "2005-12:00", XP3_TYPE_G_YEAR, "2005+12:00",
      -300, XP3_EXPECT_FALSE },
    { EQ, XP3_TYPE_G_YEAR, "1976-05:00", XP3_TYPE_G_YEAR, "1976", -300,
      XP3_EXPECT_TRUE },
    { EQ, XP3_TYPE_G_MONTH_DAY, "--12-25-14:00", XP3_TYPE_G_MONTH_DAY,
      "--12-26+10:00", -300, XP3_EXPECT_TRUE },
    { EQ, XP3_TYPE_G_MONTH_DAY, "--12-25", XP3_TYPE_G_MONTH_DAY,
      "--12-26Z", -300, XP3_EXPECT_FALSE },
    { EQ, XP3_TYPE_G_MONTH_DAY, "--02-29", XP3_TYPE_G_MONTH_DAY,
      "--03-01+14:00", 0, XP3_EXPECT_FALSE },
    { EQ, XP3_TYPE_G_MONTH, "--12-14:00", XP3_TYPE_G_MONTH, "--12+10:00",
      -300, XP3_EXPECT_FALSE },
    { EQ, XP3_TYPE_G_MONTH, "--12", XP3_TYPE_G_MONTH, "--12Z", -300,
      XP3_EXPECT_FALSE },
    { EQ, XP3_TYPE_G_DAY, "---25-14:00", XP3_TYPE_G_DAY, "---25+10:00",
      -300, XP3_EXPECT_FALSE },
    { EQ, XP3_TYPE_G_DAY, "---12", XP3_TYPE_G_DAY, "---12Z", -300,
      XP3_EXPECT_FALSE },
    { EQ, XP3_TYPE_G_DAY, "---12", XP3_TYPE_G_DAY, "---12Z", 0,
      XP3_EXPECT_TRUE },
    /* The g* types have no order, nor do different types compare */
    { LT, XP3_TYPE_G_YEAR, "2005", XP3_TYPE_G_YEAR, "2006", 0,
      XP3_EXPECT_XPTY0004 },
    { GT, XP3_TYPE_G_DAY, "---01", XP3_TYPE_G_DAY, "---02", 0,
      XP3_EXPECT_XPTY0004 },
    { EQ, T_D, "2026-01-01", T_DT, "2026-01-01T00:00:00", 0,
      XP3_EXPECT_XPTY0004 },
    { EQ, XP3_TYPE_G_YEAR, "2026", XP3_TYPE_G_YEAR_MONTH, "2026-01", 0,
      XP3_EXPECT_XPTY0004 },
    { EQ, T_D, "2026-01-01", T_DUR, "P1D", 0, XP3_EXPECT_XPTY0004 },
    /* The implicit timezone */
    { EQ, T_D, "2026-01-01", T_D, "2026-01-01Z", 0, XP3_EXPECT_TRUE },
    { EQ, T_D, "2026-01-01", T_D, "2026-01-01Z", 60, XP3_EXPECT_FALSE },
    { LT, T_D, "2026-01-01", T_D, "2026-01-01Z", 60, XP3_EXPECT_TRUE },
    { EQ, T_T, "12:00:00", T_T, "02:00:00Z", 600, XP3_EXPECT_TRUE },
    { EQ, T_T, "12:00:00", T_T, "02:00:00Z", -600, XP3_EXPECT_FALSE },
    /* F&O 3.1, 8.2: durations equal across their types */
    { EQ, T_YM, "P1Y", T_YM, "P12M", 0, XP3_EXPECT_TRUE },
    { EQ, T_YM, "P0M", T_DTD, "PT0S", 0, XP3_EXPECT_TRUE },
    { EQ, T_DUR, "P1Y", T_YM, "P12M", 0, XP3_EXPECT_TRUE },
    { EQ, T_DUR, "P1D", T_DTD, "PT24H", 0, XP3_EXPECT_TRUE },
    { EQ, T_DUR, "P1M", T_DUR, "P30D", 0, XP3_EXPECT_FALSE },
    { EQ, T_DUR, "P1Y", T_DTD, "P365D", 0, XP3_EXPECT_FALSE },
    { EQ, T_DUR, "-P0D", T_DUR, "PT0S", 0, XP3_EXPECT_TRUE },
    { LT, T_YM, "P1Y", T_YM, "P13M", 0, XP3_EXPECT_TRUE },
    { GT, T_YM, "-P1Y", T_YM, "-P13M", 0, XP3_EXPECT_TRUE },
    { GT, T_DTD, "PT1.5S", T_DTD, "PT1.4999S", 0, XP3_EXPECT_TRUE },
    { LT, T_DTD, "-PT1S", T_DTD, "PT0S", 0, XP3_EXPECT_TRUE },
    { LT, T_DTD, "-PT1.5S", T_DTD, "-PT1.2S", 0, XP3_EXPECT_TRUE },
    { LT, T_DUR, "P1Y", T_DUR, "P2Y", 0, XP3_EXPECT_XPTY0004 },
    { LT, T_YM, "P1Y", T_DTD, "P1D", 0, XP3_EXPECT_XPTY0004 },
    { LT, T_DUR, "P1Y", T_YM, "P2Y", 0, XP3_EXPECT_XPTY0004 }
};

#undef EQ
#undef LT
#undef GT
#undef T_DT
#undef T_D
#undef T_T
#undef T_DUR
#undef T_YM
#undef T_DTD

static void
testComparisons(void) {
    xp3Atomic *date;
    xp3Atomic *utc;
    xp3Clock clock;
    int result = -1;
    size_t i;

    for (i = 0; i < sizeof(compareCases) / sizeof(compareCases[0]); i++) {
        const xp3CompareCase *test = &compareCases[i];
        xp3Atomic *left = value(test->leftType, test->left);
        xp3Atomic *right = value(test->rightType, test->right);
        xp3ErrCode code;
        int actual;

        if ((left == NULL) || (right == NULL)) {
            xp3AtomicUnref(left);
            xp3AtomicUnref(right);
            continue;
        }
        fixedClock(&clock, test->timezone);
        code = xp3CompareAtomics(test->op, left, right, XP3_XPATH_31,
                                 &clock, &result);
        if (code == XP3_ERR_XPTY0004)
            actual = XP3_EXPECT_XPTY0004;
        else if (code == XP3_ERR_OK)
            actual = result;
        else
            actual = -2;
        if (actual != test->expected)
            xp3TestFail("comparing '%s' with '%s' (case %u) gave %d, "
                        "expected %d", test->left, test->right,
                        (unsigned) i, actual, test->expected);
        xp3AtomicUnref(left);
        xp3AtomicUnref(right);
    }

    /* Without an implicit timezone, only values with timezones compare */
    date = value(XP3_TYPE_DATE, "2026-01-01");
    utc = value(XP3_TYPE_DATE, "2026-01-01Z");
    if ((date != NULL) && (utc != NULL)) {
        check(xp3CompareAtomics(XP3_OP_VAL_EQ, date, utc, XP3_XPATH_31,
                                NULL, &result) == XP3_ERR_NEEDS_CONTEXT,
              "no clock, no implicit timezone");
        memset(&clock, 0, sizeof(clock));
        check(xp3CompareAtomics(XP3_OP_VAL_EQ, date, utc, XP3_XPATH_31,
                                &clock, &result) == XP3_ERR_NEEDS_CONTEXT,
              "a clock that isn't available, as while folding");
        check((xp3CompareAtomics(XP3_OP_VAL_EQ, utc, utc, XP3_XPATH_31,
                                 NULL, &result) == XP3_ERR_OK) &&
              (result == 1), "values with timezones need no clock");
        check(xp3CompareAtomics(XP3_OP_VAL_LT, date, utc, XP3_XPATH_31,
                                NULL, &result) == XP3_ERR_NEEDS_CONTEXT,
              "nor does ordering");
    }
    xp3AtomicUnref(date);
    xp3AtomicUnref(utc);
}

/*
 * The clock
 */

static int clockReads;

/* 2026-11-19T10:25:47.050651Z, at +05:30 */
static void
countingClock(xp3ClockReading *reading) {
    clockReads++;
    reading->seconds = INT64_C(1795083947);
    reading->nanoseconds = 50651000;
    reading->offset = 330;
}

static void
testClock(void) {
    xp3ClockReading reading;
    xp3Clock clock;
    int minutes = -1;

    check(xp3ClockTimezone(NULL, &minutes) == XP3_ERR_NEEDS_CONTEXT,
          "no clock");
    memset(&clock, 0, sizeof(clock));
    check(xp3ClockTimezone(&clock, &minutes) == XP3_ERR_NEEDS_CONTEXT,
          "a clock that isn't available");

    clock.available = 1;
    clock.source = countingClock;
    clockReads = 0;
    check((xp3ClockTimezone(&clock, &minutes) == XP3_ERR_OK) &&
          (minutes == 330), "the local offset is the implicit timezone");
    check((xp3ClockTimezone(&clock, &minutes) == XP3_ERR_OK) &&
          (minutes == 330), "and stays");
    check(clockReads == 1, "the clock is read once");
    check(clock.hasNow && (clock.now.year == 2026) &&
          (clock.now.month == 11) && (clock.now.day == 19) &&
          (clock.now.hour == 15) && (clock.now.minute == 55) &&
          (clock.now.second == 47) &&
          (clock.now.nanosecond == 50651000) &&
          (clock.now.timezone == 330),
          "the current dateTime is the local time of the reading");

    /* A current dateTime set by the context gives the timezone */
    memset(&clock, 0, sizeof(clock));
    clock.available = 1;
    clock.source = countingClock;
    clock.hasNow = 1;
    check(xp3DateTimeParse(XP3_TYPE_DATE_TIME_STAMP,
                           BAD_CAST "2026-01-01T00:00:00-03:00",
                           &clock.now) == XP3_ERR_OK, "a dateTime set");
    clockReads = 0;
    check((xp3ClockTimezone(&clock, &minutes) == XP3_ERR_OK) &&
          (minutes == -180) && (clockReads == 0),
          "the timezone of a dateTime set, without reading the clock");

    xp3SystemClock(&reading);
    check(reading.seconds > INT64_C(1767225600),
          "the system clock is after 2026");
    check((reading.nanoseconds >= 0) &&
          (reading.nanoseconds < XP3_NANOS_PER_SECOND) &&
          (reading.nanoseconds % 1000 == 0),
          "the system clock has microseconds");
    check((reading.offset >= -XP3_TIMEZONE_MAX) &&
          (reading.offset <= XP3_TIMEZONE_MAX),
          "the local offset is a timezone");
}

/*
 * Random properties
 */

/* xorshift64, for repeatable random tests */
static uint64_t rngState = UINT64_C(0x2545F4914F6CDD1D);

static uint64_t
rng(void) {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 7;
    rngState ^= rngState << 17;
    return(rngState);
}

/* A random number from 0 to limit - 1 */
static int64_t
randomBelow(uint64_t limit) {
    return((int64_t) (rng() % limit));
}

/* A random number from low to high */
static int64_t
randomBetween(int64_t low, int64_t high) {
    return(low + randomBelow((uint64_t) (high - low) + 1));
}

/* The leap year rule, written again for the oracle below */
static int
oracleLeap(int64_t year) {
    int64_t cycleYear = ((year % 400) + 400) % 400;

    if (cycleYear % 400 == 0)
        return(1);
    if (cycleYear % 100 == 0)
        return(0);
    return(cycleYear % 4 == 0);
}

static int
oracleMonthDays(int64_t year, int month) {
    static const int days[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    if ((month == 2) && oracleLeap(year))
        return(29);
    return(days[month - 1]);
}

/* Days from 0000-01-01 to a date, by counting days within a cycle of
 * 400 years, not with the library's formula */
static int64_t
oracleDays(int64_t year, int month, int day) {
    int64_t cycles = year / 400;
    int64_t days;
    int64_t i;
    int m;

    if (year % 400 < 0)
        cycles--;
    days = cycles * 146097;
    for (i = cycles * 400; i < year; i++) {
        days += 365;
        if (oracleLeap(i))
            days++;
    }
    for (m = 1; m < month; m++)
        days += oracleMonthDays(year, m);
    return(days + day - 1);
}

/* The starting instant of a date or time in seconds, nanoseconds
 * aside, with the templates of F&O 3.1, 9.4 */
static int64_t
oracleSeconds(xp3TypeId type, const xp3DateTime *dt, int timezone) {
    int64_t year = 1972;
    int month = 12;
    int day = 31;
    int64_t seconds;

    switch (type) {
        case XP3_TYPE_DATE_TIME:
        case XP3_TYPE_DATE:
            year = dt->year;
            month = dt->month;
            day = dt->day;
            break;
        case XP3_TYPE_G_YEAR_MONTH:
            year = dt->year;
            month = dt->month;
            day = 1;
            break;
        case XP3_TYPE_G_YEAR:
            year = dt->year;
            month = 1;
            day = 1;
            break;
        case XP3_TYPE_G_MONTH_DAY:
            month = dt->month;
            day = dt->day;
            break;
        case XP3_TYPE_G_MONTH:
            month = dt->month;
            day = 1;
            break;
        case XP3_TYPE_G_DAY:
            day = dt->day;
            break;
        default:
            break;
    }
    if (dt->hasTimezone)
        timezone = dt->timezone;
    seconds = oracleDays(year, month, day) * 86400;
    seconds += dt->hour * 3600 + dt->minute * 60 + dt->second;
    return(seconds - (int64_t) timezone * 60);
}

static const xp3TypeId xp3DateTypes[] = {
    XP3_TYPE_DATE_TIME, XP3_TYPE_DATE, XP3_TYPE_TIME,
    XP3_TYPE_G_YEAR_MONTH, XP3_TYPE_G_YEAR, XP3_TYPE_G_MONTH_DAY,
    XP3_TYPE_G_MONTH, XP3_TYPE_G_DAY
};

/* Clear the properties a type doesn't have, as casting from
 * xs:dateTime does, which every date and time type allows */
static void
keepParts(xp3TypeId type, xp3DateTime *dt) {
    xp3DateTime full = *dt;

    if (xp3DateTimeConvert(XP3_TYPE_DATE_TIME, &full, type, dt) !=
        XP3_ERR_OK)
        xp3TestFail("xs:dateTime can't be cast to xs:%s",
                    (const char *) xp3TypeLocalName(type));
}

/*
 * A random date or time. Dense values come from a few choices, so
 * that different values often share an instant across timezones; wide
 * values cover the whole range of years and fractions.
 */
static void
randomDateTime(xp3TypeId type, int dense, xp3DateTime *dt) {
    static const int denseZones[] = { 0, 60, -60, 840, -840, 30, -330 };

    memset(dt, 0, sizeof(*dt));
    if (dense) {
        dt->year = (int32_t) randomBetween(1999, 2000);
        if (randomBelow(2) == 0)
            dt->month = 12;
        else
            dt->month = 1;
        if (randomBelow(2) == 0)
            dt->day = 1;
        else
            dt->day = (unsigned char) oracleMonthDays(dt->year, dt->month);
        dt->hour = (unsigned char) (randomBelow(3) * 11 + randomBelow(2));
        dt->minute = (unsigned char) (randomBelow(2) * 30);
        if (randomBelow(4) == 0) {
            dt->hasTimezone = 0;
        } else {
            dt->hasTimezone = 1;
            dt->timezone = (int16_t) denseZones[randomBelow(7)];
        }
    } else {
        if (randomBelow(10) == 0)
            dt->year = (int32_t) randomBetween(-XP3_YEAR_MAX, XP3_YEAR_MAX);
        else
            dt->year = (int32_t) randomBetween(-3000, 3000);
        dt->month = (unsigned char) randomBetween(1, 12);
        dt->day = (unsigned char) randomBetween(
            1, oracleMonthDays(dt->year, dt->month));
        dt->hour = (unsigned char) randomBetween(0, 23);
        dt->minute = (unsigned char) randomBetween(0, 59);
        dt->second = (unsigned char) randomBetween(0, 59);
        if (randomBelow(2) == 0)
            dt->nanosecond = (int32_t) randomBelow(XP3_NANOS_PER_SECOND);
        else
            dt->nanosecond = (int32_t) (randomBelow(10) * 100000000);
        dt->hasTimezone = (unsigned char) randomBelow(2);
        if (dt->hasTimezone)
            dt->timezone = (int16_t) randomBetween(-XP3_TIMEZONE_MAX,
                                                   XP3_TIMEZONE_MAX);
    }
    keepParts(type, dt);
}

static int
sameDateTime(const xp3DateTime *a, const xp3DateTime *b) {
    return((a->year == b->year) && (a->month == b->month) &&
           (a->day == b->day) && (a->hour == b->hour) &&
           (a->minute == b->minute) && (a->second == b->second) &&
           (a->nanosecond == b->nanosecond) &&
           (a->hasTimezone == b->hasTimezone) &&
           (a->timezone == b->timezone));
}

/* A random duration of a type, both fields with one sign */
static void
randomDuration(xp3TypeId type, int dense, xp3Duration *dur) {
    int negative = (randomBelow(3) == 0);

    memset(dur, 0, sizeof(*dur));
    if (dense) {
        dur->months = randomBelow(3) * 12;
        dur->seconds = randomBelow(3) * 86400;
    } else if (randomBelow(8) == 0) {
        dur->months = (int64_t) (rng() >> 1);
        dur->seconds = (int64_t) (rng() >> 1);
        dur->nanoseconds = (int32_t) randomBelow(XP3_NANOS_PER_SECOND);
    } else {
        dur->months = randomBelow(500);
        dur->seconds = randomBelow(10000000);
        dur->nanoseconds = (int32_t) (randomBelow(1000) * 1000000);
    }
    if (dur->months == INT64_MAX)
        dur->months--;
    if (negative) {
        dur->months = -dur->months;
        dur->seconds = -dur->seconds;
        dur->nanoseconds = -dur->nanoseconds;
    }
    xp3DurationConvert(dur, type, dur);
}

/* Every random value reads back from its canonical form */
static int
roundTrips(void) {
    static const xp3TypeId durationTypes[] = {
        XP3_TYPE_DURATION, XP3_TYPE_YEAR_MONTH_DURATION,
        XP3_TYPE_DAY_TIME_DURATION
    };
    int bad = 0;
    int i;

    for (i = 0; i < 20000; i++) {
        xp3TypeId type = xp3DateTypes[randomBelow(8)];
        xp3TypeId durationType = durationTypes[randomBelow(3)];
        xp3DateTime dt;
        xp3DateTime back;
        xp3Duration dur;
        xp3Duration durBack;
        xmlChar *str;
        xp3ErrCode code;

        randomDateTime(type, (int) randomBelow(2), &dt);
        str = xp3DateTimeToString(type, &dt);
        code = xp3DateTimeParse(type, str, &back);
        if ((code != XP3_ERR_OK) || !sameDateTime(&dt, &back)) {
            if (bad == 0)
                xp3TestFail("xs:%s %s does not read back",
                            (const char *) xp3TypeLocalName(type),
                            (const char *) str);
            bad++;
        }
        xmlFree(str);

        randomDuration(durationType, (int) randomBelow(2), &dur);
        str = xp3DurationToString(durationType, &dur);
        code = xp3DurationParse(durationType, str, &durBack);
        if ((code != XP3_ERR_OK) || (dur.months != durBack.months) ||
            (dur.seconds != durBack.seconds) ||
            (dur.nanoseconds != durBack.nanoseconds)) {
            if (bad == 0)
                xp3TestFail("xs:%s %s does not read back",
                            (const char *) xp3TypeLocalName(durationType),
                            (const char *) str);
            bad++;
        }
        xmlFree(str);
    }
    return(bad);
}

/* The sign of a comparison of two numbers */
static int
sign64(int64_t a, int64_t b) {
    if (a < b)
        return(-1);
    if (a > b)
        return(1);
    return(0);
}

/* Comparisons agree with the oracle's instants */
static int
comparisonsAgree(void) {
    static const xp3AstOp ops[] = {
        XP3_OP_VAL_EQ, XP3_OP_VAL_NE, XP3_OP_VAL_LT, XP3_OP_VAL_LE,
        XP3_OP_VAL_GT, XP3_OP_VAL_GE
    };
    int bad = 0;
    int i;

    for (i = 0; i < 20000; i++) {
        xp3TypeId type = xp3DateTypes[randomBelow(8)];
        int dense = (randomBelow(4) != 0);
        int timezone = (int) randomBetween(-XP3_TIMEZONE_MAX,
                                           XP3_TIMEZONE_MAX);
        xp3AstOp op = ops[randomBelow(6)];
        xp3DateTime a;
        xp3DateTime b;
        xp3Atomic *left;
        xp3Atomic *right;
        xp3Clock clock;
        int64_t secondsA;
        int64_t secondsB;
        int cmp;
        int expected;
        int result = -1;
        xp3ErrCode code;

        randomDateTime(type, dense, &a);
        randomDateTime(type, dense, &b);
        if (!dense) {
            /* The oracle counts years one by one */
            a.year = (int32_t) (a.year % 5000);
            b.year = (int32_t) (b.year % 5000);
            if ((a.month == 2) && (a.day == 29) && !oracleLeap(a.year))
                a.day = 28;
            if ((b.month == 2) && (b.day == 29) && !oracleLeap(b.year))
                b.day = 28;
        }
        if (dense && (randomBelow(4) == 0))
            timezone = 0;
        secondsA = oracleSeconds(type, &a, timezone);
        secondsB = oracleSeconds(type, &b, timezone);
        cmp = sign64(secondsA, secondsB);
        if (cmp == 0)
            cmp = sign64(a.nanosecond, b.nanosecond);
        left = xp3AtomicNewDateTime(type, &a);
        right = xp3AtomicNewDateTime(type, &b);
        if ((left == NULL) || (right == NULL)) {
            xp3AtomicUnref(left);
            xp3AtomicUnref(right);
            bad++;
            continue;
        }
        fixedClock(&clock, timezone);
        code = xp3CompareAtomics(op, left, right, XP3_XPATH_31, &clock,
                                 &result);
        switch (op) {
            case XP3_OP_VAL_EQ:
                expected = (cmp == 0);
                break;
            case XP3_OP_VAL_NE:
                expected = (cmp != 0);
                break;
            case XP3_OP_VAL_LT:
                expected = (cmp < 0);
                break;
            case XP3_OP_VAL_LE:
                expected = (cmp <= 0);
                break;
            case XP3_OP_VAL_GT:
                expected = (cmp > 0);
                break;
            default:
                expected = (cmp >= 0);
                break;
        }
        if ((op != XP3_OP_VAL_EQ) && (op != XP3_OP_VAL_NE) &&
            (type != XP3_TYPE_DATE_TIME) && (type != XP3_TYPE_DATE) &&
            (type != XP3_TYPE_TIME))
            expected = -1;
        if (code == XP3_ERR_OK) {
            if (result != expected)
                bad++;
        } else if ((code != XP3_ERR_XPTY0004) || (expected != -1)) {
            bad++;
        }
        if (bad == 1) {
            xmlChar *strA = xp3DateTimeToString(type, &a);
            xmlChar *strB = xp3DateTimeToString(type, &b);

            xp3TestFail("comparing xs:%s %s and %s with the implicit "
                        "timezone %d disagrees with the instants",
                        (const char *) xp3TypeLocalName(type),
                        (const char *) strA, (const char *) strB,
                        timezone);
            xmlFree(strA);
            xmlFree(strB);
            bad++;
        }
        xp3AtomicUnref(left);
        xp3AtomicUnref(right);
    }
    return(bad);
}

/* Bind a variable to one value made from its lexical form */
static int
bindValue(xp3DynamicContext *ctx, const char *name, xp3TypeId type,
          const xmlChar *lexical) {
    const xmlChar *lexicals[1];
    xp3Sequence *seq;
    int ret;

    lexicals[0] = lexical;
    seq = xp3NewAtomicSequence(type, lexicals, 1, NULL);
    if (seq == NULL)
        return(-1);
    ret = xp3BindVariable(ctx, NULL, BAD_CAST name, seq);
    xp3FreeSequence(seq);
    return(ret);
}

/*
 * fn:distinct-values, whose hash table keys values by their normalized
 * form, keeps one of two values exactly when eq says they are equal,
 * and a date never equals a dateTime
 */
static int
hashingAgrees(void) {
    static const xp3TypeId types[] = {
        XP3_TYPE_DATE_TIME, XP3_TYPE_DATE, XP3_TYPE_TIME,
        XP3_TYPE_G_YEAR_MONTH, XP3_TYPE_G_YEAR, XP3_TYPE_G_MONTH_DAY,
        XP3_TYPE_G_MONTH, XP3_TYPE_G_DAY, XP3_TYPE_DURATION,
        XP3_TYPE_YEAR_MONTH_DURATION, XP3_TYPE_DAY_TIME_DURATION
    };
    xp3StaticContext *sctx = xp3NewStaticContext();
    xp3DynamicContext *ctx = xp3NewDynamicContext();
    xp3Expr *expr = NULL;
    int bad = 0;
    int i;

    if ((sctx == NULL) || (ctx == NULL) ||
        (xp3DeclareVariable(sctx, NULL, BAD_CAST "a") < 0) ||
        (xp3DeclareVariable(sctx, NULL, BAD_CAST "b") < 0)) {
        bad++;
        goto done;
    }
    expr = xp3Compile(sctx, BAD_CAST "count(distinct-values(($a, $b))) = "
                      "(if (deep-equal($a, $b)) then 1 else 2)", NULL);
    if (expr == NULL) {
        bad++;
        goto done;
    }
    for (i = 0; i < 3000; i++) {
        xp3TypeId typeA = types[randomBelow(11)];
        xp3TypeId typeB = typeA;
        int timezone = (int) (randomBelow(5) * 30 - 60);
        xmlChar *strA;
        xmlChar *strB;
        int result;

        /* Sometimes a type its values can't equal, or another
         * duration type */
        if (randomBelow(4) == 0)
            typeB = types[randomBelow(11)];
        if (xp3TypeRepresentation(typeA) == XP3_REP_DURATION) {
            xp3Duration a;
            xp3Duration b;

            if (xp3TypeRepresentation(typeB) != XP3_REP_DURATION)
                typeB = XP3_TYPE_DURATION;
            randomDuration(typeA, 1, &a);
            randomDuration(typeB, 1, &b);
            strA = xp3DurationToString(typeA, &a);
            strB = xp3DurationToString(typeB, &b);
        } else {
            xp3DateTime a;
            xp3DateTime b;

            if (xp3TypeRepresentation(typeB) != XP3_REP_DATE_TIME)
                typeB = typeA;
            randomDateTime(typeA, 1, &a);
            randomDateTime(typeB, 1, &b);
            strA = xp3DateTimeToString(typeA, &a);
            strB = xp3DateTimeToString(typeB, &b);
        }
        result = -1;
        if ((strA != NULL) && (strB != NULL) &&
            (xp3SetImplicitTimezone(ctx, timezone) == 0) &&
            (bindValue(ctx, "a", typeA, strA) == 0) &&
            (bindValue(ctx, "b", typeB, strB) == 0))
            result = xp3EvaluateBoolean(expr, ctx, NULL);
        if (result != 1) {
            if (bad == 0)
                xp3TestFail("distinct-values and deep-equal disagree on "
                            "xs:%s %s and xs:%s %s at %d",
                            (const char *) xp3TypeLocalName(typeA),
                            (const char *) strA,
                            (const char *) xp3TypeLocalName(typeB),
                            (const char *) strB, timezone);
            bad++;
        }
        xmlFree(strA);
        xmlFree(strB);
    }

done:
    xp3FreeExpr(expr);
    xp3FreeDynamicContext(ctx);
    xp3FreeStaticContext(sctx);
    return(bad);
}

static void
testProperties(void) {
    check(roundTrips() == 0,
          "20000 random values read back from their canonical forms");
    check(comparisonsAgree() == 0,
          "20000 random comparisons agree with the instants");
    check(hashingAgrees() == 0,
          "fn:distinct-values agrees with eq on 3000 random pairs");
}

/*
 * Allocation failures
 */

/* Casts from and to strings, and fn:distinct-values over dates and
 * durations, concatenated. Returns NULL with *code on failure. */
static xmlChar *
operationChain(const xp3Expr *expr, xp3DynamicContext *ctx,
               xp3ErrCode *code) {
    static const struct {
        xp3TypeId type;
        const char *lexical;
    } inputs[] = {
        { XP3_TYPE_DATE_TIME, "2026-11-19T11:25:47.5+01:00" },
        { XP3_TYPE_G_MONTH_DAY, "--02-29" },
        { XP3_TYPE_DURATION, "-P1Y2M3DT4H5M6.7S" }
    };
    xmlChar *out = NULL;
    xp3Sequence *result;
    xp3Error *err = NULL;
    size_t i;

    *code = XP3_ERR_OK;
    for (i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
        xp3Atomic *atomic = castString(inputs[i].type, inputs[i].lexical,
                                       code);
        xp3Atomic *str = NULL;

        if (atomic == NULL)
            goto error;
        *code = xp3CastAtomic(atomic, XP3_TYPE_UNTYPED_ATOMIC, &str);
        xp3AtomicUnref(atomic);
        if (*code != XP3_ERR_OK)
            goto error;
        out = xmlStrcat(out, str->u.str);
        xp3AtomicUnref(str);
        if (out == NULL) {
            *code = XP3_ERR_NO_MEMORY;
            goto error;
        }
    }
    result = xp3Evaluate(expr, ctx, &err);
    if (result == NULL) {
        if (xmlStrEqual(xp3ErrorCode(err), BAD_CAST "NO_MEMORY"))
            *code = XP3_ERR_NO_MEMORY;
        else
            *code = XP3_ERR_UNSUPPORTED;
        xp3ErrorFree(err);
        goto error;
    }
    for (i = 0; i < xp3SequenceLength(result); i++) {
        xmlChar *str = xp3SequenceStringValue(result, i);

        if (str != NULL)
            out = xmlStrcat(out, str);
        xmlFree(str);
        if ((str == NULL) || (out == NULL)) {
            xp3FreeSequence(result);
            *code = XP3_ERR_NO_MEMORY;
            goto error;
        }
    }
    xp3FreeSequence(result);
    return(out);

error:
    xmlFree(out);
    return(NULL);
}

static void
testAllocationFailures(void) {
    xp3DynamicContext *ctx = xp3NewDynamicContext();
    xp3StaticContext *plain = xp3NewStaticContext();
    xp3Expr *expr;
    xmlChar *expected;
    xp3ErrCode code;
    int i;

    if ((ctx == NULL) || (plain == NULL) ||
        (xp3SetImplicitTimezone(ctx, 0) < 0)) {
        check(0, "contexts created");
        xp3FreeDynamicContext(ctx);
        xp3FreeStaticContext(plain);
        return;
    }
    /* Not folded, so that the evaluation does the work */
    plain->noOptimize = 1;
    expr = xp3Compile(plain, BAD_CAST
                      "distinct-values((xs:date('2026-01-01'), "
                      "xs:date('2026-01-01Z'), xs:date('2026-01-02'), "
                      "xs:dayTimeDuration('PT24H'), xs:duration('P1D'), "
                      "xs:yearMonthDuration('P0M'), xs:time('24:00:00'), "
                      "xs:time('00:00:00')))", NULL);
    check(expr != NULL, "the expression compiles");
    expected = operationChain(expr, ctx, &code);
    check((expected != NULL) &&
          xmlStrEqual(expected, BAD_CAST
                      "2026-11-19T11:25:47.5+01:00--02-29"
                      "-P1Y2M3DT4H5M6.7S2026-01-012026-01-02P1DP0M"
                      "00:00:00"), "the chain computed");

    if (!xp3TestAllocInstall()) {
        xp3TestSkip("this libxml2 ignores xmlMemSetup()");
        xp3TestAllocRestore();
    } else {
        for (i = 0; i < 1000; i++) {
            xmlChar *actual;

            xp3TestAllocFailAt(i);
            actual = operationChain(expr, ctx, &code);
            if (!xp3TestAllocFailed()) {
                check((actual != NULL) && xmlStrEqual(actual, expected),
                      "the chain completes after allocation failures");
                xp3TestAllocFailAt(-1);
                xmlFree(actual);
                break;
            }
            xp3TestAllocFailAt(-1);
            check((actual == NULL) && (code == XP3_ERR_NO_MEMORY),
                  "a failed allocation is reported as NO_MEMORY");
            xmlFree(actual);
        }
        check((i > 0) && (i < 1000), "allocation failure points exercised");
        xp3TestAllocRestore();
    }
    xmlFree(expected);
    xp3FreeExpr(expr);
    xp3FreeStaticContext(plain);
    xp3FreeDynamicContext(ctx);
}

int
main(void) {
    xmlInitParser();

    XP3_TEST_RUN(testLexicalForms);
    XP3_TEST_RUN(testInvalidForms);
    XP3_TEST_RUN(testCalendar);
    XP3_TEST_RUN(testCasts);
    XP3_TEST_RUN(testComparisons);
    XP3_TEST_RUN(testClock);
    XP3_TEST_RUN(testProperties);
    XP3_TEST_RUN(testAllocationFailures);

    return(xp3TestDone());
}
