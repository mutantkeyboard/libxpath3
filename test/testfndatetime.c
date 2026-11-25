/*
 * testfndatetime.c: tests for the functions on dates, times and
 * durations
 *
 * Expressions evaluated with an implicit timezone of -05:00, as the
 * examples of F&O 3.1 assume, compared as "type:canonical form" with
 * the results the spec gives; edge cases of the representation:
 * negative durations and years, year 0, 24:00:00, and timezones of
 * +-14:00, -00:00 and none.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdio.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/xmlmemory.h>

#include <libxpath3/xpath3.h>
#include "private/xp3static.h"
#include "private/xp3types.h"
#include "private/xp3value.h"
#include "testtap.h"

static void
check(int cond, const char *what) {
    xp3TestCheck(cond, what);
}

/*
 * Evaluating
 */

/* Append an item's description to out, after a space unless first */
static void
appendItem(char *out, size_t size, const char *item) {
    size_t used = strlen(out);

    if (used > 0)
        snprintf(out + used, size - used, " %s", item);
    else
        snprintf(out, size, "%s", item);
}

/* A result as "type:value" for each item, separated by spaces, or
 * "error:CODE" */
static void
describe(const xp3Sequence *seq, char *out, size_t size) {
    size_t i;

    out[0] = 0;
    for (i = 0; i < xp3SequenceLength(seq); i++) {
        int type = xp3SequenceAtomicType(seq, i);
        xmlChar *value = xp3SequenceStringValue(seq, i);
        char item[256];

        if ((type < 0) || (value == NULL))
            snprintf(item, sizeof(item), "?");
        else
            snprintf(item, sizeof(item), "%s:%s",
                     (const char *) xp3AtomicTypeName(
                         (xp3AtomicType) type),
                     (const char *) value);
        xmlFree(value);
        appendItem(out, size, item);
    }
}

/* Compile and evaluate, and describe the result or the error */
static void
run(const xp3StaticContext *sctx, xp3DynamicContext *ctx, const char *expr,
    char *out, size_t size) {
    xp3Error *err = NULL;
    xp3Expr *compiled = xp3Compile(sctx, BAD_CAST expr, &err);
    xp3Sequence *result = NULL;

    if (compiled != NULL)
        result = xp3Evaluate(compiled, ctx, &err);
    if (result != NULL)
        describe(result, out, size);
    else
        snprintf(out, size, "error:%s", (const char *) xp3ErrorCode(err));
    xp3FreeSequence(result);
    xp3FreeExpr(compiled);
    xp3ErrorFree(err);
}

static void
checkEval(const xp3StaticContext *sctx, xp3DynamicContext *ctx,
          const char *expr, const char *expected) {
    char actual[1024];

    run(sctx, ctx, expr, actual, sizeof(actual));
    if (strcmp(actual, expected) != 0)
        xp3TestFail("%s\n  expected: %s\n  actual:   %s", expr, expected,
                    actual);
}

typedef struct {
    const char *expr;
    const char *expected;
} xp3EvalCase;

/* Evaluate cases with an implicit timezone of -05:00 */
static void
checkCases(const xp3EvalCase *cases, size_t count) {
    xp3DynamicContext *ctx = xp3NewDynamicContext();
    size_t i;

    if ((ctx == NULL) || (xp3SetImplicitTimezone(ctx, -300) < 0)) {
        check(0, "context created");
        xp3FreeDynamicContext(ctx);
        return;
    }
    for (i = 0; i < count; i++)
        checkEval(NULL, ctx, cases[i].expr, cases[i].expected);
    xp3FreeDynamicContext(ctx);
}

#define CHECK_CASES(cases) \
    checkCases(cases, sizeof(cases) / sizeof((cases)[0]))

/*
 * Components of durations (F&O 3.1, 8.3)
 */

static const xp3EvalCase durationCases[] = {
    /* The examples of 8.3.1 to 8.3.6 */
    { "years-from-duration(xs:yearMonthDuration('P20Y15M'))", "integer:21" },
    { "years-from-duration(xs:yearMonthDuration('-P15M'))", "integer:-1" },
    { "years-from-duration(xs:dayTimeDuration('-P2DT15H'))", "integer:0" },
    { "months-from-duration(xs:yearMonthDuration('P20Y15M'))", "integer:3" },
    { "months-from-duration(xs:yearMonthDuration('-P20Y18M'))",
      "integer:-6" },
    { "months-from-duration(xs:dayTimeDuration('-P2DT15H0M0S'))",
      "integer:0" },
    { "days-from-duration(xs:dayTimeDuration('P3DT10H'))", "integer:3" },
    { "days-from-duration(xs:dayTimeDuration('P3DT55H'))", "integer:5" },
    { "days-from-duration(xs:yearMonthDuration('P3Y5M'))", "integer:0" },
    { "hours-from-duration(xs:dayTimeDuration('P3DT10H'))", "integer:10" },
    { "hours-from-duration(xs:dayTimeDuration('P3DT12H32M12S'))",
      "integer:12" },
    { "hours-from-duration(xs:dayTimeDuration('PT123H'))", "integer:3" },
    { "hours-from-duration(xs:dayTimeDuration('-P3DT10H'))", "integer:-10" },
    { "minutes-from-duration(xs:dayTimeDuration('P3DT10H'))", "integer:0" },
    { "minutes-from-duration(xs:dayTimeDuration('-P5DT12H30M'))",
      "integer:-30" },
    { "seconds-from-duration(xs:dayTimeDuration('P3DT10H12.5S'))",
      "decimal:12.5" },
    { "seconds-from-duration(xs:dayTimeDuration('-PT256S'))",
      "decimal:-16" },

    /* Plain xs:duration has both halves */
    { "let $d := xs:duration('-P1Y14M3DT25H61M59.000000001S') return "
      "(years-from-duration($d), months-from-duration($d), "
      "days-from-duration($d), hours-from-duration($d), "
      "minutes-from-duration($d), seconds-from-duration($d))",
      "integer:-2 integer:-2 integer:-4 integer:-2 integer:-1 "
      "decimal:-59.000000001" },
    { "seconds-from-duration(xs:duration('-PT0.5S'))", "decimal:-0.5" },
    { "seconds-from-duration(xs:duration('PT0.000000001S'))",
      "decimal:0.000000001" },
    { "seconds-from-duration(xs:yearMonthDuration('P1Y'))", "decimal:0" },
    { "years-from-duration(xs:duration('PT0S'))", "integer:0" },
    { "seconds-from-duration(xs:duration('-P0D'))", "decimal:0" },

    /* An untyped argument is cast to xs:duration */
    { "days-from-duration(xs:untypedAtomic('P2D'))", "integer:2" },

    /* The largest months and seconds */
    { "years-from-duration(xs:yearMonthDuration("
      "'-P768614336404564650Y7M'))", "integer:-768614336404564650" },
    { "days-from-duration(xs:dayTimeDuration('PT9223372036854775807S'))",
      "integer:106751991167300" },
    { "seconds-from-duration(xs:dayTimeDuration("
      "'-PT9223372036854775807.999999999S'))", "decimal:-7.999999999" },

    /* The empty sequence */
    { "years-from-duration(())", "" },
    { "months-from-duration(())", "" },
    { "days-from-duration(())", "" },
    { "hours-from-duration(())", "" },
    { "minutes-from-duration(())", "" },
    { "seconds-from-duration(())", "" },

    /* Not a duration */
    { "years-from-duration(xs:date('2026-01-01'))", "error:XPTY0004" },
    { "seconds-from-duration('PT1S')", "error:XPTY0004" }
};

static void
testDurationComponents(void) {
    CHECK_CASES(durationCases);
}

/*
 * Components of dates and times (F&O 3.1, 9.5)
 */

static const xp3EvalCase componentCases[] = {
    /* The examples of 9.5.1 to 9.5.14 */
    { "year-from-dateTime(xs:dateTime('1999-05-31T13:20:00-05:00'))",
      "integer:1999" },
    { "year-from-dateTime(xs:dateTime('1999-05-31T21:30:00-05:00'))",
      "integer:1999" },
    { "year-from-dateTime(xs:dateTime('1999-12-31T19:20:00'))",
      "integer:1999" },
    { "year-from-dateTime(xs:dateTime('1999-12-31T24:00:00'))",
      "integer:2000" },
    { "year-from-dateTime(xs:dateTime('-0002-06-06T00:00:00'))",
      "integer:-2" },
    { "month-from-dateTime(xs:dateTime('1999-05-31T13:20:00-05:00'))",
      "integer:5" },
    { "month-from-dateTime(xs:dateTime('1999-12-31T19:20:00-05:00'))",
      "integer:12" },
    { "day-from-dateTime(xs:dateTime('1999-05-31T13:20:00-05:00'))",
      "integer:31" },
    { "day-from-dateTime(xs:dateTime('1999-12-31T20:00:00-05:00'))",
      "integer:31" },
    { "hours-from-dateTime(xs:dateTime('1999-05-31T08:20:00-05:00'))",
      "integer:8" },
    { "hours-from-dateTime(xs:dateTime('1999-12-31T21:20:00-05:00'))",
      "integer:21" },
    { "hours-from-dateTime(xs:dateTime('1999-12-31T12:00:00'))",
      "integer:12" },
    { "hours-from-dateTime(xs:dateTime('1999-12-31T24:00:00'))",
      "integer:0" },
    { "minutes-from-dateTime(xs:dateTime('1999-05-31T13:20:00-05:00'))",
      "integer:20" },
    { "minutes-from-dateTime(xs:dateTime('1999-05-31T13:30:00+05:30'))",
      "integer:30" },
    { "seconds-from-dateTime(xs:dateTime('1999-05-31T13:20:00-05:00'))",
      "decimal:0" },
    { "year-from-date(xs:date('1999-05-31'))", "integer:1999" },
    { "year-from-date(xs:date('2000-01-01+05:00'))", "integer:2000" },
    { "year-from-date(xs:date('-0002-06-01'))", "integer:-2" },
    { "month-from-date(xs:date('1999-05-31-05:00'))", "integer:5" },
    { "month-from-date(xs:date('2000-01-01+05:00'))", "integer:1" },
    { "day-from-date(xs:date('1999-05-31-05:00'))", "integer:31" },
    { "day-from-date(xs:date('2000-01-01+05:00'))", "integer:1" },
    { "hours-from-time(xs:time('11:23:00'))", "integer:11" },
    { "hours-from-time(xs:time('21:23:00'))", "integer:21" },
    { "hours-from-time(xs:time('01:23:00+05:00'))", "integer:1" },
    { "hours-from-time(xs:time('24:00:00'))", "integer:0" },
    { "minutes-from-time(xs:time('13:00:00Z'))", "integer:0" },
    { "seconds-from-time(xs:time('13:20:10.5'))", "decimal:10.5" },

    /* The examples of 9.5.7, 9.5.11 and 9.5.15 */
    { "timezone-from-dateTime(xs:dateTime('1999-05-31T13:20:00-05:00'))",
      "dayTimeDuration:-PT5H" },
    { "timezone-from-dateTime(xs:dateTime('2000-06-12T13:20:00Z'))",
      "dayTimeDuration:PT0S" },
    { "timezone-from-dateTime(xs:dateTime('2004-08-27T00:00:00'))", "" },
    { "timezone-from-date(xs:date('1999-05-31-05:00'))",
      "dayTimeDuration:-PT5H" },
    { "timezone-from-date(xs:date('2000-06-12Z'))", "dayTimeDuration:PT0S" },
    { "timezone-from-time(xs:time('13:20:00-05:00'))",
      "dayTimeDuration:-PT5H" },
    { "timezone-from-time(xs:time('13:20:00'))", "" },

    /* The limits, -00:00 and minutes */
    { "timezone-from-dateTime(xs:dateTime('2026-01-01T00:00:00+14:00'))",
      "dayTimeDuration:PT14H" },
    { "timezone-from-date(xs:date('2026-01-01-14:00'))",
      "dayTimeDuration:-PT14H" },
    { "timezone-from-time(xs:time('00:00:00-00:00'))",
      "dayTimeDuration:PT0S" },
    { "timezone-from-time(xs:time('24:00:00+05:30'))",
      "dayTimeDuration:PT5H30M" },
    { "timezone-from-date(xs:date('2026-01-01-00:45'))",
      "dayTimeDuration:-PT45M" },
    { "timezone-from-dateTime(())", "" },
    { "timezone-from-date(())", "" },
    { "timezone-from-time(())", "" },
    { "timezone-from-time(xs:date('2026-01-01Z'))", "error:XPTY0004" },

    /* The local value, whatever the timezone */
    { "let $d := xs:dateTime('-0001-12-31T23:59:59.999999999+14:00') "
      "return (year-from-dateTime($d), month-from-dateTime($d), "
      "day-from-dateTime($d), hours-from-dateTime($d), "
      "minutes-from-dateTime($d), seconds-from-dateTime($d))",
      "integer:-1 integer:12 integer:31 integer:23 integer:59 "
      "decimal:59.999999999" },
    { "let $d := xs:date('0000-02-29-14:00') return "
      "(year-from-date($d), month-from-date($d), day-from-date($d))",
      "integer:0 integer:2 integer:29" },
    { "year-from-dateTime(xs:dateTime('-0001-12-31T24:00:00'))",
      "integer:0" },
    { "day-from-dateTime(xs:dateTime('2000-02-28T24:00:00Z'))",
      "integer:29" },
    { "year-from-date(xs:date('-2147483647-01-01'))",
      "integer:-2147483647" },
    { "year-from-dateTime(xs:dateTime('2147483647-12-31T23:59:59Z'))",
      "integer:2147483647" },
    { "seconds-from-time(xs:time('00:00:00.000000001-00:00'))",
      "decimal:0.000000001" },
    { "seconds-from-dateTime(xs:dateTimeStamp('2026-11-24T10:00:07Z'))",
      "decimal:7" },
    { "minutes-from-time(xs:untypedAtomic('10:42:00'))", "integer:42" },

    /* The empty sequence */
    { "year-from-dateTime(())", "" },
    { "month-from-date(())", "" },
    { "seconds-from-time(())", "" },

    /* The wrong type */
    { "year-from-date(xs:dateTime('2026-01-01T00:00:00'))",
      "error:XPTY0004" },
    { "hours-from-time(xs:dateTime('2026-01-01T00:00:00'))",
      "error:XPTY0004" },
    { "hours-from-dateTime(xs:time('10:00:00'))", "error:XPTY0004" },
    { "year-from-date(xs:untypedAtomic('2026-13-01'))", "error:FORG0001" }
};

static void
testDateTimeComponents(void) {
    CHECK_CASES(componentCases);
}

/*
 * fn:dateTime (F&O 3.1, 9.3.1)
 */

static const xp3EvalCase constructorCases[] = {
    /* The examples of 9.3.1 */
    { "dateTime(xs:date('1999-12-31'), xs:time('12:00:00'))",
      "dateTime:1999-12-31T12:00:00" },
    { "dateTime(xs:date('1999-12-31'), xs:time('24:00:00'))",
      "dateTime:1999-12-31T00:00:00" },

    /* The timezone of either, or of both if they agree */
    { "dateTime(xs:date('2026-11-24+14:00'), xs:time('10:20:30.5'))",
      "dateTime:2026-11-24T10:20:30.5+14:00" },
    { "dateTime(xs:date('2026-11-24'), xs:time('10:20:30-14:00'))",
      "dateTime:2026-11-24T10:20:30-14:00" },
    { "dateTime(xs:date('2026-11-24Z'), xs:time('10:20:30-00:00'))",
      "dateTime:2026-11-24T10:20:30Z" },
    { "dateTime(xs:date('2026-11-24+05:30'), xs:time('10:20:30+05:30'))",
      "dateTime:2026-11-24T10:20:30+05:30" },
    { "dateTime(xs:date('2026-11-24Z'), xs:time('10:20:30+00:01'))",
      "error:FORG0008" },
    { "dateTime(xs:date('2026-11-24-05:00'), xs:time('10:20:30'))",
      "dateTime:2026-11-24T10:20:30-05:00" },

    /* Negative years and year 0 */
    { "dateTime(xs:date('-0001-12-31'), xs:time('23:59:59.999999999'))",
      "dateTime:-0001-12-31T23:59:59.999999999" },
    { "dateTime(xs:date('0000-02-29Z'), xs:time('00:00:00'))",
      "dateTime:0000-02-29T00:00:00Z" },
    { "dateTime(xs:date('2147483647-12-31'), xs:time('24:00:00'))",
      "dateTime:2147483647-12-31T00:00:00" },

    /* The empty sequence */
    { "dateTime((), xs:time('10:00:00'))", "" },
    { "dateTime(xs:date('2026-01-01'), ())", "" },
    { "dateTime((), ())", "" },

    /* The wrong types; untyped values are cast */
    { "dateTime(xs:time('10:00:00'), xs:date('2026-01-01'))",
      "error:XPTY0004" },
    { "dateTime(xs:dateTime('2026-01-01T00:00:00'), xs:time('10:00:00'))",
      "error:XPTY0004" },
    { "dateTime(xs:untypedAtomic('2026-01-01'), "
      "xs:untypedAtomic('10:00:00Z'))", "dateTime:2026-01-01T10:00:00Z" }
};

static void
testDateTimeConstructor(void) {
    CHECK_CASES(constructorCases);
}

/*
 * Adjusting to a timezone (F&O 3.1, 9.6)
 */

#define TZ_MINUS_10 "xs:dayTimeDuration('-PT10H')"

static const xp3EvalCase adjustCases[] = {
    /* The examples of 9.6.1 to 9.6.3 with the implicit timezone */
    { "adjust-dateTime-to-timezone(xs:dateTime('2002-03-07T10:00:00'))",
      "dateTime:2002-03-07T10:00:00-05:00" },
    { "adjust-dateTime-to-timezone("
      "xs:dateTime('2002-03-07T10:00:00-07:00'))",
      "dateTime:2002-03-07T12:00:00-05:00" },
    { "adjust-date-to-timezone(xs:date('2002-03-07'))",
      "date:2002-03-07-05:00" },
    { "adjust-date-to-timezone(xs:date('2002-03-07-07:00'))",
      "date:2002-03-07-05:00" },
    { "adjust-time-to-timezone(xs:time('10:00:00'))", "time:10:00:00-05:00" },
    { "adjust-time-to-timezone(xs:time('10:00:00-07:00'))",
      "time:12:00:00-05:00" },
    { "adjust-date-to-timezone(())", "" },

    /* The examples of 9.6.1 to 9.6.3 that give the timezone */
    { "adjust-dateTime-to-timezone(xs:dateTime('2002-03-07T10:00:00'), "
      TZ_MINUS_10 ")", "dateTime:2002-03-07T10:00:00-10:00" },
    { "adjust-dateTime-to-timezone("
      "xs:dateTime('2002-03-07T10:00:00-07:00'), " TZ_MINUS_10 ")",
      "dateTime:2002-03-07T07:00:00-10:00" },
    { "adjust-dateTime-to-timezone("
      "xs:dateTime('2002-03-07T10:00:00-07:00'), "
      "xs:dayTimeDuration('PT10H'))", "dateTime:2002-03-08T03:00:00+10:00" },
    { "adjust-dateTime-to-timezone("
      "xs:dateTime('2002-03-07T00:00:00+01:00'), "
      "xs:dayTimeDuration('-PT8H'))", "dateTime:2002-03-06T15:00:00-08:00" },
    { "adjust-dateTime-to-timezone(xs:dateTime('2002-03-07T10:00:00'), ())",
      "dateTime:2002-03-07T10:00:00" },
    { "adjust-dateTime-to-timezone("
      "xs:dateTime('2002-03-07T10:00:00-07:00'), ())",
      "dateTime:2002-03-07T10:00:00" },
    { "adjust-date-to-timezone(xs:date('2002-03-07'), " TZ_MINUS_10 ")",
      "date:2002-03-07-10:00" },
    { "adjust-date-to-timezone(xs:date('2002-03-07-07:00'), " TZ_MINUS_10
      ")", "date:2002-03-06-10:00" },
    { "adjust-date-to-timezone(xs:date('2002-03-07'), ())",
      "date:2002-03-07" },
    { "adjust-date-to-timezone(xs:date('2002-03-07-07:00'), ())",
      "date:2002-03-07" },
    { "adjust-time-to-timezone(xs:time('10:00:00'), " TZ_MINUS_10 ")",
      "time:10:00:00-10:00" },
    { "adjust-time-to-timezone(xs:time('10:00:00-07:00'), " TZ_MINUS_10 ")",
      "time:07:00:00-10:00" },
    { "adjust-time-to-timezone(xs:time('10:00:00'), ())", "time:10:00:00" },
    { "adjust-time-to-timezone(xs:time('10:00:00-07:00'), ())",
      "time:10:00:00" },
    { "adjust-time-to-timezone(xs:time('10:00:00-07:00'), "
      "xs:dayTimeDuration('PT10H'))", "time:03:00:00+10:00" },

    /* The examples of 9.5 that adjust to UTC */
    { "month-from-dateTime(adjust-dateTime-to-timezone("
      "xs:dateTime('1999-12-31T19:20:00-05:00'), "
      "xs:dayTimeDuration('PT0S')))", "integer:1" },
    { "day-from-dateTime(adjust-dateTime-to-timezone("
      "xs:dateTime('1999-12-31T19:20:00-05:00'), "
      "xs:dayTimeDuration('PT0S')))", "integer:1" },
    { "hours-from-dateTime(adjust-dateTime-to-timezone("
      "xs:dateTime('1999-12-31T21:20:00-05:00'), "
      "xs:dayTimeDuration('PT0S')))", "integer:2" },
    { "hours-from-time(adjust-time-to-timezone(xs:time('01:23:00+05:00'), "
      "xs:dayTimeDuration('PT0S')))", "integer:20" },

    /* Across midnight, the year and year 0 */
    { "adjust-date-to-timezone(xs:date('2026-01-01-14:00'), "
      "xs:dayTimeDuration('PT14H'))", "date:2026-01-02+14:00" },
    { "adjust-date-to-timezone(xs:date('2026-01-01+14:00'), "
      "xs:dayTimeDuration('-PT14H'))", "date:2025-12-30-14:00" },
    { "adjust-date-to-timezone(xs:date('2026-01-01+01:00'), "
      "xs:dayTimeDuration('PT0S'))", "date:2025-12-31Z" },
    { "adjust-date-to-timezone(xs:date('2026-01-01-01:00'), "
      "xs:dayTimeDuration('PT0S'))", "date:2026-01-01Z" },
    { "adjust-date-to-timezone(xs:date('0000-01-01Z'), "
      "xs:dayTimeDuration('-PT1H'))", "date:-0001-12-31-01:00" },
    { "adjust-dateTime-to-timezone("
      "xs:dateTime('-0001-12-31T23:30:00-00:30'), "
      "xs:dayTimeDuration('PT0S'))", "dateTime:0000-01-01T00:00:00Z" },
    { "adjust-dateTime-to-timezone(xs:dateTime('2000-02-28T24:00:00Z'), "
      "xs:dayTimeDuration('-PT0M'))", "dateTime:2000-02-29T00:00:00Z" },
    { "adjust-time-to-timezone(xs:time('24:00:00+01:00'), "
      "xs:dayTimeDuration('PT0S'))", "time:23:00:00Z" },
    { "adjust-time-to-timezone(xs:time('23:59:59.5-14:00'), "
      "xs:dayTimeDuration('PT14H'))", "time:03:59:59.5+14:00" },
    { "adjust-dateTime-to-timezone(xs:dateTime('2026-01-01T00:00:00-00:00'),"
      " ())", "dateTime:2026-01-01T00:00:00" },
    { "adjust-dateTime-to-timezone(xs:dateTime('2026-01-01T00:00:00Z'), "
      "xs:dayTimeDuration('PT5H45M'))", "dateTime:2026-01-01T05:45:00+05:45" },

    /* An xs:dateTimeStamp in, an xs:dateTime out */
    { "adjust-dateTime-to-timezone("
      "xs:dateTimeStamp('2026-01-01T00:00:00Z'), ())",
      "dateTime:2026-01-01T00:00:00" },

    /* Beyond the year limits */
    { "adjust-dateTime-to-timezone("
      "xs:dateTime('2147483647-12-31T23:00:00Z'), "
      "xs:dayTimeDuration('PT14H'))", "error:FODT0001" },
    { "adjust-date-to-timezone(xs:date('-2147483647-01-01Z'), "
      "xs:dayTimeDuration('-PT1M'))", "error:FODT0001" },
    { "adjust-date-to-timezone(xs:date('2147483647-12-31-14:00'), "
      "xs:dayTimeDuration('PT14H'))", "error:FODT0001" },
    { "adjust-dateTime-to-timezone("
      "xs:dateTime('2147483647-12-31T23:00:00'), "
      "xs:dayTimeDuration('PT14H'))",
      "dateTime:2147483647-12-31T23:00:00+14:00" },

    /* Timezones that aren't (FODT0003) */
    { "adjust-dateTime-to-timezone(xs:dateTime('2026-01-01T00:00:00Z'), "
      "xs:dayTimeDuration('PT14H1M'))", "error:FODT0003" },
    { "adjust-date-to-timezone(xs:date('2026-01-01'), "
      "xs:dayTimeDuration('-PT14H1M'))", "error:FODT0003" },
    { "adjust-time-to-timezone(xs:time('00:00:00'), "
      "xs:dayTimeDuration('PT1S'))", "error:FODT0003" },
    { "adjust-time-to-timezone(xs:time('00:00:00Z'), "
      "xs:dayTimeDuration('PT0.000000001S'))", "error:FODT0003" },
    { "adjust-date-to-timezone(xs:date('2026-01-01'), "
      "xs:dayTimeDuration('P1D'))", "error:FODT0003" },
    { "adjust-date-to-timezone(xs:date('2026-01-01'), "
      "xs:dayTimeDuration('-PT14H'))", "date:2026-01-01-14:00" },
    { "adjust-time-to-timezone(xs:time('12:00:00Z'), "
      "xs:dayTimeDuration('PT14H'))", "time:02:00:00+14:00" },

    /* The empty sequence, and the wrong types */
    { "adjust-dateTime-to-timezone((), ())", "" },
    { "adjust-date-to-timezone((), xs:dayTimeDuration('PT1H'))", "" },
    { "adjust-time-to-timezone((), ())", "" },
    { "adjust-date-to-timezone(xs:date('2026-01-01'), "
      "xs:yearMonthDuration('P0M'))", "error:XPTY0004" },
    { "adjust-time-to-timezone(xs:time('00:00:00'), 'PT1H')",
      "error:XPTY0004" },
    { "adjust-date-to-timezone(xs:dateTime('2026-01-01T00:00:00'), ())",
      "error:XPTY0004" }
};

static void
testAdjustToTimezone(void) {
    CHECK_CASES(adjustCases);
}

/*
 * Folding: a call folds unless it reads the clock or the implicit
 * timezone, which only an evaluation has
 */

/* Whether an expression compiles to a constant */
static int
isFolded(const char *expr) {
    xp3Expr *compiled = xp3Compile(NULL, BAD_CAST expr, NULL);
    int folded;

    if (compiled == NULL) {
        xp3TestFail("%s doesn't compile", expr);
        return(-1);
    }
    folded = (compiled->ast->root->kind == XP3_AST_CONSTANT);
    xp3FreeExpr(compiled);
    return(folded);
}

static void
testFolding(void) {
    xp3DynamicContext *ctx = xp3NewDynamicContext();

    check(isFolded("year-from-date(xs:date('2026-01-01'))") == 1,
          "year-from-date of a literal is folded");
    check(isFolded("adjust-date-to-timezone(xs:date('2026-01-01'), ())") ==
          1, "adjust-date-to-timezone#2 is folded");
    check(isFolded("adjust-dateTime-to-timezone("
                   "xs:dateTime('2026-01-01T00:00:00Z'), "
                   "xs:dayTimeDuration('PT1H'))") == 1,
          "so is adjusting to a timezone given");
    check(isFolded("timezone-from-time(xs:time('10:00:00'))") == 1,
          "the timezone of a value without one is folded");
    check(isFolded("dateTime(xs:date('2026-01-01'), xs:time('10:00:00'))") ==
          1, "fn:dateTime is folded");
    check(isFolded("seconds-from-duration(xs:dayTimeDuration('PT1.5S'))") ==
          1, "seconds-from-duration is folded");
    check(isFolded("adjust-date-to-timezone(xs:date('2026-01-01'))") == 0,
          "adjust-date-to-timezone#1 is not folded");
    check(isFolded("adjust-dateTime-to-timezone("
                   "xs:dateTime('2026-01-01T00:00:00'))") == 0,
          "nor adjust-dateTime-to-timezone#1");
    check(isFolded("adjust-time-to-timezone(xs:time('00:00:00'))") == 0,
          "nor adjust-time-to-timezone#1");
    check(isFolded("adjust-date-to-timezone(xs:date('2026-01-01'), "
                   "xs:dayTimeDuration('PT15H'))") == 0,
          "an error stays for the evaluation");

    if ((ctx == NULL) || (xp3SetImplicitTimezone(ctx, 840) < 0)) {
        check(0, "context created");
        xp3FreeDynamicContext(ctx);
        return;
    }
    checkEval(NULL, ctx, "adjust-date-to-timezone(xs:date('2026-01-01'))",
              "date:2026-01-01+14:00");
    xp3SetImplicitTimezone(ctx, -840);
    checkEval(NULL, ctx, "adjust-date-to-timezone(xs:date('2026-01-01'))",
              "date:2026-01-01-14:00");
    checkEval(NULL, ctx, "adjust-date-to-timezone(xs:date('2026-01-01'), "
              "xs:dayTimeDuration('PT15H'))", "error:FODT0003");
    xp3FreeDynamicContext(ctx);
}

int
main(void) {
    xmlInitParser();

    XP3_TEST_RUN(testDurationComponents);
    XP3_TEST_RUN(testDateTimeComponents);
    XP3_TEST_RUN(testDateTimeConstructor);
    XP3_TEST_RUN(testAdjustToTimezone);
    XP3_TEST_RUN(testFolding);

    return(xp3TestDone());
}
