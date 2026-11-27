/*
 * 05-dates-times.c: dates, times, durations and timezones, on a fixed
 * clock
 *
 * The program of doc/tutorial/05-dates-times.md. Run it with the
 * directory of the example documents:
 *
 *     05-dates-times examples/data
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdio.h>

#include <libxml/tree.h>
#include <libxml/xmlmemory.h>
#include <libxpath3/xpath3.h>

#include "common.h"

#define CLOCK "2026-11-25T10:30:00+01:00"

/* A query, and the error code it raises, or NULL if it succeeds */
typedef struct {
    const char *text;
    const char *expectedError;
} Query;

/* Queries whose answer depends on the implicit timezone */
static const Query zoneQueries[] = {
    { "implicit-timezone()", NULL },
    { "current-dateTime()", NULL },
    { "xs:dateTime('2026-03-29T08:00:00') eq "
      "xs:dateTime('2026-03-29T07:00:00Z')", NULL },
    { "//trip[depart castable as xs:dateTime]"
      "[xs:dateTime(arrive) lt xs:dateTime(depart)]/string(@id)", NULL },
    { "adjust-dateTime-to-timezone(xs:dateTime('2026-03-29T08:00:00'))",
      NULL }
};

static const Query queries[] = {
    /* Adding months keeps the day if it can, else takes the last one */
    { "xs:date('2024-02-29') + xs:yearMonthDuration('P1Y')", NULL },
    { "xs:date('2026-01-31') + xs:yearMonthDuration('P1M')", NULL },
    { "xs:date('2026-01-31') + xs:yearMonthDuration('P1M')"
      " + xs:yearMonthDuration('P1M')", NULL },
    { "xs:date('2026-01-31') + xs:yearMonthDuration('P2M')", NULL },
    /* Subtracting and comparing take both sides to UTC first */
    { "xs:dateTime('2026-03-28T11:30:00-12:00')"
      " - xs:dateTime('2026-03-29T09:00:00+14:00')", NULL },
    { "xs:time('23:30:00+14:00') lt xs:time('00:30:00-12:00')", NULL },
    { "distinct-values((xs:dateTime('2026-03-29T09:00:00+14:00'), "
      "xs:dateTime('2026-03-28T19:00:00Z')))", NULL },
    { "xs:date('2026-03-29') = xs:dateTime('2026-03-29T00:00:00')",
      "XPTY0004" },
    { "dateTime(xs:date('2026-03-29+01:00'), xs:time('08:00:00Z'))",
      "FORG0008" },
    /* Months and days have no fixed ratio */
    { "xs:duration('P1M') lt xs:duration('P30D')", "XPTY0004" },
    { "xs:yearMonthDuration('P1M') lt xs:yearMonthDuration('P2M')", NULL },
    { "xs:dayTimeDuration('P30D') lt xs:dayTimeDuration('P31D')", NULL },
    { "xs:duration('P1M') + xs:duration('P1D')", "XPTY0004" },
    { "xs:dayTimeDuration('P1D') * 1.5", NULL },
    { "xs:yearMonthDuration('P1Y') div xs:yearMonthDuration('P5M')", NULL },
    { "xs:duration('P1M') = xs:duration('P30D')", NULL },
    { "xs:duration('P1Y') = xs:duration('P12M')", NULL },
    { "xs:yearMonthDuration(//grace/@standard)"
      " = xs:dayTimeDuration(//grace/@short)", NULL },
    /* Invalid values, and a date where a dateTime is expected */
    { "xs:date('2025-02-30')", "FORG0001" },
    { "'2025-02-30' castable as xs:date", NULL },
    { "//trip[not(depart castable as xs:dateTime)]/string(@id)", NULL },
    { "//trip[@id = 't7']/dateTime(xs:date(depart), xs:time('00:00:00'))",
      NULL },
    { "xs:dateTime('2024-02-29T24:00:00Z')", NULL },
    { "xs:time('24:00:00')", NULL },
    { "xs:dateTime('2026-03-29T08:00:00+14:01')", "FORG0001" },
    /* Timezones: removing one, moving to one, and one out of range */
    { "adjust-dateTime-to-timezone("
      "xs:dateTime('2026-03-29T08:00:00+14:00'), ())", NULL },
    { "adjust-dateTime-to-timezone("
      "xs:dateTime('2026-03-29T08:00:00+14:00'), "
      "xs:dayTimeDuration('-PT12H'))", NULL },
    { "adjust-dateTime-to-timezone("
      "xs:dateTime('2026-03-29T08:00:00'), xs:dayTimeDuration('PT15H'))",
      "FODT0003" },
    /* fn:max casts untyped values to xs:double */
    { "max(//depart)", "FORG0001" },
    { "max(//depart[. castable as xs:dateTime] ! xs:dateTime(.))", NULL },
    { "min(//depart[. castable as xs:dateTime] ! xs:dateTime(.))", NULL },
    { "max((xs:date('2026-06-01'), xs:dateTime('2026-06-01T12:00:00Z')))",
      "FORG0006" },
    /* Components are local, never moved to UTC */
    { "day-from-dateTime(xs:dateTime('2026-03-28T23:30:00-12:00'))", NULL },
    { "day-from-dateTime(adjust-dateTime-to-timezone("
      "xs:dateTime('2026-03-28T23:30:00-12:00'), "
      "xs:dayTimeDuration('PT0S')))", NULL },
    { "day-from-date(xs:date('2026-03-29+14:00'))", NULL },
    { "timezone-from-date(xs:date('2026-03-29+14:00'))", NULL },
    { "timezone-from-dateTime(xs:dateTime('2026-03-29T08:00:00'))", NULL },
    /* Fractions of a second beyond nanoseconds, and the year 0 */
    { "xs:dateTime(//trip[@id = 't6']/arrive)", NULL },
    { "seconds-from-dateTime(xs:dateTime(//trip[@id = 't6']/arrive))",
      NULL },
    { "xs:dateTime(//trip[@id = 't6']/arrive)"
      " eq xs:dateTime(//trip[@id = 't6']/depart)", NULL },
    { "year-from-dateTime(xs:dateTime(//trip[@id = 't8']/arrive))", NULL },
    { "xs:date('2147483647-12-31') + xs:yearMonthDuration('P1Y')",
      "FODT0001" },
    /* The clock stands still during an evaluation */
    { "current-dateTime() eq current-dateTime()", NULL },
    { "current-date()", NULL }
};

/*
 * Compute the time of each trip with one compiled expression, with
 * each trip as the context node in turn.
 */
static int
tripTimes(const xp3StaticContext *sctx, xp3DynamicContext *dctx,
          xmlDocPtr doc) {
    xp3Expr *trips;
    xp3Expr *time;
    xp3Sequence *list = NULL;
    size_t i;
    int failures = 0;

    trips = xp3Compile(sctx, BAD_CAST "//trip", NULL);
    time = xp3Compile(sctx,
                      BAD_CAST "xs:dateTime(arrive) - xs:dateTime(depart)",
                      NULL);
    if ((trips == NULL) || (time == NULL)) {
        failures++;
        goto done;
    }
    list = xp3Evaluate(trips, dctx, NULL);
    if (list == NULL) {
        failures++;
        goto done;
    }

    for (i = 0; i < xp3SequenceLength(list); i++) {
        xmlNodePtr trip = xp3SequenceNode(list, i);
        xmlChar *id = xmlGetProp(trip, BAD_CAST "id");
        const char *expectedError = NULL;

        if (id == NULL) {
            failures++;
            continue;
        }
        /* t5 departs on 30 February, t7 has a date without a time */
        if (xmlStrEqual(id, BAD_CAST "t5") || xmlStrEqual(id, BAD_CAST "t7"))
            expectedError = "FORG0001";
        xp3SetContextNode(dctx, trip);
        failures += exEvaluate(time, dctx, (const char *) id, expectedError);
        xmlFree(id);
    }

done:
    xp3SetContextNode(dctx, (xmlNodePtr) doc);
    xp3FreeSequence(list);
    xp3FreeExpr(time);
    xp3FreeExpr(trips);
    return(failures);
}

static int
runQueries(const xp3StaticContext *sctx, xp3DynamicContext *dctx,
           const Query *list, size_t count) {
    size_t i;
    int failures = 0;

    for (i = 0; i < count; i++)
        failures += exRun(sctx, dctx, NULL, list[i].text,
                          list[i].expectedError);
    return(failures);
}

int
main(int argc, char **argv) {
    xmlDocPtr doc = NULL;
    xp3StaticContext *sctx = NULL;
    xp3DynamicContext *dctx = NULL;
    size_t zoneCount = sizeof(zoneQueries) / sizeof(zoneQueries[0]);
    int failures = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: %s DATA-DIRECTORY\n", argv[0]);
        return(2);
    }

    doc = exReadDocument(argv[1], "timetable.xml", 0);
    sctx = xp3NewStaticContext();
    dctx = xp3NewDynamicContext();
    if ((doc == NULL) || (sctx == NULL) || (dctx == NULL)) {
        failures++;
        goto done;
    }
    xp3SetContextNode(dctx, (xmlNodePtr) doc);

    /* A fixed clock, whose timezone is the implicit timezone too */
    if (xp3SetCurrentDateTime(dctx, BAD_CAST CLOCK) < 0) {
        failures++;
        goto done;
    }

    printf("Time of each trip, implicit timezone +01:00:\n");
    failures += tripTimes(sctx, dctx, doc);

    printf("Implicit timezone +01:00:\n");
    failures += runQueries(sctx, dctx, zoneQueries, zoneCount);

    /* Minutes from UTC, negative west of Greenwich */
    xp3SetImplicitTimezone(dctx, -5 * 60);
    printf("Implicit timezone -05:00:\n");
    failures += runQueries(sctx, dctx, zoneQueries, zoneCount);
    failures += tripTimes(sctx, dctx, doc);

    /* Back to the timezone of the clock */
    xp3SetImplicitTimezone(dctx, XP3_TIMEZONE_LOCAL);
    printf("Implicit timezone of the clock again:\n");
    failures += runQueries(sctx, dctx, queries,
                           sizeof(queries) / sizeof(queries[0]));

done:
    xp3FreeDynamicContext(dctx);
    xp3FreeStaticContext(sctx);
    xmlFreeDoc(doc);
    if (failures > 0)
        return(1);
    return(0);
}
