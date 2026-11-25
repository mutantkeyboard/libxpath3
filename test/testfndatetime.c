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

int
main(void) {
    xmlInitParser();

    XP3_TEST_RUN(testDurationComponents);

    return(xp3TestDone());
}
