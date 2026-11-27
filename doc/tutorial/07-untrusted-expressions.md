# 7. Untrusted expressions

Sooner or later an application lets users type expressions: a search box, a rule editor, a configuration file. This page treats every expression as hostile, sends the kinds of input that break expression engines at the library catalogue of [tutorial 1](01-first-query.md), and checks after each one that the call came back with an error code, that nothing leaked, and that the same contexts still evaluate an ordinary query.

You will learn:

- which limits libxpath3 has built in, and which ones you have to set
- what each of them reports, and that a limit is an error like any other
- what the C API can't check for you
- how to restrict expressions to XPath 2.0

The full program is [`examples/07-untrusted-expressions.c`](../../examples/07-untrusted-expressions.c) and its output [`examples/expected/07-untrusted-expressions.txt`](../../examples/expected/07-untrusted-expressions.txt). The test runs it under a timeout, and the CI runs it under AddressSanitizer and LeakSanitizer, which is what "returned promptly" and "nothing leaked" rest on; the program itself prints only what is the same on every machine.

## The limits

```c
    /* Without these, an evaluation may run as long as it likes */
    xp3SetMaxSteps(dctx, MAX_STEPS);
    xp3SetMaxSequenceLength(dctx, MAX_ITEMS);
```

`MAX_STEPS` is 1,000,000 and `MAX_ITEMS` 100,000. A step is a unit of work: an expression evaluated, a node visited, a pair compared, 256 bytes of a string read or made, 256 operations of a regular expression (`doc/DESIGN.md` 7.3). The default is no step limit, like libxml2's XPath 1.0 engine, and 10 million items per sequence. Both belong to the dynamic context and apply per evaluation.

Every expression below goes through the same function, which also runs an ordinary query afterwards with the same two contexts:

```c
static int
untrusted(const xp3StaticContext *sctx, xp3DynamicContext *dctx,
          const char *label, const char *expression,
          const char *expectedError) {
    int failures = 0;

    if (expression == NULL)
        return(1);
    failures += exRun(sctx, dctx, label, expression, expectedError);
    failures += exRun(sctx, dctx, "  afterwards count(//book)",
                      "count(//book)", NULL);
    return(failures);
}
```

## Size and depth

```c
    /* Nesting is limited when parsing, before any stack is at risk */
    text = repeatAround("(", "1", ")", 100000);
    failures += untrusted(sctx, dctx, "100,000 nested parentheses", text,
                          "LIMIT");
    free(text);

    text = repeatAround("1+", "1", "", 99999);
    failures += untrusted(sctx, dctx, "1+1+...+1 with 100,000 terms", text,
                          "LIMIT");
    free(text);

    text = longLiteral(2 * 1024 * 1024);
    failures += untrusted(sctx, dctx, "string-length() of a 2 MB literal",
                          text, "LIMIT");
    free(text);
```

```text
Size and depth:
100,000 nested parentheses -> ERROR LIMIT when compiling
  afterwards count(//book) -> xs:integer 6
1+1+...+1 with 100,000 terms -> ERROR LIMIT when compiling
  afterwards count(//book) -> xs:integer 6
string-length() of a 2 MB literal -> ERROR LIMIT when compiling
  afterwards count(//book) -> xs:integer 6
```

**Why:** a recursive-descent parser recurses once per level of nesting, and so do the analysis and the evaluation that follow, so deep nesting is a stack overflow waiting to happen. libxpath3's parser counts the depth and stops with `LIMIT` long before that: about 200 levels of parentheses, or a chain of a few hundred binary operators, which builds a tree as deep as nesting does (`doc/DESIGN.md` 7.3). An expression longer than 1 MB is refused before it is parsed. Both limits are fixed, so they apply whether or not you set any; no realistic expression comes near them. `LIMIT` is not a W3C code: the specifications allow implementations to have limits without naming an error for them.

## Work and memory

```c
    /* A range is not stored item by item... */
    failures += untrusted(sctx, dctx, NULL, "count(1 to 1000000000000)",
                          NULL);
    /* ... but filtering it builds a sequence */
    failures += untrusted(sctx, dctx, NULL,
                          "(1 to 1000000000000)[. mod 7 = 0][last()]",
                          "LIMIT");
    failures += untrusted(sctx, dctx, NULL,
                          "count(for $a in 1 to 100000, $b in 1 to 100000 "
                          "return 1)", "LIMIT");

    text = doubling(60);
    failures += untrusted(sctx, dctx, "a string doubled 60 times", text,
                          "LIMIT");
    free(text);

    failures += untrusted(sctx, dctx, NULL,
                          "matches(string-join((1 to 30) ! 'a') || 'b', "
                          "'^(a+)+\\1$')", "LIMIT");
```

```text
Work and memory:
count(1 to 1000000000000) -> xs:integer 1000000000000
  afterwards count(//book) -> xs:integer 6
(1 to 1000000000000)[. mod 7 = 0][last()] -> ERROR LIMIT
  afterwards count(//book) -> xs:integer 6
count(for $a in 1 to 100000, $b in 1 to 100000 return 1) -> ERROR LIMIT
  afterwards count(//book) -> xs:integer 6
a string doubled 60 times -> ERROR LIMIT
  afterwards count(//book) -> xs:integer 6
matches(string-join((1 to 30) ! 'a') || 'b', '^(a+)+\1$') -> ERROR LIMIT
  afterwards count(//book) -> xs:integer 6
```

**Why:**

- A range of integers is stored as its two ends, so counting a trillion of them is instant; filtering them builds a real sequence, which reaches the limit of 100,000 items.
- Two nested `for` clauses of 100,000 each make 10¹⁰ items; the sequence limit stops them first.
- The doubled string is `let $s0 := 'ab', $s1 := concat($s0, $s0), …` sixty times, which would be 2⁶¹ bytes. String functions count a step per 256 bytes they make, so the step limit stops it after about 256 MB of string work in all. Without a step limit, it stops at libxpath3's maximum string length of 2 GB, after allocating more than a gigabyte.
- The back-reference pattern from [tutorial 4](04-strings-regex.md) backtracks exponentially, and matching counts steps.

Each of these is a single evaluation failing with `LIMIT`; the contexts are unaffected, and the next evaluation starts counting from zero.

## Bytes that are not text

```c
    /* xp3Compile() reads up to the NUL and never sees the rest */
    failures += untrusted(sctx, dctx, "1 + 1, a NUL and more", withNul,
                          NULL);
    if (acceptExpression(withNul, sizeof(withNul) - 1) < 0)
        printf("1 + 1, a NUL and more -> rejected before compiling\n");
    else
        failures++;

    /* Bytes that are not UTF-8 */
    failures += untrusted(sctx, dctx, "'\\xC3\\x28', invalid UTF-8",
                          "'\xC3\x28'", "XPST0003");
    failures += untrusted(sctx, dctx, "'\\xC0\\xAF', an overlong /",
                          "'\xC0\xAF'", "XPST0003");
```

```text
Bytes that are not text:
1 + 1, a NUL and more -> xs:integer 2
  afterwards count(//book) -> xs:integer 6
1 + 1, a NUL and more -> rejected before compiling
'\xC3\x28', invalid UTF-8 -> ERROR XPST0003 when compiling
  afterwards count(//book) -> xs:integer 6
'\xC0\xAF', an overlong / -> ERROR XPST0003 when compiling
  afterwards count(//book) -> xs:integer 6
```

`withNul` is `"1 + 1\0 + garbage"`. `xp3Compile()` takes a NUL-terminated string, so it compiles `1 + 1` and never learns that more was sent. If the expression arrives as bytes with a length, as from a network or a file, check for a NUL before compiling:

```c
static int
acceptExpression(const char *bytes, size_t length) {
    if (memchr(bytes, 0, length) != NULL)
        return(-1);
    return(0);
}
```

Bytes that are not UTF-8, including overlong forms that some decoders accept as `/`, are a syntax error: the lexer decodes strictly.

## Outside the process

```c
    failures += untrusted(sctx, dctx, NULL, "doc('file:///etc/passwd')",
                          "UNSUPPORTED");
    failures += untrusted(sctx, dctx, NULL,
                          "unparsed-text('file:///etc/passwd')",
                          "UNSUPPORTED");
    failures += untrusted(sctx, dctx, NULL, "environment-variable('HOME')",
                          "UNSUPPORTED");
```

```text
Outside the process:
doc('file:///etc/passwd') -> ERROR UNSUPPORTED
  afterwards count(//book) -> xs:integer 6
unparsed-text('file:///etc/passwd') -> ERROR UNSUPPORTED
  afterwards count(//book) -> xs:integer 6
environment-variable('HOME') -> ERROR UNSUPPORTED
  afterwards count(//book) -> xs:integer 6
```

These functions compile, because they exist in XPath 3.1, and fail when called. At the time of writing they are not implemented yet (`UNSUPPORTED`, `doc/DESIGN.md` 7.2); when they are, the design is that they reach nothing unless the application installs a resolver (`doc/DESIGN.md` 5). Don't rely on the code staying `UNSUPPORTED`; treat any error from these calls as a refusal.

## XPath 2.0 only

An application whose users write XPath 2.0, or whose expressions must also run on other XPath 2.0 engines, can compile at that level:

```c
    if ((sctx == NULL) ||
        (xp3SetStaticContextLanguageLevel(sctx, XP3_XPATH_20) < 0)) {
        xp3FreeStaticContext(sctx);
        return(1);
    }
    failures += untrusted(sctx, dctx, NULL, "//book ! title", "XPST0003");
    failures += untrusted(sctx, dctx, NULL, "'a' || 'b'", "XPST0003");
    failures += untrusted(sctx, dctx, NULL, "'abc' => string-length()",
                          "XPST0003");
    failures += untrusted(sctx, dctx, NULL, "head(//book)", "XPST0017");
    failures += untrusted(sctx, dctx, NULL, "environment-variable('HOME')",
                          "XPST0017");
```

```text
XPath 2.0 only:
//book ! title -> ERROR XPST0003 when compiling
  afterwards count(//book) -> xs:integer 6
'a' || 'b' -> ERROR XPST0003 when compiling
  afterwards count(//book) -> xs:integer 6
'abc' => string-length() -> ERROR XPST0003 when compiling
  afterwards count(//book) -> xs:integer 6
head(//book) -> ERROR XPST0017 when compiling
  afterwards count(//book) -> xs:integer 6
environment-variable('HOME') -> ERROR XPST0017 when compiling
  afterwards count(//book) -> xs:integer 6
```

**Why:** the simple map `!` and the string concatenation `||` came with XPath 3.0, the arrow `=>` with 3.1, so at XPath 2.0 they are syntax errors, and functions added in 3.0 or 3.1 are unknown, `XPST0017`. Everything is rejected by `xp3Compile()`, before evaluating. The level restricts what is accepted; the semantics of what is accepted are the same (`doc/DESIGN.md` 5.1).

## A checklist for accepting expressions

1. **Set a step limit** with `xp3SetMaxSteps()` on every dynamic context that evaluates user input. There is none by default. Pick it by measuring your largest legitimate expression on your largest document, and leave a margin.
2. **Set a sequence limit** with `xp3SetMaxSequenceLength()` lower than the default of 10 million items if your memory budget requires it.
3. **Check the bytes** before compiling: no NUL, and a length cap of your own if 1 MB is too much for you. libxpath3 rejects invalid UTF-8 itself.
4. **Compile and evaluate separately**, and report both kinds of error. A static error (`XPST…`) is the user's mistake; `LIMIT` means the expression asked too much; `NO_MEMORY` means the process ran out.
5. **Show the code, not only the message.** Codes are stable; messages may change between versions.
6. **Declare only what users may see**: the prefixes, the variables and the extension functions in the static context are the whole vocabulary an expression has. Don't register extension functions with side effects in a context that compiles user input.
7. **Restrict the language level** to `XP3_XPATH_20` if expressions must stay portable to 2.0 engines.
8. **Parse documents safely too**: the limits above protect the evaluation, not libxml2. Don't use `XML_PARSE_NOENT` on untrusted documents without `XML_PARSE_NO_XXE`, and keep `XML_PARSE_HUGE` off.
9. **Reuse the contexts**: an error never leaves a static or dynamic context in a broken state, as every "afterwards" line above shows.
