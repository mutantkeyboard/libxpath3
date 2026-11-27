# 4. Strings and regular expressions

Strings in XPath are sequences of Unicode code points, compared code point by code point unless a collation says otherwise, and matched with a regular expression dialect of their own. This page runs them over an address book merged from several sources, where names come in two Unicode forms, from four scripts and with whitespace you can't see.

You will learn:

- why two names that look the same compare unequal, and what `normalize-unicode` does about it
- what `string-length` and `substring` count
- what upper and lower case mean without a locale
- which whitespace `normalize-space` removes and which it keeps
- the regular expression dialect: flags, groups, back-references, reluctant quantifiers, and the errors for patterns that match the empty string
- that a pattern which takes exponential time elsewhere is linear here, and how the step limit stops the one that isn't

The full program is [`examples/04-strings-regex.c`](../../examples/04-strings-regex.c), its document [`examples/data/contacts.xml`](../../examples/data/contacts.xml), and its output [`examples/expected/04-strings-regex.txt`](../../examples/expected/04-strings-regex.txt).

## The data

Invisible or easily confused characters are written as character references, so you can see them:

```xml
  <contact id="c1">
    <name>José Núñez</name>
    <phone>+34 912 345 678</phone>
    <email>jose@example.com</email>
  </contact>
  <contact id="c2">
    <name>Jose&#x301; Nu&#x301;n&#x303;ez</name>
    <phone>(030) 1234-5678</phone>
    <email>"office@home"@example.org</email>
  </contact>
  <contact id="c3">
    <name>&#x20BB7;野 Kenji</name>
    <phone>030/12345678</phone>
    <email/>
  </contact>
  <contact id="c4">
    <name>Işıl Yıldız</name>
    <city>İstanbul</city>
    <phone>+90-212-555-0143</phone>
  </contact>
  <contact id="c5">
    <name>Jürgen Groß</name>
    <city>Straße des 17. Juni</city>
    <phone>0049 30 1234 5678</phone>
  </contact>
  <contact id="c6">
    <name>  Ann&#xA0;Marie&#9; Lee
    </name>
    <phone>+44 (0)20 7946 0958 ext. 12</phone>
  </contact>
  <contact id="c7">
    <name>שרה כהן</name>
    <phone>٠٣٠ ١٢٣٤٥٦٧٨</phone>
    <note>Uses C++ (v2.0)? [beta] *only* $5+ plan</note>
  </contact>
```

What to look out for:

- **c1** and **c2** are the same name: c1 with precomposed letters (é is U+00E9), c2 with a letter followed by a combining accent (e followed by U+0301).
- **c3** starts with U+20BB7, a character outside the Basic Multilingual Plane: four bytes in UTF-8, two units in UTF-16.
- **c4** has the Turkish dotless ı and the capital İ with a dot.
- **c5** has the German ß.
- **c6** has leading, trailing and inner whitespace, among it a no-break space (U+00A0), a tab and a newline.
- **c7** is written right to left, has a phone number in Arabic-Indic digits, and a note full of regular expression metacharacters.
- The phone numbers come in six formats; c2's e-mail has a quoted local part containing `@`; c3's `email` is empty and c4 has none.

## The same name, twice

```c
static const Query queries[] = {
    /* The same name, precomposed and with combining accents */
    { "//contact[@id = 'c1']/name = //contact[@id = 'c2']/name", NULL },
    { "normalize-unicode(//contact[@id = 'c1']/name) = "
      "normalize-unicode(//contact[@id = 'c2']/name)", NULL },
    { "//contact[@id = ('c1', 'c2')]/string-length(name)", NULL },
    { "string-to-codepoints(//contact[@id = 'c2']/name)", NULL },
    { "starts-with(//contact[@id = 'c2']/name, 'José')", NULL },
```

```text
//contact[@id = 'c1']/name = //contact[@id = 'c2']/name -> xs:boolean false
normalize-unicode(//contact[@id = 'c1']/name) = normalize-unicode(//contact[@id = 'c2']/name) -> xs:boolean true
//contact[@id = ('c1', 'c2')]/string-length(name) -> xs:integer 10, xs:integer 13
string-to-codepoints(//contact[@id = 'c2']/name) -> xs:integer 74, xs:integer 111, xs:integer 115, xs:integer 101, xs:integer 769, xs:integer 32, xs:integer 78, xs:integer 117, xs:integer 769, xs:integer 110, xs:integer 771, xs:integer 101, xs:integer 122
starts-with(//contact[@id = 'c2']/name, 'José') -> xs:boolean false
```

**Why:** strings compare with the Unicode codepoint collation by default ([F&O 3.1, 5.3.2](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#codepoint-collation)), which compares code points and knows nothing of canonical equivalence, so the two names differ, and so do their lengths: 10 code points against 13. `normalize-unicode` puts both into NFC by default ([5.4.6](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-normalize-unicode)), after which they are equal. Normalise both sides whenever text comes from more than one source; neither XML parsing nor XPath does it for you.

## Counting characters

```c
    /* Lengths and positions count characters, not bytes */
    { "string-length(//contact[@id = 'c3']/name)", NULL },
    { "substring(//contact[@id = 'c3']/name, 1, 1)", NULL },
    { "string-to-codepoints(substring(//contact[@id = 'c3']/name, 1, 2))",
      NULL },
    { "string-length(//contact[@id = 'c7']/name)", NULL },
    { "substring(//contact[@id = 'c7']/name, 1, 3)", NULL },
```

```text
string-length(//contact[@id = 'c3']/name) -> xs:integer 8
substring(//contact[@id = 'c3']/name, 1, 1) -> xs:string "𠮷"
string-to-codepoints(substring(//contact[@id = 'c3']/name, 1, 2)) -> xs:integer 134071, xs:integer 37326
string-length(//contact[@id = 'c7']/name) -> xs:integer 7
substring(//contact[@id = 'c7']/name, 1, 3) -> xs:string "שרה"
```

**Why:** `string-length` counts characters, that is code points ([F&O 3.1, 5.4.4](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-string-length)), and `substring` counts positions the same way ([5.4.3](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-substring)). "𠮷野 Kenji" is 8 characters, 13 bytes in UTF-8 and 9 units in UTF-16. C code that counts bytes, or code that counts UTF-16 units as Java and JavaScript strings do, gets a different length; XPath counts code points. A right-to-left name is stored in logical order, the order it is read, so its first three characters are the first word, whatever a terminal shows.

## Case without a locale

```c
    /* Case mappings of Unicode, the same in every locale */
    { "upper-case(//contact[@id = 'c5']/city)", NULL },
    { "upper-case('ß') = 'SS'", NULL },
    { "upper-case(//contact[@id = 'c4']/name)", NULL },
    { "lower-case(//contact[@id = 'c4']/city)", NULL },
    { "string-length(lower-case(//contact[@id = 'c4']/city))", NULL },
    { "matches('STRASSE', 'straße', 'i')", NULL },
    { "matches('ıstanbul', 'ISTANBUL', 'i')", NULL },
    { "matches(//contact[@id = 'c4']/city, '^istanbul$', 'i')", NULL },
    { "matches(//contact[@id = 'c5']/name, 'GROẞ', 'i')", NULL },
    { "translate(//contact[@id = 'c5']/city, 'ß', 'ss')", NULL },
```

```text
upper-case(//contact[@id = 'c5']/city) -> xs:string "STRASSE DES 17. JUNI"
upper-case('ß') = 'SS' -> xs:boolean true
upper-case(//contact[@id = 'c4']/name) -> xs:string "IŞIL YILDIZ"
lower-case(//contact[@id = 'c4']/city) -> xs:string "i̇stanbul"
string-length(lower-case(//contact[@id = 'c4']/city)) -> xs:integer 9
matches('STRASSE', 'straße', 'i') -> xs:boolean false
matches('ıstanbul', 'ISTANBUL', 'i') -> xs:boolean true
matches(//contact[@id = 'c4']/city, '^istanbul$', 'i') -> xs:boolean false
matches(//contact[@id = 'c5']/name, 'GROẞ', 'i') -> xs:boolean true
translate(//contact[@id = 'c5']/city, 'ß', 'ss') -> xs:string "Strase des 17. Juni"
```

**Why:** `upper-case` and `lower-case` apply Unicode's full case mappings without tailoring for any language ([F&O 3.1, 5.4.7](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-upper-case) and [5.4.8](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-lower-case)). So ß becomes SS, two characters, the dotless ı becomes I, and İ becomes i followed by a combining dot above (U+0307): "İstanbul" has 8 characters, its lower case 9. A Turkish locale would map İ to i and I to ı; XPath never does, on any machine.

The `i` flag of regular expressions is not "compare the lower-case forms". A character in the pattern matches its *case-variants*: the characters with the same `lower-case` or the same `upper-case` as a single character ([F&O 3.1, 5.6.2](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#flags)). ß and SS are not one character each, so `straße` doesn't match `STRASSE`. ı and i have the same upper case, I, so they match. İ and i have neither the same lower case ("i̇" against "i") nor the same upper case (İ against I), so `^istanbul$` doesn't match "İstanbul". The capital ẞ (U+1E9E) and ß have the same lower case, so `GROẞ` matches "Groß".

Saxon-HE 12.5 answers the last two the other way round (true for İstanbul, false for ẞ); the definition in 5.6.2 decides, and libxpath3 follows it. `translate` maps one character to one character, so `'ss'` only contributes its first `s`.

## Whitespace

```c
    /* normalize-space() knows four whitespace characters only */
    { "normalize-space(//contact[@id = 'c6']/name)", NULL },
    { "string-to-codepoints(normalize-space(//contact[@id = 'c6']/name))[4]",
      NULL },
    { "normalize-space(replace(//contact[@id = 'c6']/name, '\\p{Zs}', ' '))",
      NULL },
```

```text
normalize-space(//contact[@id = 'c6']/name) -> xs:string "Ann Marie Lee"
string-to-codepoints(normalize-space(//contact[@id = 'c6']/name))[4] -> xs:integer 160
normalize-space(replace(//contact[@id = 'c6']/name, '\p{Zs}', ' ')) -> xs:string "Ann Marie Lee"
```

The first two lines look the same, and aren't: the space after "Ann" in the first result is still U+00A0.

**Why:** `normalize-space` collapses the whitespace of XML, which is space, tab, carriage return and line feed ([F&O 3.1, 5.4.5](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-normalize-space)). The tab and the newline go; the no-break space stays, because it is not whitespace in XML. Replacing the Unicode space separators, `\p{Zs}`, first catches it. The same holds for `\s` in a regular expression, which is the same four characters.

## Patterns that match nothing, and broken patterns

```c
    /* Patterns that match the empty string, invalid patterns and flags */
    { "tokenize('a,b,,c', ',*')", "FORX0003" },
    { "tokenize('a,b,,c', ',+')", NULL },
    { "tokenize('a,b,,c', ',')", NULL },
    { "replace('abc', 'x*', '-')", "FORX0003" },
    { "matches('abc', '(')", "FORX0002" },
    { "matches('abc', 'a', 'g')", "FORX0001" },
```

```text
tokenize('a,b,,c', ',*') -> ERROR FORX0003
tokenize('a,b,,c', ',+') -> xs:string "a", xs:string "b", xs:string "c"
tokenize('a,b,,c', ',') -> xs:string "a", xs:string "b", xs:string "", xs:string "c"
replace('abc', 'x*', '-') -> ERROR FORX0003
matches('abc', '(') -> ERROR FORX0002
matches('abc', 'a', 'g') -> ERROR FORX0001
```

**Why:** in XPath 3.1, `tokenize` and `replace` reject a pattern that matches the empty string, `FORX0003` ([F&O 3.1, 5.6.5](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-tokenize) and [5.6.4](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-replace)), even when the input happens to have no such match. Use `+` instead of `*` for separators, and decide whether `,,` means an empty field (`','`) or none (`',+'`). An invalid pattern is `FORX0002`, and a flag other than `s`, `m`, `i`, `x` and `q` is `FORX0001` ([5.6.2](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#flags)); there is no `g`, since `replace` always replaces every match. These are dynamic errors, because a pattern can come from a variable, so they are raised when evaluating.

## Groups, back-references and reluctant quantifiers

```c
    /* Groups, back-references and reluctant quantifiers */
    { "matches('abab', '^(ab)\\1$')", NULL },
    { "replace('2026-11-25', '(\\d+)-(\\d+)-(\\d+)', '$3.$2.$1')", NULL },
    { "replace('<b>x</b> and <b>y</b>', '<b>(.*)</b>', '[$1]')", NULL },
    { "replace('<b>x</b> and <b>y</b>', '<b>(.*?)</b>', '[$1]')", NULL },
```

```text
matches('abab', '^(ab)\1$') -> xs:boolean true
replace('2026-11-25', '(\d+)-(\d+)-(\d+)', '$3.$2.$1') -> xs:string "25.11.2026"
replace('<b>x</b> and <b>y</b>', '<b>(.*)</b>', '[$1]') -> xs:string "[x</b> and <b>y]"
replace('<b>x</b> and <b>y</b>', '<b>(.*?)</b>', '[$1]') -> xs:string "[x] and [y]"
```

In C source the backslash of `\1` and `\d` is doubled; the expression itself has one. `$1` to `$9` in the replacement refer to groups ([F&O 3.1, 5.6.4](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-replace)). `.*` takes as much as it can, `.*?` as little.

## The flags

```c
    /* The flags x, s, m and q */
    { "matches('2026-11-25', '^ \\d{4} - \\d{2} - \\d{2} $', 'x')", NULL },
    { "matches(//contact[@id = 'c6']/name, 'Lee.+')", NULL },
    { "matches(//contact[@id = 'c6']/name, 'Lee.+', 's')", NULL },
    { "matches(//contact[@id = 'c6']/name, '^\\s*$')", NULL },
    { "matches(//contact[@id = 'c6']/name, '^\\s*$', 'm')", NULL },
    { "matches(//contact[@id = 'c7']/note, 'C++ (v2.0)?')", "FORX0002" },
    { "matches(//contact[@id = 'c7']/note, 'C++ (v2.0)?', 'q')", NULL },
    { "replace(//contact[@id = 'c7']/note, '$5+', 'five', 'q')", NULL },
    { "replace('price: 5', '\\d', '$')", "FORX0004" },
    { "replace('price: 5', '\\d', '\\$')", NULL },
```

```text
matches('2026-11-25', '^ \d{4} - \d{2} - \d{2} $', 'x') -> xs:boolean true
matches(//contact[@id = 'c6']/name, 'Lee.+') -> xs:boolean false
matches(//contact[@id = 'c6']/name, 'Lee.+', 's') -> xs:boolean true
matches(//contact[@id = 'c6']/name, '^\s*$') -> xs:boolean false
matches(//contact[@id = 'c6']/name, '^\s*$', 'm') -> xs:boolean true
matches(//contact[@id = 'c7']/note, 'C++ (v2.0)?') -> ERROR FORX0002
matches(//contact[@id = 'c7']/note, 'C++ (v2.0)?', 'q') -> xs:boolean true
replace(//contact[@id = 'c7']/note, '$5+', 'five', 'q') -> xs:string "Uses C++ (v2.0)? [beta] *only* five plan"
replace('price: 5', '\d', '$') -> ERROR FORX0004
replace('price: 5', '\d', '\$') -> xs:string "price: $"
```

**Why** ([F&O 3.1, 5.6.2](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#flags)):

- `x` removes whitespace from the pattern, so it can be laid out for reading.
- Without `s`, `.` matches anything but a newline, and the name of c6 continues with a newline after "Lee". With `s` it matches newlines too.
- Without `m`, `^` and `$` match at the start and end of the whole string; with `m`, also at each line. The name of c6 ends in a line with only spaces.
- `q` takes the pattern literally, metacharacters and all, and the replacement too. Without it, `C++` is a quantifier applied to a quantifier, which is not a valid pattern. Use `q` whenever the pattern comes from data.
- In a replacement, `$` must be followed by a digit and a literal dollar is written `\$` ([5.6.4](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-replace)), `FORX0004` otherwise.

## Digits that are not 0 to 9

```c
    /* \d is any decimal digit of Unicode, not only 0 to 9 */
    { "//phone ! replace(., '[^0-9+]', '')", NULL },
    { "matches(//contact[@id = 'c7']/phone, '^[\\d ]+$')", NULL },
    { "matches(//contact[@id = 'c7']/phone, '^[0-9 ]+$')", NULL },
    { "xs:integer(substring-before(//contact[@id = 'c7']/phone, ' '))",
      "FORG0001" },
```

```text
//phone ! replace(., '[^0-9+]', '') -> xs:string "+34912345678", xs:string "03012345678", xs:string "03012345678", xs:string "+902125550143", xs:string "00493012345678", xs:string "+440207946095812", xs:string ""
matches(//contact[@id = 'c7']/phone, '^[\d ]+$') -> xs:boolean true
matches(//contact[@id = 'c7']/phone, '^[0-9 ]+$') -> xs:boolean false
xs:integer(substring-before(//contact[@id = 'c7']/phone, ' ')) -> ERROR FORG0001
```

**Why:** `\d` is every character of the Unicode category Nd, decimal digits of every script ([F&O 3.1, 5.6.1](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#regex-syntax), from XML Schema), so it accepts the Arabic-Indic digits of c7, which `xs:integer` then refuses. Validate with `[0-9]` when the value must be a number. Stripping everything but digits normalises the first five numbers, gives c6 a wrong one because "(0)" and the extension are digits too, and leaves c7 empty: tidying phone numbers with one regular expression doesn't work.

## A quoted local part

```c
    /* A quoted local part may contain an @ */
    { "substring-before(//contact[@id = 'c2']/email, '@')", NULL },
    { "replace(//contact[@id = 'c2']/email, '^(.*)@[^@]*$', '$1')", NULL },
```

```text
substring-before(//contact[@id = 'c2']/email, '@') -> xs:string ""office"
replace(//contact[@id = 'c2']/email, '^(.*)@[^@]*$', '$1') -> xs:string ""office@home""
```

`substring-before` stops at the first `@`, which is inside the quotes. The domain is after the *last* `@`, which a greedy group finds.

## Ordering without a collation

```c
    /* The codepoint collation orders by code point */
    { "compare('Zebra', 'apple')", NULL },
    { "compare('zoo', 'ábaco')", NULL },
    { "min(('b', 'B', 'á', 'a', 'Z'))", NULL },
    { "max(('b', 'B', 'á', 'a', 'Z'))", NULL },
```

```text
compare('Zebra', 'apple') -> xs:integer -1
compare('zoo', 'ábaco') -> xs:integer -1
min(('b', 'B', 'á', 'a', 'Z')) -> xs:string "B"
max(('b', 'B', 'á', 'a', 'Z')) -> xs:string "á"
```

**Why:** under the codepoint collation ([F&O 3.1, 5.3.2](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#codepoint-collation)) every upper-case ASCII letter comes before every lower-case one, and á (U+00E1) after z. libxpath3 has no language-aware collations; the UCA collation URIs fall back to the codepoint collation (`doc/DESIGN.md` 5). Sort names for people in the application, not in XPath.

## Empty strings

```c
    /* Every string contains the empty string */
    { "contains('abc', '')", NULL },
    { "contains((), '')", NULL },
    { "substring-before('abc', '')", NULL },
    /* An empty element and a missing one */
    { "string-length(//contact[@id = 'c3']/email)", NULL },
    { "matches(//contact[@id = 'c4']/email, '^$')", NULL },
    { "tokenize(//contact[@id = 'c4']/email, ',')", NULL }
};
```

```text
contains('abc', '') -> xs:boolean true
contains((), '') -> xs:boolean true
substring-before('abc', '') -> xs:string ""
string-length(//contact[@id = 'c3']/email) -> xs:integer 0
matches(//contact[@id = 'c4']/email, '^$') -> xs:boolean true
tokenize(//contact[@id = 'c4']/email, ',') -> ()
```

**Why:** the string functions treat the empty sequence as the empty string, and every string contains the empty string ([F&O 3.1, 5.5.1](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-contains)), so `contains($field, $search)` is true for an empty search term. A missing `email` matches `^$` like an empty one; `tokenize` of nothing is nothing.

## Patterns that backtrack

```c
    printf("With at most 1,000,000 steps:\n");
    xp3SetMaxSteps(dctx, 1000000);
    failures += exRun(sctx, dctx, NULL,
                      "matches(string-join((1 to 100000) ! 'a') || 'b', "
                      "'(a+)+$')", NULL);
    failures += exRun(sctx, dctx, NULL,
                      "matches(string-join((1 to 30) ! 'a') || 'b', "
                      "'^(a+)+\\1$')", "LIMIT");

    /* The contexts work as before; 0 removes the limit again */
    xp3SetMaxSteps(dctx, 0);
    failures += exRun(sctx, dctx, NULL, "count(//contact)", NULL);
```

```text
With at most 1,000,000 steps:
matches(string-join((1 to 100000) ! 'a') || 'b', '(a+)+$') -> xs:boolean false
matches(string-join((1 to 30) ! 'a') || 'b', '^(a+)+\1$') -> ERROR LIMIT
count(//contact) -> xs:integer 7
```

`(a+)+$` on a long run of `a` followed by `b` is the classic pattern that takes exponential time in a backtracking matcher. libxpath3 runs patterns without back-references on an automaton whose time is linear in the length of the subject (`doc/DESIGN.md` 6.4), so it answers at once, and well within the limit. (Saxon-HE 12.5 stops this query at its own backtracking limit and raises an error.)

A back-reference can't be matched by an automaton, so `^(a+)+\1$` runs on a backtracking matcher and does take exponential time: without a limit, 30 `a` take tens of seconds, and each further `a` doubles that. Matching counts towards the step limit of the evaluation like any other work, so with `xp3SetMaxSteps()` set it stops with `LIMIT` after a million steps, a fraction of a second. The default is no limit at all; an application that evaluates patterns from users sets one, see [tutorial 7](07-untrusted-expressions.md). Setting 0 removes it again.

Next: [5. Dates, times and durations](05-dates-times.md).
