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

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/xmlmemory.h>

#include <libxpath3/xpath3.h>
#include "private/xp3context.h"
#include "private/xp3datetime.h"
#include "private/xp3eval.h"
#include "private/xp3static.h"
#include "private/xp3types.h"
#include "private/xp3value.h"
#include "testalloc.h"
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

        if (value == NULL) {
            snprintf(out, size, "error:NO_MEMORY");
            return;
        }
        if (type < 0)
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
    check(isFolded("current-date()") == 0, "current-date() is not folded");
    check(isFolded("year-from-date(current-date())") == 0,
          "nor anything that uses it");
    check(isFolded("current-dateTime()") == 0, "nor current-dateTime()");
    check(isFolded("current-time()") == 0, "nor current-time()");
    check(isFolded("implicit-timezone()") == 0,
          "nor implicit-timezone()");
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

/*
 * The clock (F&O 3.1, 15.3 to 15.6)
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

#define XP3_CLOCK_CALLS \
    "current-dateTime(), current-date(), current-time(), " \
    "implicit-timezone()"

/* Evaluate an expression and count the clock readings */
static int
readingsOf(xp3DynamicContext *ctx, const char *expr, const char *expected) {
    clockReads = 0;
    checkEval(NULL, ctx, expr, expected);
    return(clockReads);
}

static void
testSetClock(void) {
    xp3DynamicContext *ctx = xp3NewDynamicContext();

    if (ctx == NULL) {
        check(0, "context created");
        return;
    }
    ctx->clockSource = countingClock;

    /* The clock read, in its local offset */
    check(readingsOf(ctx, XP3_CLOCK_CALLS ", " XP3_CLOCK_CALLS,
                     "dateTimeStamp:2026-11-19T15:55:47.050651+05:30 "
                     "date:2026-11-19+05:30 time:15:55:47.050651+05:30 "
                     "dayTimeDuration:PT5H30M "
                     "dateTimeStamp:2026-11-19T15:55:47.050651+05:30 "
                     "date:2026-11-19+05:30 time:15:55:47.050651+05:30 "
                     "dayTimeDuration:PT5H30M") == 1,
          "the clock is read once for every call");
    check(readingsOf(ctx, "current-dateTime() instance of "
                     "xs:dateTimeStamp", "boolean:true") == 1,
          "the current dateTime is an xs:dateTimeStamp");
    check(readingsOf(ctx, "year-from-date(xs:date('2026-01-01'))",
                     "integer:2026") == 0,
          "other functions don't read the clock");

    /* An implicit timezone set moves the clock to it */
    xp3SetImplicitTimezone(ctx, 0);
    check(readingsOf(ctx, XP3_CLOCK_CALLS,
                     "dateTimeStamp:2026-11-19T10:25:47.050651Z "
                     "date:2026-11-19Z time:10:25:47.050651Z "
                     "dayTimeDuration:PT0S") == 1,
          "the clock in the implicit timezone set");
    xp3SetImplicitTimezone(ctx, -840);
    checkEval(NULL, ctx, XP3_CLOCK_CALLS,
              "dateTimeStamp:2026-11-18T20:25:47.050651-14:00 "
              "date:2026-11-18-14:00 time:20:25:47.050651-14:00 "
              "dayTimeDuration:-PT14H");
    check(readingsOf(ctx, "implicit-timezone()",
                     "dayTimeDuration:-PT14H") == 0,
          "the implicit timezone set doesn't read the clock");

    /* Back to the local offset */
    xp3SetImplicitTimezone(ctx, XP3_TIMEZONE_LOCAL);
    checkEval(NULL, ctx, "implicit-timezone()", "dayTimeDuration:PT5H30M");

    /* A current dateTime set, alone and with an implicit timezone */
    check(xp3SetCurrentDateTime(ctx, BAD_CAST
                                "2026-11-24T10:20:30.123456789+01:00") == 0,
          "a current dateTime is set");
    check(readingsOf(ctx, XP3_CLOCK_CALLS,
                     "dateTimeStamp:2026-11-24T10:20:30.123456789+01:00 "
                     "date:2026-11-24+01:00 time:10:20:30.123456789+01:00 "
                     "dayTimeDuration:PT1H") == 0,
          "exactly the dateTime set, without reading the clock");
    xp3SetImplicitTimezone(ctx, -300);
    checkEval(NULL, ctx, XP3_CLOCK_CALLS,
              "dateTimeStamp:2026-11-24T04:20:30.123456789-05:00 "
              "date:2026-11-24-05:00 time:04:20:30.123456789-05:00 "
              "dayTimeDuration:-PT5H");
    checkEval(NULL, ctx, "current-dateTime() eq "
              "xs:dateTime('2026-11-24T10:20:30.123456789+01:00')",
              "boolean:true");
    xp3SetCurrentDateTime(ctx, BAD_CAST "2026-01-01T02:00:00+01:00");
    xp3SetImplicitTimezone(ctx, -840);
    checkEval(NULL, ctx, "current-date(), current-time()",
              "date:2025-12-31-14:00 time:11:00:00-14:00");
    xp3SetCurrentDateTime(ctx, BAD_CAST "-0001-12-31T23:00:00Z");
    xp3SetImplicitTimezone(ctx, 60);
    checkEval(NULL, ctx, "current-dateTime(), year-from-date(current-date())",
              "dateTimeStamp:0000-01-01T00:00:00+01:00 integer:0");
    xp3SetCurrentDateTime(ctx, BAD_CAST "2147483647-12-31T23:00:00Z");
    xp3SetImplicitTimezone(ctx, 840);
    checkEval(NULL, ctx, "current-dateTime()", "error:FODT0001");
    checkEval(NULL, ctx, "implicit-timezone()", "dayTimeDuration:PT14H");
    xp3SetImplicitTimezone(ctx, XP3_TIMEZONE_LOCAL);
    checkEval(NULL, ctx, XP3_CLOCK_CALLS,
              "dateTimeStamp:2147483647-12-31T23:00:00Z "
              "date:2147483647-12-31Z time:23:00:00Z dayTimeDuration:PT0S");

    /* The functions that need the implicit timezone use the same */
    xp3SetCurrentDateTime(ctx, NULL);
    check(readingsOf(ctx, "adjust-time-to-timezone(xs:time('00:00:00')), "
                     "current-time()",
                     "time:00:00:00+05:30 time:15:55:47.050651+05:30") == 1,
          "adjust-time-to-timezone#1 and the clock share one reading");
    xp3FreeDynamicContext(ctx);
}

/* The number of digits after the point of the seconds of a lexical
 * dateTime with a timezone, such as "...T10:20:30.123456+01:00" */
static int
fractionDigits(const char *lexical) {
    const char *point = strchr(lexical, '.');
    int digits = 0;

    if (point == NULL)
        return(0);
    point++;
    while ((*point >= '0') && (*point <= '9')) {
        digits++;
        point++;
    }
    return(digits);
}

static void
testSystemClock(void) {
    xp3DynamicContext *ctx = xp3NewDynamicContext();
    xp3ClockReading reading;
    char first[128];
    char next[128];
    char expected[64];
    const char *minus = "";
    int offset;
    int i;

    if (ctx == NULL) {
        check(0, "context created");
        return;
    }
    checkEval(NULL, ctx, "let $now := current-dateTime() return "
              "(for $i in 1 to 100000 return $i)[last()] ! "
              "(current-dateTime() eq $now and "
              "current-date() eq xs:date($now) and "
              "current-time() eq xs:time($now))", "boolean:true");
    checkEval(NULL, ctx, "timezone-from-dateTime(current-dateTime()) eq "
              "implicit-timezone()", "boolean:true");

    /* Microseconds, which change between evaluations */
    run(NULL, ctx, "string(current-dateTime())", first, sizeof(first));
    check(fractionDigits(first) <= 6, "no more than microseconds");
    for (i = 0; i < 1000000; i++) {
        run(NULL, ctx, "string(current-dateTime())", next, sizeof(next));
        if (strcmp(first, next) != 0)
            break;
    }
    check(i < 1000000, "two evaluations get different dateTimes");

    /* XP3_TIMEZONE_LOCAL goes back to the local offset */
    xp3SetImplicitTimezone(ctx, 840);
    checkEval(NULL, ctx, "implicit-timezone()", "dayTimeDuration:PT14H");
    xp3SetImplicitTimezone(ctx, XP3_TIMEZONE_LOCAL);
    xp3SystemClock(&reading);
    offset = reading.offset;
    if (offset < 0) {
        minus = "-";
        offset = -offset;
    }
    if (offset == 0)
        snprintf(expected, sizeof(expected), "dayTimeDuration:PT0S");
    else if (offset % 60 == 0)
        snprintf(expected, sizeof(expected), "dayTimeDuration:%sPT%dH",
                 minus, offset / 60);
    else if (offset < 60)
        snprintf(expected, sizeof(expected), "dayTimeDuration:%sPT%dM",
                 minus, offset);
    else
        snprintf(expected, sizeof(expected), "dayTimeDuration:%sPT%dH%dM",
                 minus, offset / 60, offset % 60);
    checkEval(NULL, ctx, "implicit-timezone()", expected);
    xp3FreeDynamicContext(ctx);
}

/*
 * fn:parse-ietf-date (F&O 3.1, 9.9.1)
 */

#define IETF(input) "parse-ietf-date('" input "')"

static const xp3EvalCase ietfCases[] = {
    /* The example of 9.9.1 */
    { IETF("Wed, 06 Jun 1994 07:29:35 GMT"),
      "dateTime:1994-06-06T07:29:35Z" },

    /* The three formats: RFC 1123, RFC 850 and asctime() */
    { IETF("Sun, 06 Nov 1994 08:49:37 GMT"),
      "dateTime:1994-11-06T08:49:37Z" },
    { IETF("Sunday, 06-Nov-94 08:49:37 GMT"),
      "dateTime:1994-11-06T08:49:37Z" },
    { IETF("Sun Nov  6 08:49:37 1994"), "dateTime:1994-11-06T08:49:37Z" },

    /* Optional parts, case and whitespace */
    { IETF(" \t\n\r20 Aug 2014 19:36:01 GMT \t"),
      "dateTime:2014-08-20T19:36:01Z" },
    { IETF("Wed 20 Aug 2014 19:36:01 GMT"),
      "dateTime:2014-08-20T19:36:01Z" },
    { IETF("WEDNESDAY, 20 AUG 2014 19:36:01 gmt"),
      "dateTime:2014-08-20T19:36:01Z" },
    { IETF("wed, 20 aug 2014 19:36"), "dateTime:2014-08-20T19:36:00Z" },
    { IETF("Wed, 20 Aug 2014 19:36:01GMT"),
      "dateTime:2014-08-20T19:36:01Z" },
    { IETF("Wed, 20 - Aug - 2014 19:36"), "dateTime:2014-08-20T19:36:00Z" },
    { IETF("Wed, 20-Aug -2014 9:36"), "dateTime:2014-08-20T09:36:00Z" },
    { IETF("Tue, 9 Sep 2014 19:36:01.25 UT"),
      "dateTime:2014-09-09T19:36:01.25Z" },
    { IETF("Aug-20 19:36 2014"), "dateTime:2014-08-20T19:36:00Z" },
    { IETF("Wed, Aug 20 14:36:01EST 2014"),
      "dateTime:2014-08-20T14:36:01-05:00" },
    { IETF("1 Jan 2026 00:00:00.12345678912 UTC"),
      "dateTime:2026-01-01T00:00:00.123456789Z" },

    /* Every tzname */
    { IETF("1 Jan 2026 12:00 EDT"), "dateTime:2026-01-01T12:00:00-04:00" },
    { IETF("1 Jan 2026 12:00 CST"), "dateTime:2026-01-01T12:00:00-06:00" },
    { IETF("1 Jan 2026 12:00 CDT"), "dateTime:2026-01-01T12:00:00-05:00" },
    { IETF("1 Jan 2026 12:00 MST"), "dateTime:2026-01-01T12:00:00-07:00" },
    { IETF("1 Jan 2026 12:00 MDT"), "dateTime:2026-01-01T12:00:00-06:00" },
    { IETF("1 Jan 2026 12:00 PST"), "dateTime:2026-01-01T12:00:00-08:00" },
    { IETF("1 Jan 2026 12:00 pdt"), "dateTime:2026-01-01T12:00:00-07:00" },

    /* Offsets: H, HH, HMM, HHMM, with a colon, and a tzname ignored */
    { IETF("Wed, 20 Aug 2014 14:36:01 -5"),
      "dateTime:2014-08-20T14:36:01-05:00" },
    { IETF("Wed, 20 Aug 2014 14:36:01+05"),
      "dateTime:2014-08-20T14:36:01+05:00" },
    { IETF("Wed, 20 Aug 2014 14:36:01 -530"),
      "dateTime:2014-08-20T14:36:01-05:30" },
    { IETF("Wed, 20 Aug 2014 14:36:01 +0545"),
      "dateTime:2014-08-20T14:36:01+05:45" },
    { IETF("Wed, 20 Aug 2014 14:36:01 -5:30"),
      "dateTime:2014-08-20T14:36:01-05:30" },
    { IETF("Wed, 20 Aug 2014 14:36:01-05:"),
      "dateTime:2014-08-20T14:36:01-05:00" },
    { IETF("Wed, 20 Aug 2014 14:36:01 -05:00 ( EST )"),
      "dateTime:2014-08-20T14:36:01-05:00" },
    { IETF("Wed, 20 Aug 2014 14:36:01 +0500(GMT)"),
      "dateTime:2014-08-20T14:36:01+05:00" },
    { IETF("Aug-20 14:36:01-05(EST) 14"),
      "dateTime:1914-08-20T14:36:01-05:00" },
    { IETF("Feb-02 02:02-02: 02"), "dateTime:1902-02-02T02:02:00-02:00" },
    { IETF("1 Jan 2026 00:00 +14:00"), "dateTime:2026-01-01T00:00:00+14:00" },
    { IETF("1 Jan 2026 00:00 -1400"), "dateTime:2026-01-01T00:00:00-14:00" },
    { IETF("1 Jan 2026 00:00 -00:00"), "dateTime:2026-01-01T00:00:00Z" },

    /* Two-digit years are in the 1900s; four digits are as written */
    { IETF("20 Aug 14 19:36:01"), "dateTime:1914-08-20T19:36:01Z" },
    { IETF("28 Feb 00 00:00"), "dateTime:1900-02-28T00:00:00Z" },
    { IETF("29 Feb 00 00:00"), "error:FORG0010" },
    { IETF("29 Feb 2000 00:00"), "dateTime:2000-02-29T00:00:00Z" },
    { IETF("1 Jan 0070 00:00"), "dateTime:0070-01-01T00:00:00Z" },
    { IETF("29 Feb 0000 00:00"), "dateTime:0000-02-29T00:00:00Z" },

    /* 24:00 is midnight of the next day */
    { IETF("Aug 20 24:00:00 2014"), "dateTime:2014-08-21T00:00:00Z" },
    { IETF("31 Dec 9999 24:00 +0100"),
      "dateTime:10000-01-01T00:00:00+01:00" },

    /* Not the grammar, or not a valid dateTime (FORG0010) */
    { IETF(""), "error:FORG0010" },
    { IETF("   "), "error:FORG0010" },
    { IETF("2014-08-20T19:36:01Z"), "error:FORG0010" },
    { IETF("Wed,20 Aug 2014 19:36:01"), "error:FORG0010" },
    { IETF("Wed, 020 Aug 2014 19:36:01 GMT"), "error:FORG0010" },
    { IETF("Wed, 20 August 2014 19:36:01 GMT"), "error:FORG0010" },
    { IETF("Wed, 20 Aug 114 19:36:01 GMT"), "error:FORG0010" },
    { IETF("Wed, 20 Aug 02014 19:36:01 GMT"), "error:FORG0010" },
    { IETF("Wed, 20 Aug 2014 19:36:01 CET"), "error:FORG0010" },
    { IETF("Mon, 32 Aug 2014 19:36:01 GMT"), "error:FORG0010" },
    { IETF("Wed, 00 Aug 2014 19:36:01 GMT"), "error:FORG0010" },
    { IETF("Sat, 29 Feb 2014 19:36:01 GMT"), "error:FORG0010" },
    { IETF("Feb 28 19:36:01 GMT -2000"), "error:FORG0010" },
    { IETF("Mond, 20 Aug 2014 19:36"), "error:FORG0010" },
    { IETF("Aug,20 19:36:01 2014"), "error:FORG0010" },
    { IETF("Aug20 19:36:01 2014"), "error:FORG0010" },
    { IETF("20 Aug2014 19:36:01"), "error:FORG0010" },
    { IETF("Aug 20 19:36:01GMT2014"), "error:FORG0010" },
    { IETF("Wed, 20 Aug 2014 19:36:01 GMT Manchester"), "error:FORG0010" },
    { IETF("Aug 20 19:3:01GMT 2014"), "error:FORG0010" },
    { IETF("Aug 20 19:36:0.1GMT 2014"), "error:FORG0010" },
    { IETF("Aug 20 19:36:01.GMT 2014"), "error:FORG0010" },
    { IETF("Aug 20 19:36:01 -05:0 2014"), "error:FORG0010" },
    { IETF("Aug 20 19:36:01 -05:00 () 2014"), "error:FORG0010" },
    { IETF("Aug 20 19:36:01 -05:00 (CET) 2014"), "error:FORG0010" },
    { IETF("Aug 20 19:36:01 -05:00 (EST 2014"), "error:FORG0010" },
    { IETF("Aug 20 19:36:01 -05:00 EST 2014"), "error:FORG0010" },
    { IETF("Aug 20 19:36(EST) 2014"), "error:FORG0010" },
    { IETF("Aug 20 29:36:01GMT 2014"), "error:FORG0010" },
    { IETF("Aug 20 24:00:01 2014"), "error:FORG0010" },
    { IETF("Aug 20 19:60:01GMT 2014"), "error:FORG0010" },
    { IETF("Aug 20 19:36:60GMT 2014"), "error:FORG0010" },
    { IETF("Aug 20 19:36:01 -15:00 2014"), "error:FORG0010" },
    { IETF("Aug 20 19:36:01 +14:01 2014"), "error:FORG0010" },
    { IETF("Aug 20 19:36:01 -05:60 2014"), "error:FORG0010" },
    { IETF("Aug 20 19:36:01 -05000 2014"), "error:FORG0010" },
    { IETF("Aug 20 19:36:01 - 2014"), "error:FORG0010" },
    { IETF("Aug 20 19:36:01 2014 GMT"), "error:FORG0010" },

    /* The empty sequence */
    { "parse-ietf-date(())", "" }
};

static void
testParseIetfDate(void) {
    xp3StaticContext *level30 = xp3NewStaticContext();
    xp3DynamicContext *ctx = xp3NewDynamicContext();

    CHECK_CASES(ietfCases);
    check(isFolded(IETF("Wed, 06 Jun 1994 07:29:35 GMT")) == 1,
          "parse-ietf-date is folded");

    /* Only XPath 3.1 has it */
    if ((level30 == NULL) || (ctx == NULL) ||
        (xp3SetStaticContextLanguageLevel(level30, XP3_XPATH_30) < 0)) {
        check(0, "contexts created");
    } else {
        checkEval(level30, ctx, IETF("Wed, 06 Jun 1994 07:29:35 GMT"),
                  "error:XPST0017");
    }
    xp3FreeStaticContext(level30);
    xp3FreeDynamicContext(ctx);
}

/*
 * Allocation failures
 */

/* Every function, unfolded, on a clock that is set, as strings */
static const char xp3AllocExpr[] =
    "string-join(("
    "years-from-duration(xs:duration('-P1Y2M')), "
    "months-from-duration(xs:duration('-P1Y2M')), "
    "days-from-duration(xs:dayTimeDuration('P3DT4H')), "
    "hours-from-duration(xs:dayTimeDuration('P3DT4H')), "
    "minutes-from-duration(xs:dayTimeDuration('PT5M')), "
    "seconds-from-duration(xs:dayTimeDuration('-PT1.5S')), "
    "year-from-dateTime(xs:dateTime('2026-11-24T10:20:30.25+05:30')), "
    "month-from-date(xs:date('2026-11-24')), "
    "day-from-date(xs:date('2026-11-24')), "
    "hours-from-time(xs:time('10:20:30')), "
    "minutes-from-dateTime(xs:dateTime('2026-11-24T10:20:30')), "
    "seconds-from-dateTime(xs:dateTime('2026-11-24T10:20:30.25')), "
    "timezone-from-dateTime(xs:dateTime('2026-11-24T10:20:30+05:30')), "
    "dateTime(xs:date('2026-11-24'), xs:time('10:20:30Z')), "
    "adjust-date-to-timezone(xs:date('2026-11-24+01:00'), "
    "xs:dayTimeDuration('PT0S')), "
    "adjust-dateTime-to-timezone(xs:dateTime('2026-11-24T10:20:30')), "
    "adjust-time-to-timezone(xs:time('10:20:30Z'), ()), "
    "current-dateTime(), current-date(), current-time(), "
    "implicit-timezone(), "
    "parse-ietf-date('Wed, 06 Jun 1994 07:29:35 GMT')"
    ") ! string(.), ' ')";

static const char xp3AllocExpected[] =
    "string:-1 -2 3 4 5 -1.5 2026 11 24 10 20 30.25 PT5H30M "
    "2026-11-24T10:20:30Z 2026-11-23Z 2026-11-24T10:20:30-03:00 "
    "10:20:30 2026-11-24T10:20:30.5-03:00 2026-11-24-03:00 "
    "10:20:30.5-03:00 -PT3H 1994-06-06T07:29:35Z";

static void
testAllocationFailures(void) {
    xp3StaticContext *plain = xp3NewStaticContext();
    xp3DynamicContext *ctx = xp3NewDynamicContext();
    char actual[1024];
    int i;

    if ((plain == NULL) || (ctx == NULL) ||
        (xp3SetCurrentDateTime(ctx, BAD_CAST
                               "2026-11-24T10:20:30.5-03:00") < 0)) {
        check(0, "contexts created");
        goto done;
    }
    /* Not folded, so that the evaluation does the work */
    plain->noOptimize = 1;
    run(plain, ctx, xp3AllocExpr, actual, sizeof(actual));
    if (strcmp(actual, xp3AllocExpected) != 0) {
        xp3TestFail("the functions gave %s", actual);
        goto done;
    }

    if (!xp3TestAllocInstall()) {
        xp3TestSkip("this libxml2 ignores xmlMemSetup()");
        xp3TestAllocRestore();
        goto done;
    }
    for (i = 0; i < 5000; i++) {
        xp3TestAllocFailAt(i);
        run(plain, ctx, xp3AllocExpr, actual, sizeof(actual));
        if (!xp3TestAllocFailed()) {
            xp3TestAllocFailAt(-1);
            check(strcmp(actual, xp3AllocExpected) == 0,
                  "the functions complete after allocation failures");
            break;
        }
        xp3TestAllocFailAt(-1);
        if (strcmp(actual, "error:NO_MEMORY") != 0) {
            xp3TestFail("allocation failure %d gave %s", i, actual);
            break;
        }
    }
    check((i > 0) && (i < 5000), "allocation failure points exercised");
    xp3TestAllocRestore();

done:
    xp3FreeStaticContext(plain);
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
    XP3_TEST_RUN(testParseIetfDate);
    XP3_TEST_RUN(testSetClock);
    XP3_TEST_RUN(testSystemClock);
    XP3_TEST_RUN(testAllocationFailures);

    return(xp3TestDone());
}
