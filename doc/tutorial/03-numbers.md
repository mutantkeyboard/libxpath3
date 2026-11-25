# 3. Numbers

XPath has four numeric types where C has two, and the data your program reads was typed by people. This page runs exact and binary arithmetic side by side over an inventory whose numbers came from five different systems, then reads numeric results back into C.

You will learn:

- the difference between `xs:decimal` and `xs:double`, and how a literal or a cast picks one
- what `sum`, `avg`, division and rounding do with empty input, zero, NaN, negative zero and halves
- which strings each numeric type accepts
- how to read a result in C when it doesn't fit in `int64_t` or `double`

The full program is [`examples/03-numbers.c`](../../examples/03-numbers.c), its document [`examples/data/inventory.xml`](../../examples/data/inventory.xml), and its output [`examples/expected/03-numbers.txt`](../../examples/expected/03-numbers.txt).

## The data

```xml
<inventory>
  <item sku="A1" price=" 12.50 " qty="3"/>
  <item sku="A2" price="1e3" qty="1"/>
  <item sku="A3" price="NaN" qty="0"/>
  <item sku="A4" price="-0" qty="2"/>
  <item sku="A5" price="" qty="1"/>
  <item sku="A6" qty="5"/>
  <item sku="A7" price="0.000000000000000000000000000001" qty="1"/>
  <item sku="A8" price="+5"
        qty="1234567890123456789012345678901234567890"/>
  <item sku="A9" price=".5" qty="4"/>
  <item sku="A10" price="5." qty="4"/>
  <item sku="A11" price="1,250.00" qty="1"/>
</inventory>
```

Every price is awkward in its own way: spaces around it, exponent notation, `NaN`, negative zero, empty, missing, thirty decimal places, a plus sign, no digit before or after the point, and a thousands separator. One quantity has 40 digits.

## Decimals and doubles

The queries run with `exRun()` from a table, as in [tutorial 2](02-namespaces-variables.md):

```c
static const Query queries[] = {
    /* Literals with a point are decimals, with an exponent doubles */
    { "0.1 + 0.2", NULL },
    { "0.1e0 + 0.2e0", NULL },
    { "0.1 + 0.2 = 0.3", NULL },
    { "0.1e0 + 0.2e0 = 0.3e0", NULL },
```

```text
0.1 + 0.2 -> xs:decimal 0.3
0.1e0 + 0.2e0 -> xs:double 0.30000000000000004
0.1 + 0.2 = 0.3 -> xs:boolean true
0.1e0 + 0.2e0 = 0.3e0 -> xs:boolean false
```

**Why:** `0.1` is an `xs:decimal` literal and `0.1e0` an `xs:double` literal ([XPath 3.1, 3.1.1](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#id-literals)). Decimals are exact; libxpath3 gives them arbitrary precision (`doc/DESIGN.md` 4.3). Doubles are IEEE 754 binary numbers, where 0.1 has no exact value. An XPath 1.0 habit to drop: in 1.0 every number was a double.

## Summing what people typed

```c
    /* fn:sum casts untyped values to xs:double; "" is not one */
    { "sum(//item/@price)", "FORG0001" },
    { "sum(//item/@price[. castable as xs:decimal])", NULL },
    { "sum(//item/@price[. castable as xs:decimal] ! xs:decimal(.))", NULL },
    { "sum(//item/@price[. castable as xs:double])", NULL },
    { "sum(())", NULL },
    { "avg(())", NULL },
```

```text
sum(//item/@price) -> ERROR FORG0001
sum(//item/@price[. castable as xs:decimal]) -> xs:double 23
sum(//item/@price[. castable as xs:decimal] ! xs:decimal(.)) -> xs:decimal 23.000000000000000000000000000001
sum(//item/@price[. castable as xs:double]) -> xs:double NaN
sum(()) -> xs:integer 0
avg(()) -> ()
```

**Why:** `fn:sum` casts untyped values to `xs:double` ([F&O 3.1, 14.4.5](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-sum)), and the empty `price=""` of A5 is not a double, so the whole sum fails. Filtering with `castable as xs:decimal` keeps the right values, but they are still untyped, so the sum is still a double and the 0.000…001 of A7 disappears into 23. Casting each value with `! xs:decimal(.)` keeps the sum exact. Filtering with `castable as xs:double` lets `1e3` in, which you may want, and `NaN`, which turns the whole sum into NaN.

The sum of nothing is the integer 0, but the average of nothing is the empty sequence ([F&O 3.1, 14.4.2](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-avg)), because there is no sensible average. Code that divides by a count should expect it.

## Division

```c
    /* Integer division is decimal division (XPath 3.1, 3.5) */
    { "1 div 0", "FOAR0001" },
    { "1e0 div 0", NULL },
    { "-1e0 div 0", NULL },
    { "0e0 div 0", NULL },
    { "1 idiv 0", "FOAR0001" },
    { "1e0 idiv 0", "FOAR0001" },
    { "xs:double('INF') idiv 1", "FOAR0002" },
    { "1 div 3", NULL },
    { "1 div 3 * 3", NULL },
    { "-7 idiv 2", NULL },
    { "-7 mod 2", NULL },
```

```text
1 div 0 -> ERROR FOAR0001
1e0 div 0 -> xs:double INF
-1e0 div 0 -> xs:double -INF
0e0 div 0 -> xs:double NaN
1 idiv 0 -> ERROR FOAR0001
1e0 idiv 0 -> ERROR FOAR0001
xs:double('INF') idiv 1 -> ERROR FOAR0002
1 div 3 -> xs:decimal 0.333333333333333333
1 div 3 * 3 -> xs:decimal 0.999999999999999999
-7 idiv 2 -> xs:integer -3
-7 mod 2 -> xs:integer -1
```

**Why:** `div` on two integers is decimal division, so dividing by zero is an error, `FOAR0001`, while a double operand gives IEEE 754 division with its infinities and NaN ([F&O 3.1, 4.2.4](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-numeric-divide)). `idiv` always returns an integer, so it fails on a zero divisor whatever the type, and on an infinite dividend with `FOAR0002` ([4.2.5](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-numeric-integer-divide)). It truncates toward zero, and `mod` takes the sign of the dividend ([4.2.6](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-numeric-mod)), as in C.

`1 div 3` can't be exact. How many digits a decimal quotient has is implementation-defined; libxpath3 follows Saxon's rule, at least 18 digits after the point (`doc/DESIGN.md` 4.3), so `1 div 3 * 3` is not 1. Exact decimals are exact for `+`, `-` and `*` only.

## NaN

```c
    /* NaN equals nothing, itself included */
    { "xs:double('NaN') = xs:double('NaN')", NULL },
    { "xs:double('NaN') != xs:double('NaN')", NULL },
    { "//item[@price != @price]/string(@sku)", NULL },
    { "//item[number(@price) != number(@price)]/string(@sku)", NULL },
```

```text
xs:double('NaN') = xs:double('NaN') -> xs:boolean false
xs:double('NaN') != xs:double('NaN') -> xs:boolean true
//item[@price != @price]/string(@sku) -> ()
//item[number(@price) != number(@price)]/string(@sku) -> xs:string "A3", xs:string "A5", xs:string "A6", xs:string "A11"
```

**Why:** NaN is not equal to anything, itself included ([F&O 3.1, 4.3.1](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-numeric-equal)), which makes `x != x` a test for NaN. But `@price != @price` compares two untyped values, which are compared as strings, and the string "NaN" equals itself. `fn:number` turns anything it can't read into NaN ([F&O 3.1, 4.5.1](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-number)): the literal NaN of A3, the empty price of A5, the missing one of A6 and the thousands separator of A11. That makes it forgiving and lossy: it can't tell those four apart.

## Rounding

```c
    /* Halves round up, or to even; 35.425e0 is not exactly a half */
    { "round(2.5)", NULL },
    { "round(-2.5)", NULL },
    { "round(-0.5)", NULL },
    { "round(-0.5e0)", NULL },
    { "round-half-to-even(-2.5)", NULL },
    { "round-half-to-even(-3.5)", NULL },
    { "round(35.425, 2)", NULL },
    { "round(35.425e0, 2)", NULL },
    { "floor(-0.5)", NULL },
    { "ceiling(-0.5e0)", NULL },
```

```text
round(2.5) -> xs:decimal 3
round(-2.5) -> xs:decimal -2
round(-0.5) -> xs:decimal 0
round(-0.5e0) -> xs:double -0
round-half-to-even(-2.5) -> xs:decimal -2
round-half-to-even(-3.5) -> xs:decimal -4
round(35.425, 2) -> xs:decimal 35.43
round(35.425e0, 2) -> xs:double 35.42
floor(-0.5) -> xs:decimal -1
ceiling(-0.5e0) -> xs:double -0
```

**Why:** `fn:round` rounds halves toward positive infinity, so -2.5 becomes -2, not -3 as C's `round()` would make it ([F&O 3.1, 4.4.4](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-round)). `fn:round-half-to-even` rounds halves to the even neighbour ([4.4.5](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-round-half-to-even)). A double between -0.5 and 0 rounds to negative zero, which prints as `-0`; decimals have no negative zero.

`round(35.425e0, 2)` is the example of the specification's own note: the double written 35.425e0 is exactly 35.42499999999999715…, which is not a half, so it rounds down. Saxon-HE 12.5 gives 35.43 here; the specification gives 35.42, and so does libxpath3. Round decimals, not doubles, when the digits matter.

## Integers without a limit

```c
    /* Integers have no upper limit */
    { "9223372036854775807 + 1", NULL },
    { "-9223372036854775808 idiv -1", NULL },
    { "xs:long('9223372036854775807') + 1", NULL },
    { "xs:integer(//item[@sku = 'A8']/@qty) * 2", NULL },
    { "xs:double(//item[@sku = 'A8']/@qty)", NULL },
```

```text
9223372036854775807 + 1 -> xs:integer 9223372036854775808
-9223372036854775808 idiv -1 -> xs:integer 9223372036854775808
xs:long('9223372036854775807') + 1 -> xs:integer 9223372036854775808
xs:integer(//item[@sku = 'A8']/@qty) * 2 -> xs:integer 2469135780246913578024691357802469135780
xs:double(//item[@sku = 'A8']/@qty) -> xs:double 1.2345678901234568E39
```

**Why:** `xs:integer` has no upper bound in XML Schema, and libxpath3 doesn't impose one, so arithmetic past 64 bits stays exact: no wrap-around, and no `FOAR0002` where a 64-bit implementation would overflow. Arithmetic on `xs:long` gives an `xs:integer`, not an `xs:long` ([F&O 3.1, 4.2](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#op.numeric)), so it can't overflow the type either. The double of the 40-digit quantity keeps 17 significant digits.

## What each type accepts

```c
    /* Lexical forms: what each type accepts */
    { "xs:decimal('1e3')", "FORG0001" },
    { "xs:double('1e3')", NULL },
    { "xs:integer('1.0')", "FORG0001" },
    { "xs:integer(1.0)", NULL },
    { "xs:decimal(' 12.50 ')", NULL },
    { "xs:decimal('+5')", NULL },
    { "xs:decimal('.5')", NULL },
    { "xs:decimal('5.')", NULL },
    { "xs:decimal('1,250.00')", "FORG0001" },
    { "xs:decimal(translate('1,250.00', ',', ''))", NULL },
    { "xs:double('-0')", NULL },
    { "xs:decimal('-0')", NULL },
    { "xs:decimal(//item[@sku = 'A7']/@price) * 1000000", NULL },
    { "xs:double(//item[@sku = 'A7']/@price)", NULL },
    { "xs:double('1e309')", NULL },
    { "xs:integer(xs:double('INF'))", "FOCA0002" },
```

```text
xs:decimal('1e3') -> ERROR FORG0001
xs:double('1e3') -> xs:double 1000
xs:integer('1.0') -> ERROR FORG0001
xs:integer(1.0) -> xs:integer 1
xs:decimal(' 12.50 ') -> xs:decimal 12.5
xs:decimal('+5') -> xs:decimal 5
xs:decimal('.5') -> xs:decimal 0.5
xs:decimal('5.') -> xs:decimal 5
xs:decimal('1,250.00') -> ERROR FORG0001
xs:decimal(translate('1,250.00', ',', '')) -> xs:decimal 1250
xs:double('-0') -> xs:double -0
xs:decimal('-0') -> xs:decimal 0
xs:decimal(//item[@sku = 'A7']/@price) * 1000000 -> xs:decimal 0.000000000000000000000001
xs:double(//item[@sku = 'A7']/@price) -> xs:double 1.0E-30
xs:double('1e309') -> xs:double INF
xs:integer(xs:double('INF')) -> ERROR FOCA0002
```

**Why:** casting a string uses the lexical forms of XML Schema, after collapsing whitespace ([F&O 3.1, 19.2](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#casting-from-strings)). A decimal has no exponent; an integer has no point, so the string `'1.0'` fails while the decimal `1.0` casts, because casting a number truncates ([19.1.2.4](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#casting-to-integer)). A sign, a missing digit on either side of the point and spaces around the number are fine; a thousands separator is not, and has to be removed first. A double too large for the type reads as infinity, and infinity has no integer value (`FOCA0002`).

## Doubles as strings and as decimals

```c
    /* A double as a string, and as a decimal (F&O 3.1, 19.1.2.3) */
    { "string(1e6)", NULL },
    { "string(1e-6)", NULL },
    { "xs:decimal(0.1e0)", NULL },
    { "round(xs:decimal(35.425e0), 2)", NULL },
```

```text
string(1e6) -> xs:string "1.0E6"
string(1e-6) -> xs:string "0.000001"
xs:decimal(0.1e0) -> xs:decimal 0.1000000000000000055511151231257827021181583404541015625
round(xs:decimal(35.425e0), 2) -> xs:decimal 35.42
```

**Why:** a double is written without an exponent only from 10⁻⁶ up to, but not including, 10⁶ ([F&O 3.1, 19.1.1](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#casting-to-string)), so a million comes out as `1.0E6`. Format numbers for people with decimals or `fn:round`, not with `string()` of a double.

Casting a double to `xs:decimal` gives "the `xs:decimal` value, within the set of `xs:decimal` values that the implementation is capable of representing, that is numerically closest" to the double ([F&O 3.1, 19.1.2.3](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#casting-to-decimal)). libxpath3's decimals can hold the double's exact binary value, so that is the closest one, and it is what Saxon-HE 12.5 returns. The expected output above follows the specification. **libxpath3 currently returns `0.1` and `35.43` for these two queries**: `doc/DESIGN.md` 4.2 chooses the shortest decimal that reads back as the double. Until that is settled, the test of this page fails on these two lines. The consequence to remember either way: `round(35.425e0, 2)` and `round(xs:decimal(35.425e0), 2)` should agree, and a decimal made from a double inherits the double's binary value.

## Comparing an integer with a double

```c
    /* The integer becomes a double before comparing (XPath 3.1, B.1) */
    { "9007199254740993 = 9007199254740992e0", NULL },
    { "9007199254740993 = 9007199254740992", NULL }
};
```

```text
9007199254740993 = 9007199254740992e0 -> xs:boolean true
9007199254740993 = 9007199254740992 -> xs:boolean false
```

**Why:** when an integer meets a double, the integer is promoted to a double first ([XPath 3.1, B.1](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#promotion)), and 2⁵³ + 1 has no double of its own, so it becomes 2⁵³ and the two compare equal. Two integers compare exactly. A comparison is only as exact as its least exact operand.

## Reading numbers in C

The API has three ways to read an atomic value: `xp3SequenceInteger()`, `xp3SequenceDouble()` and `xp3SequenceStringValue()`. The program tries all three on a few results:

```c
    /* 0: it fits, 1: an integer outside int64_t, -1: not an integer */
    ret = xp3SequenceInteger(result, 0, &integer);
    if (ret == 0)
        printf("  xp3SequenceInteger -> %lld\n", (long long) integer);
    else if (ret == 1)
        printf("  xp3SequenceInteger -> 1, outside int64_t\n");
    else
        printf("  xp3SequenceInteger -> -1, not an integer\n");

    /* Any numeric type, rounded to the nearest double */
    if (xp3SequenceDouble(result, 0, &number) < 0)
        printf("  xp3SequenceDouble -> -1, not a number\n");
    else if (isnan(number))
        printf("  xp3SequenceDouble -> NaN\n");
    else
        printf("  xp3SequenceDouble -> %.17g\n", number);

    /* The exact value, for every type */
    text = xp3SequenceStringValue(result, 0);
```

```text
Reading results in C:
count(//item)
  xp3SequenceInteger -> 11
  xp3SequenceDouble -> 11
  xp3SequenceStringValue -> "11"
xs:integer(//item[@sku = 'A8']/@qty)
  xp3SequenceInteger -> 1, outside int64_t
  xp3SequenceDouble -> 1.2345678901234568e+39
  xp3SequenceStringValue -> "1234567890123456789012345678901234567890"
sum(//item/@price[. castable as xs:decimal] ! xs:decimal(.))
  xp3SequenceInteger -> -1, not an integer
  xp3SequenceDouble -> 23
  xp3SequenceStringValue -> "23.000000000000000000000000000001"
data(//item[@sku = 'A1']/@price)
  xp3SequenceInteger -> -1, not an integer
  xp3SequenceDouble -> -1, not a number
  xp3SequenceStringValue -> " 12.50 "
number(//item[@sku = 'A1']/@price)
  xp3SequenceInteger -> -1, not an integer
  xp3SequenceDouble -> 12.5
  xp3SequenceStringValue -> "12.5"
number(//item[@sku = 'A6']/@price)
  xp3SequenceInteger -> -1, not an integer
  xp3SequenceDouble -> NaN
  xp3SequenceStringValue -> "NaN"
xs:double('-0')
  xp3SequenceInteger -> -1, not an integer
  xp3SequenceDouble -> -0
  xp3SequenceStringValue -> "-0"
```

What to take from it:

- `xp3SequenceInteger()` returns **1**, not -1, for an integer that doesn't fit, and leaves the variable unchanged. Check for 1 separately, or a 40-digit quantity becomes whatever the variable held before.
- `xp3SequenceDouble()` accepts every numeric type and rounds **silently**: the 40-digit integer and the exact decimal sum lose digits without any error. It refuses anything that isn't numeric, including an untyped attribute value, so `data(@price)` gives -1 while `number(@price)` works. Wrap values in `number()` or a cast in the expression when C wants a double.
- `xp3SequenceStringValue()` is exact for every type, because decimals and integers are written without exponent. It is the way to hand an `xs:decimal` to a decimal library of your own.
- NaN and negative zero survive the trip into C, so test with `isnan()` and `signbit()`; `printf()` writes NaN differently on different platforms, which is why the program prints it itself.

Next: [4. Strings and regular expressions](04-strings-regex.md).
