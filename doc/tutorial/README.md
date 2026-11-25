# Tutorials

Seven tutorials for C programmers who know libxml2 and XPath 1.0 and want to embed libxpath3. Each page walks through a complete program and a document written to be awkward: mixed content, namespaces bound twice, numbers in five notations, names in four scripts, dates in every timezone. The point is what really happens on such input, what the specifications say about it, and how to handle it in C.

| | Page | Program | Document |
|---|---|---|---|
| 1 | [First query](01-first-query.md): parse, compile, evaluate, walk the result, report errors | [`01-first-query.c`](../../examples/01-first-query.c) | [`library.xml`](../../examples/data/library.xml) |
| 2 | [Variables, namespaces, reuse](02-namespaces-variables.md): prefixes, default namespaces, variables, one expression on many nodes | [`02-namespaces-variables.c`](../../examples/02-namespaces-variables.c) | [`feed.xml`](../../examples/data/feed.xml) |
| 3 | [Numbers](03-numbers.md): decimals and doubles, sums, division, rounding, reading numbers in C | [`03-numbers.c`](../../examples/03-numbers.c) | [`inventory.xml`](../../examples/data/inventory.xml) |
| 4 | [Strings and regular expressions](04-strings-regex.md): Unicode, case, whitespace, patterns and flags | [`04-strings-regex.c`](../../examples/04-strings-regex.c) | [`contacts.xml`](../../examples/data/contacts.xml) |
| 5 | [Dates, times and durations](05-dates-times.md): timezones, month arithmetic, a fixed clock | [`05-dates-times.c`](../../examples/05-dates-times.c) | [`timetable.xml`](../../examples/data/timetable.xml) |
| 6 | [Extension functions](06-extension-functions.md): C functions with signatures, errors, folding, user data | [`06-extension-functions.c`](../../examples/06-extension-functions.c) | [`inventory.xml`](../../examples/data/inventory.xml) |
| 7 | [Untrusted expressions](07-untrusted-expressions.md): limits, error codes, XPath 2.0 only, a checklist | [`07-untrusted-expressions.c`](../../examples/07-untrusted-expressions.c) | [`library.xml`](../../examples/data/library.xml) |

Read them in order: each page assumes the API of the ones before it. From tutorial 2 on, the programs share a few helpers for reading documents and printing results, in [`examples/common.c`](../../examples/common.c).

## Running the programs

The programs are built with the library unless the build is configured with `-Dexamples=disabled` (Meson) or `-DXP3_BUILD_EXAMPLES=OFF` (CMake). Each takes the directory of the documents as its only argument:

```sh
meson setup build
meson compile -C build
build/examples/01-first-query examples/data
```

## How they are tested

Every program is a test. `meson test --suite examples` (or `ctest -R example`) runs each one with `examples/data` and compares its output byte for byte with [`examples/expected/`](../../examples/expected/), and fails if the program exits with a non-zero status, which it does when a query does not behave as its page says. A second test per page checks that every C code block on the page is a contiguous excerpt of its program or of `common.c`, so the pages can't drift from the code. Both are done by [`examples/runexample.c`](../../examples/runexample.c), a small C program, so the tests need nothing but a C compiler on any platform.

The expected output is not a copy of what libxpath3 printed. Each result was derived from the specifications, XPath 3.1 and XPath and XQuery Functions and Operators 3.1, and checked against Saxon-HE 12.5 on the same documents; where Saxon and the specifications disagree, the specifications decide, and the pages say so. Results that are libxpath3's own, such as its limits, its messages and the behaviour of the C API, are marked as such on the pages.
