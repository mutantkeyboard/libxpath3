# 5. Dates, times and durations

Dates without a timezone, months of different lengths, durations that can't be compared, and a clock that differs from machine to machine: this page runs into each of them with a ferry timetable merged from ports that each wrote times their own way, and fixes the clock so that the program gives the same output everywhere.

You will learn:

- how to set the current dateTime and the implicit timezone, and why a test or a reproducible run needs both fixed
- how a value without a timezone is compared, and how the implicit timezone changes the answer
- month arithmetic at the end of a month, and why `P1M` and `P30D` can't be compared
- which strings are dates, and how to guard a cast
- that the component functions return the local value, not UTC

The full program is [`examples/05-dates-times.c`](../../examples/05-dates-times.c), its document [`examples/data/timetable.xml`](../../examples/data/timetable.xml), and its output [`examples/expected/05-dates-times.txt`](../../examples/expected/05-dates-times.txt).

## The data

```xml
<timetable>
  <trip id="t1">
    <depart>2026-03-29T01:30:00Z</depart>
    <arrive>2026-03-29T04:30:00+02:00</arrive>
  </trip>
  <trip id="t2">
    <depart>2026-03-29T09:00:00+14:00</depart>
    <arrive>2026-03-28T11:30:00-12:00</arrive>
  </trip>
  <trip id="t3">
    <depart>2026-03-29T08:00:00</depart>
    <arrive>2026-03-29T07:45:00Z</arrive>
  </trip>
  <trip id="t4">
    <depart>2024-02-29T23:00:00Z</depart>
    <arrive>2024-02-29T24:00:00Z</arrive>
  </trip>
  <trip id="t5">
    <depart>2025-02-30T10:00:00Z</depart>
    <arrive>2025-03-02T10:00:00Z</arrive>
  </trip>
  <trip id="t6">
    <depart>2026-06-01T12:00:00.123456789Z</depart>
    <arrive>2026-06-01T12:00:00.1234567899Z</arrive>
  </trip>
  <trip id="t7">
    <depart>2026-06-01</depart>
    <arrive>2026-06-01T18:00:00Z</arrive>
  </trip>
  <trip id="t8">
    <depart>-0001-12-31T23:00:00Z</depart>
    <arrive>0000-01-01T01:00:00Z</arrive>
  </trip>
  <grace standard="P1M" short="P30D"/>
</timetable>
```

What to look out for:

- The times are in UTC (`Z`), with offsets from `-12:00` to `+14:00`, or with **no timezone** (t3's departure).
- **t2** arrives on the 28th after departing on the 29th, by the dates as written.
- **t3** may arrive before it departs, depending on where its departure time was meant.
- **t4** departs on a leap day and arrives at `24:00:00`.
- **t5** departs on 30 February, **t7** has a date where a dateTime belongs.
- **t6** has nine and ten digits of fractions of a second.
- **t8** crosses from year -1 to year 0.
- The grace periods are a month and thirty days.

## A fixed clock

```c
    /* A fixed clock, whose timezone is the implicit timezone too */
    if (xp3SetCurrentDateTime(dctx, BAD_CAST CLOCK) < 0) {
        failures++;
        goto done;
    }
```

`CLOCK` is `"2026-11-25T10:30:00+01:00"`. Without this call, `current-dateTime()` reads the system clock and the implicit timezone is the machine's local offset, both taken once per evaluation (`doc/DESIGN.md` 5). Both are part of the dynamic context and "must be initialized" by the host ([XPath 3.1, C.2](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#id-xp-evaluation-context-components)): a program whose output must not depend on the machine sets them. The clock's timezone becomes the implicit timezone unless `xp3SetImplicitTimezone()` sets another.

## One expression per trip

```c
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
```

`time` is `xs:dateTime(arrive) - xs:dateTime(depart)`, compiled once. Evaluating it per row means a bad row fails alone, where one expression over all trips would fail as a whole.

```text
Time of each trip, implicit timezone +01:00:
t1 -> xs:dayTimeDuration PT1H
t2 -> xs:dayTimeDuration PT4H30M
t3 -> xs:dayTimeDuration PT45M
t4 -> xs:dayTimeDuration PT1H
t5 -> ERROR FORG0001
t6 -> xs:dayTimeDuration PT0S
t7 -> ERROR FORG0001
t8 -> xs:dayTimeDuration PT2H
```

**Why:** subtracting two dateTimes takes both to UTC first ([F&O 3.1, 9.7.2](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-subtract-dateTimes)). t2 departs at 19:00 UTC on the 28th and arrives at 23:30 UTC the same day, so the trip takes four and a half hours although the dates as written go backwards. t3's departure has no timezone, so the implicit timezone, +01:00, is used for it: 08:00 local is 07:00 UTC, 45 minutes before the arrival. t4's `24:00:00` is midnight at the start of the next day ([F&O 3.1, 9.2](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#date-time-values)). t6 is explained [below](#precision-year-0-and-the-limits).

## The implicit timezone

The same queries run twice, the second time after:

```c
    /* Minutes from UTC, negative west of Greenwich */
    xp3SetImplicitTimezone(dctx, -5 * 60);
```

```c
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
```

```text
Implicit timezone +01:00:
implicit-timezone() -> xs:dayTimeDuration PT1H
current-dateTime() -> xs:dateTimeStamp 2026-11-25T10:30:00+01:00
xs:dateTime('2026-03-29T08:00:00') eq xs:dateTime('2026-03-29T07:00:00Z') -> xs:boolean true
//trip[depart castable as xs:dateTime][xs:dateTime(arrive) lt xs:dateTime(depart)]/string(@id) -> ()
adjust-dateTime-to-timezone(xs:dateTime('2026-03-29T08:00:00')) -> xs:dateTime 2026-03-29T08:00:00+01:00
Implicit timezone -05:00:
implicit-timezone() -> xs:dayTimeDuration -PT5H
current-dateTime() -> xs:dateTimeStamp 2026-11-25T04:30:00-05:00
xs:dateTime('2026-03-29T08:00:00') eq xs:dateTime('2026-03-29T07:00:00Z') -> xs:boolean false
//trip[depart castable as xs:dateTime][xs:dateTime(arrive) lt xs:dateTime(depart)]/string(@id) -> xs:string "t3"
adjust-dateTime-to-timezone(xs:dateTime('2026-03-29T08:00:00')) -> xs:dateTime 2026-03-29T08:00:00-05:00
t1 -> xs:dayTimeDuration PT1H
t2 -> xs:dayTimeDuration PT4H30M
t3 -> xs:dayTimeDuration -PT5H15M
...
```

**Why:** a value without a timezone is compared and subtracted as if it had the implicit timezone ([F&O 3.1, 9.4](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#comp.datetime)). At +01:00, 08:00 is 07:00 UTC; at -05:00 it is 13:00 UTC. So the same document and the same query say on one machine that t3 is fine and on another that it arrives five hours before it departs, with a negative duration. `current-dateTime()` is the same instant in both runs, shown in the implicit timezone ([F&O 3.1, 15.3](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-current-dateTime)). `adjust-dateTime-to-timezone` with one argument gives a value without a timezone the implicit one, which is the explicit way to say what the comparisons do anyway ([9.6.1](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-adjust-dateTime-to-timezone)).

Afterwards `XP3_TIMEZONE_LOCAL` goes back to the default, which here is the clock's +01:00:

```c
    /* Back to the timezone of the clock */
    xp3SetImplicitTimezone(dctx, XP3_TIMEZONE_LOCAL);
```

## Month arithmetic

```c
static const Query queries[] = {
    /* Adding months keeps the day if it can, else takes the last one */
    { "xs:date('2024-02-29') + xs:yearMonthDuration('P1Y')", NULL },
    { "xs:date('2026-01-31') + xs:yearMonthDuration('P1M')", NULL },
    { "xs:date('2026-01-31') + xs:yearMonthDuration('P1M')"
      " + xs:yearMonthDuration('P1M')", NULL },
    { "xs:date('2026-01-31') + xs:yearMonthDuration('P2M')", NULL },
```

```text
xs:date('2024-02-29') + xs:yearMonthDuration('P1Y') -> xs:date 2025-02-28
xs:date('2026-01-31') + xs:yearMonthDuration('P1M') -> xs:date 2026-02-28
xs:date('2026-01-31') + xs:yearMonthDuration('P1M') + xs:yearMonthDuration('P1M') -> xs:date 2026-03-28
xs:date('2026-01-31') + xs:yearMonthDuration('P2M') -> xs:date 2026-03-31
```

**Why:** adding months moves the month and keeps the day, unless the new month is shorter, in which case it takes the month's last day ([F&O 3.1, 9.7.9](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-add-yearMonthDuration-to-date), which applies the algorithm of XML Schema). So month arithmetic is not associative: one month twice is 28 March, two months once is 31 March. Compute "the same day next month" from the original date each time, not from the previous result.

## Comparing across timezones

```c
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
```

```text
xs:dateTime('2026-03-28T11:30:00-12:00') - xs:dateTime('2026-03-29T09:00:00+14:00') -> xs:dayTimeDuration PT4H30M
xs:time('23:30:00+14:00') lt xs:time('00:30:00-12:00') -> xs:boolean true
distinct-values((xs:dateTime('2026-03-29T09:00:00+14:00'), xs:dateTime('2026-03-28T19:00:00Z'))) -> xs:dateTime 2026-03-29T09:00:00+14:00
xs:date('2026-03-29') = xs:dateTime('2026-03-29T00:00:00') -> ERROR XPTY0004
dateTime(xs:date('2026-03-29+01:00'), xs:time('08:00:00Z')) -> ERROR FORG0008
```

**Why:** 23:30 at +14:00 is 09:30 UTC, and 00:30 at -12:00 is 12:30 UTC, so the time that reads later is earlier ([F&O 3.1, 9.4.8](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-time-less-than)). Two dateTimes for the same instant are equal, so `distinct-values` keeps one of them; which one is implementation-dependent ([14.2.1](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-distinct-values)), and libxpath3 keeps the first. A date and a dateTime are different types and never compare, not even with `=` ([XPath 3.1, B.2](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#mapping)). `fn:dateTime` refuses a date and a time with different timezones ([F&O 3.1, 9.3.1](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-dateTime)).

## Durations

```c
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
```

```text
xs:duration('P1M') lt xs:duration('P30D') -> ERROR XPTY0004
xs:yearMonthDuration('P1M') lt xs:yearMonthDuration('P2M') -> xs:boolean true
xs:dayTimeDuration('P30D') lt xs:dayTimeDuration('P31D') -> xs:boolean true
xs:duration('P1M') + xs:duration('P1D') -> ERROR XPTY0004
xs:dayTimeDuration('P1D') * 1.5 -> xs:dayTimeDuration P1DT12H
xs:yearMonthDuration('P1Y') div xs:yearMonthDuration('P5M') -> xs:decimal 2.4
xs:duration('P1M') = xs:duration('P30D') -> xs:boolean false
xs:duration('P1Y') = xs:duration('P12M') -> xs:boolean true
xs:yearMonthDuration(//grace/@standard) = xs:dayTimeDuration(//grace/@short) -> xs:boolean false
```

**Why:** a month is 28 to 31 days, so a duration of months and one of days have no order. XPath splits durations into two subtypes that each have a total order, `xs:yearMonthDuration` and `xs:dayTimeDuration` ([F&O 3.1, 8.1](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#duration-subtypes)). Plain `xs:duration` has `eq` and `ne` only, no `lt` and no arithmetic ([XPath 3.1, B.2](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#mapping)): equal means the same months and the same seconds ([F&O 3.1, 8.2.5](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-duration-equal)), so a year equals twelve months and a month never equals thirty days. Decide which subtype a field holds and cast to it.

## Invalid values and guarded casts

```c
    /* Invalid values, and a date where a dateTime is expected */
    { "xs:date('2025-02-30')", "FORG0001" },
    { "'2025-02-30' castable as xs:date", NULL },
    { "//trip[not(depart castable as xs:dateTime)]/string(@id)", NULL },
    { "//trip[@id = 't7']/dateTime(xs:date(depart), xs:time('00:00:00'))",
      NULL },
    { "xs:dateTime('2024-02-29T24:00:00Z')", NULL },
    { "xs:time('24:00:00')", NULL },
    { "xs:dateTime('2026-03-29T08:00:00+14:01')", "FORG0001" },
```

```text
xs:date('2025-02-30') -> ERROR FORG0001
'2025-02-30' castable as xs:date -> xs:boolean false
//trip[not(depart castable as xs:dateTime)]/string(@id) -> xs:string "t5", xs:string "t7"
//trip[@id = 't7']/dateTime(xs:date(depart), xs:time('00:00:00')) -> xs:dateTime 2026-06-01T00:00:00
xs:dateTime('2024-02-29T24:00:00Z') -> xs:dateTime 2024-03-01T00:00:00Z
xs:time('24:00:00') -> xs:time 00:00:00
xs:dateTime('2026-03-29T08:00:00+14:01') -> ERROR FORG0001
```

**Why:** casting checks the calendar, not only the pattern, so 30 February is `FORG0001` ([F&O 3.1, 19.2](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#casting-from-strings)), and `castable as` asks the same question without failing. A dateTime needs a time; `fn:dateTime` combines a date with a time of your choosing, here without a timezone, because t7 had none. A timezone beyond ±14:00 is not a valid value.

## Moving between timezones

```c
    /* Timezones: removing one, moving to one, and one out of range */
    { "adjust-dateTime-to-timezone("
      "xs:dateTime('2026-03-29T08:00:00+14:00'), ())", NULL },
    { "adjust-dateTime-to-timezone("
      "xs:dateTime('2026-03-29T08:00:00+14:00'), "
      "xs:dayTimeDuration('-PT12H'))", NULL },
    { "adjust-dateTime-to-timezone("
      "xs:dateTime('2026-03-29T08:00:00'), xs:dayTimeDuration('PT15H'))",
      "FODT0003" },
```

```text
adjust-dateTime-to-timezone(xs:dateTime('2026-03-29T08:00:00+14:00'), ()) -> xs:dateTime 2026-03-29T08:00:00
adjust-dateTime-to-timezone(xs:dateTime('2026-03-29T08:00:00+14:00'), xs:dayTimeDuration('-PT12H')) -> xs:dateTime 2026-03-28T06:00:00-12:00
adjust-dateTime-to-timezone(xs:dateTime('2026-03-29T08:00:00'), xs:dayTimeDuration('PT15H')) -> ERROR FODT0003
```

**Why:** with `()` the timezone is removed and the local time kept; with a timezone the instant is kept and the local time moves, here across the date line to the previous day ([F&O 3.1, 9.6.1](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-adjust-dateTime-to-timezone)). A timezone beyond ±14 hours is `FODT0003`.

## Minimum and maximum

```c
    /* fn:max casts untyped values to xs:double */
    { "max(//depart)", "FORG0001" },
    { "max(//depart[. castable as xs:dateTime] ! xs:dateTime(.))", NULL },
    { "min(//depart[. castable as xs:dateTime] ! xs:dateTime(.))", NULL },
    { "max((xs:date('2026-06-01'), xs:dateTime('2026-06-01T12:00:00Z')))",
      "FORG0006" },
```

```text
max(//depart) -> ERROR FORG0001
max(//depart[. castable as xs:dateTime] ! xs:dateTime(.)) -> xs:dateTime 2026-06-01T12:00:00.123456789Z
min(//depart[. castable as xs:dateTime] ! xs:dateTime(.)) -> xs:dateTime -0001-12-31T23:00:00Z
max((xs:date('2026-06-01'), xs:dateTime('2026-06-01T12:00:00Z'))) -> ERROR FORG0006
```

**Why:** `fn:max` and `fn:min` treat untyped values as numbers ([F&O 3.1, 14.4.3](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-max)), so the column has to be cast to dateTimes first, after filtering out what isn't one. The result keeps the timezone it was written with. Values of different types that have no common order are `FORG0006`.

## Components are local

```c
    /* Components are local, never moved to UTC */
    { "day-from-dateTime(xs:dateTime('2026-03-28T23:30:00-12:00'))", NULL },
    { "day-from-dateTime(adjust-dateTime-to-timezone("
      "xs:dateTime('2026-03-28T23:30:00-12:00'), "
      "xs:dayTimeDuration('PT0S')))", NULL },
    { "day-from-date(xs:date('2026-03-29+14:00'))", NULL },
    { "timezone-from-date(xs:date('2026-03-29+14:00'))", NULL },
    { "timezone-from-dateTime(xs:dateTime('2026-03-29T08:00:00'))", NULL },
```

```text
day-from-dateTime(xs:dateTime('2026-03-28T23:30:00-12:00')) -> xs:integer 28
day-from-dateTime(adjust-dateTime-to-timezone(xs:dateTime('2026-03-28T23:30:00-12:00'), xs:dayTimeDuration('PT0S'))) -> xs:integer 29
day-from-date(xs:date('2026-03-29+14:00')) -> xs:integer 29
timezone-from-date(xs:date('2026-03-29+14:00')) -> xs:dayTimeDuration PT14H
timezone-from-dateTime(xs:dateTime('2026-03-29T08:00:00')) -> ()
```

**Why:** the component functions read the local value as written ([F&O 3.1, 9.5](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#component-extraction-dateTime)). The day of 23:30 at -12:00 is the 28th, although in UTC it is already the 29th; adjust to UTC first if you want the UTC day. A value without a timezone has no timezone component, which is the empty sequence, not zero.

## Precision, year 0 and the limits

```c
    /* Fractions of a second beyond nanoseconds, and the year 0 */
    { "xs:dateTime(//trip[@id = 't6']/arrive)", NULL },
    { "seconds-from-dateTime(xs:dateTime(//trip[@id = 't6']/arrive))",
      NULL },
    { "xs:dateTime(//trip[@id = 't6']/arrive)"
      " eq xs:dateTime(//trip[@id = 't6']/depart)", NULL },
    { "year-from-dateTime(xs:dateTime(//trip[@id = 't8']/arrive))", NULL },
    { "xs:date('2147483647-12-31') + xs:yearMonthDuration('P1Y')",
      "FODT0001" },
```

```text
xs:dateTime(//trip[@id = 't6']/arrive) -> xs:dateTime 2026-06-01T12:00:00.123456789Z
seconds-from-dateTime(xs:dateTime(//trip[@id = 't6']/arrive)) -> xs:decimal 0.123456789
xs:dateTime(//trip[@id = 't6']/arrive) eq xs:dateTime(//trip[@id = 't6']/depart) -> xs:boolean true
year-from-dateTime(xs:dateTime(//trip[@id = 't8']/arrive)) -> xs:integer 0
xs:date('2147483647-12-31') + xs:yearMonthDuration('P1Y') -> ERROR FODT0001
```

**Why:** implementations must keep at least milliseconds and may keep more ([F&O 3.1, 9.1.1](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#date-time-duration-conformance)). libxpath3 keeps nanoseconds and truncates further digits (`doc/DESIGN.md` 4.2), so t6's two times, which differ in the tenth digit, are equal, and t6 took no time at all. XML Schema 1.1 has a year 0, which is 1 BCE, so t8 takes two hours from 23:00 on 31 December of year -1; the specification leaves arithmetic across year 0 to the implementation. Years are limited to ±2,147,483,647, and going beyond is `FODT0001`, never a silent wrap-around (Saxon-HE 12.5 returns the year -3648 here).

## The clock stands still

```c
    /* The clock stands still during an evaluation */
    { "current-dateTime() eq current-dateTime()", NULL },
    { "current-date()", NULL }
};
```

```text
current-dateTime() eq current-dateTime() -> xs:boolean true
current-date() -> xs:date 2026-11-25+01:00
```

**Why:** the current dateTime is fixed for the whole evaluation ([F&O 3.1, 15.3](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-current-dateTime)), so two calls always agree, also when the system clock is used. `current-date()` has the implicit timezone too.

Next: [6. Extension functions](06-extension-functions.md).
