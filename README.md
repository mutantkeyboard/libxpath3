# libxpath3

[![pipeline](https://gitlab.gnome.org/mutantkeyboard/libxpath3/badges/main/pipeline.svg)](https://gitlab.gnome.org/mutantkeyboard/libxpath3/-/pipelines?ref=main)
[![coverage](https://gitlab.gnome.org/mutantkeyboard/libxpath3/badges/main/coverage.svg)](https://gitlab.gnome.org/mutantkeyboard/libxpath3/-/pipelines?ref=main)

libxpath3 is an XPath 3.1 engine in C for documents parsed with [libxml2](https://gitlab.gnome.org/GNOME/libxml2). It evaluates expressions directly on libxml2's tree, so existing libxml2 code can use it without converting documents.

> **Early release.** libxpath3 0.1 evaluates paths and operators, but no function calls yet. The API may still change before 1.0.

## Features and scope

- **XPath 3.1 on libxml2's tree**, using only libxml2's public API.
- **XPath 2.0 and 3.0 too.** XPath 3.1 is backwards compatible, so 2.0 and 3.0 expressions work with the same API. An option restricts the language to a specific version.
- **Exact arithmetic.** `xs:decimal` and `xs:integer` have arbitrary precision.
- **Safe for untrusted input.** Limits on recursion, operation count and sizes, and no file or network access unless the application allows it.
- **A foundation for XSLT 2.0 and 3.0** in libxslt, in coordination with the libxslt maintainer.
- **Close to libxml2.** MIT licensed and following libxml2's conventions: its memory functions, error reporting, and Meson and CMake builds.

Not in scope:

- **libxml2's XPath 1.0 engine.** It stays as it is, and its behaviour does not change.
- **XSLT.** XSLT 2.0 and 3.0 belong in libxslt.
- **Schema-aware processing.** Only the built-in XML Schema types are supported.
- **XQuery and streaming.**

## Usage

Compile an expression once, then evaluate it on a document with a dynamic context. Link with `pkg-config --cflags --libs libxpath3`.

```c
#include <stdio.h>
#include <libxml/parser.h>
#include <libxpath3/xpath3.h>

int
main(void) {
    static const char xml[] = "<order><item qty='3' price='2.50'/>"
                              "<item qty='1' price='10'/></order>";
    xmlDocPtr doc = xmlReadMemory(xml, sizeof(xml) - 1, NULL, NULL, 0);
    xp3StaticContext *sctx = xp3NewStaticContext();
    xp3DynamicContext *dctx = xp3NewDynamicContext();
    const int64_t min = 2;
    xp3Sequence *minSeq = xp3NewIntegerSequence(&min, 1);
    xp3Error *error = NULL;
    xp3Expr *expr;
    xp3Sequence *result;
    size_t i;

    /* $min is declared for compiling and bound for evaluating */
    xp3DeclareVariable(sctx, NULL, BAD_CAST "min");
    expr = xp3Compile(sctx, BAD_CAST "for $i in /order/item[@qty >= $min] "
                                     "return $i/@qty * $i/@price", &error);
    if (expr == NULL) {
        fprintf(stderr, "%s: %s\n", xp3ErrorCode(error),
                xp3ErrorMessage(error));
        return(1);
    }

    xp3SetContextNode(dctx, (xmlNodePtr) doc);
    xp3BindVariable(dctx, NULL, BAD_CAST "min", minSeq);
    result = xp3Evaluate(expr, dctx, &error);
    for (i = 0; i < xp3SequenceLength(result); i++) {
        double total;

        if (xp3SequenceDouble(result, i, &total) == 0)
            printf("%s: %g\n",
                   xp3AtomicTypeName(xp3SequenceAtomicType(result, i)),
                   total);
    }

    xp3FreeSequence(result);
    xp3FreeSequence(minSeq);
    xp3FreeExpr(expr);
    xp3FreeDynamicContext(dctx);
    xp3FreeStaticContext(sctx);
    xmlFreeDoc(doc);
    return(0);
}
```

This prints `double: 7.5`: attributes are untyped, so the multiplication casts them to `xs:double`.

## Conformance

Conformance is measured against the W3C [QT3 test suite](https://github.com/w3c/qt3tests), and the pass rate is published here with each release. Fuzzers with allocation-failure injection run in the same style as libxml2's.

Where the specifications leave a choice to the implementation, libxpath3 should behave like [Saxon](https://www.saxonica.com/), the de facto reference implementation that most existing stylesheets and Schematron rule sets are tested against. The same input then gives the same result in both. The first such choice is the precision of decimal division, which was measured with Saxon-HE 10.9 and 12.5; `doc/DESIGN.md` (section 4.3) has the rule and how to repeat the measurement. However, where the specifications are explicit, they win over Saxon.

The gate is compile-only mode: each expression is compiled with the namespaces and variables of its test environment. A test passes if it compiles when the expected result allows that, or fails with the expected error code. An expected static error (`XPST…`, `XQST…`) must be raised by compiling, unless the expression calls `fn:error`, which can raise any code when it runs. Tests that need features outside the scope above (schema awareness, the namespace axis, XQuery, XSLT) are not applicable. Against QT3 commit `201a6e4`:

| Spec | Passed | Failed | Not applicable |
|---|---|---|---|
| XPath 2.0 | 15,293 | 0 | 16,528 |
| XPath 3.0 | 19,372 | 1 (known) | 12,448 |
| XPath 3.1 | 22,030 | 1 (known) | 9,790 |

The known failure, `fn-serialize/serialize-xml-012`, uses the namespace axis without declaring that it needs it. Without the namespace axis, the specification requires the static error XPST0010, which libxpath3 raises; the test expects the error of serializing a namespace node instead. `qt3/known-failures-*.txt` lists it with that explanation.

Evaluation is under way: paths, predicates, `for`, `let`, `some`, `every`, `if`, the logical, comparison and arithmetic operators, ranges, `union`, `intersect`, `except`, `||`, the simple map `!`, arrows to named functions, `cast`, `castable`, `instance of` and `treat` are evaluated. Function calls apply the function conversion rules, and the first built-in functions are implemented: `true`, `false`, `not`, `boolean`, `exists`, `empty`, `count`, `exactly-one`, `zero-or-one`, `one-or-more`, `string`, `data`, `number`, `position`, `last`, `head`, `tail`, `reverse`, `subsequence`, `remove`, `insert-before` and `error`; the others raise `UNSUPPORTED` when called. `qt3run -e` evaluates each test and checks its assertions; tests that need anything not implemented yet are "not judged" rather than failed. Against the same commit:

| Spec | Passed | Failed | Not judged |
|---|---|---|---|
| XPath 2.0 | 6,999 | 3 | 8,291 |
| XPath 3.0 | 7,437 | 4 (1 known) | 11,932 |
| XPath 3.1 | 7,588 | 3 (1 known) | 14,440 |

The failures are the known one above, two ranges of millions of integers beyond 64 bits, which hit the sequence length limit (`doc/DESIGN.md` 4.1), and at 2.0 and 3.0 `fn-error/K-ErrorFunc-4`: `fn:error(())` raises FOER0000 as F&O 3.1 defines it, while F&O 1.0 and 3.0 required a QName, so the test expects XPTY0004 there; the 3.1 signatures apply at every level (`doc/DESIGN.md` 6.3). CI runs this as a report, which doesn't fail the pipeline yet. It also evaluates every test a second time without constant folding and path rewrites (`qt3run -e -c`), and any test whose outcome differs fails the pipeline.

To run it yourself, check out the test suite at the same commit and pass its directory to Meson. `meson test --suite qt3` then runs all three levels:

```sh
git clone https://github.com/w3c/qt3tests.git
git -C qt3tests checkout 201a6e466940cdfc727f4babfedcde5332b9f578
meson setup build -Dqt3=$PWD/qt3tests
meson test -C build --suite qt3
```

`build/qt3/qt3run -s XP31 -v qt3tests/catalog.xml` shows each failing expression with the expected and actual result, `-t PREFIX` limits the run to test sets whose name starts with `PREFIX`, and `-e` evaluates. With CMake, set `-DXP3_QT3_DIR=...`.

Failures that are accepted for now are listed in `qt3/known-failures-XP20.txt`, `-XP30.txt` and `-XP31.txt`, one `test-set/test-case` per line. Any failure not listed there fails the run. So does a listed test that now passes, which must then be removed from the list, so the lists stay accurate.

### Fuzzing

There are two libFuzzer targets. In both, the input's first byte selects the language level, the next two inject an allocation failure at that position, and the rest is the expression.

- `fuzz/xp3parse.c` compiles the expression, so it covers the parser and static analysis, and through constant folding also the evaluator.
- `fuzz/xp3eval.c` evaluates it against a fixed document with every kind of node, and is differential: each input is compiled with and without optimizing, and the two must give the same result, so a folding or path-order bug can't hide. Its first byte also selects the context node.

Both abort when a library contract breaks (no expression and no error, an injected failure not reported as `NO_MEMORY`, a variable slot out of range, or the two evaluations disagreeing); the sanitizers catch the rest. Each file also builds as a replay driver with any compiler, so `meson test` runs the seed corpora in `fuzz/seed/` and `fuzz/seed-eval/` as regression tests everywhere, and every crash the fuzzers find becomes a seed.

Apple's clang ships without the libFuzzer runtime, so real fuzzing runs on Linux. Locally:

```sh
sh .gitlab-ci/run-local.sh fuzz        # 2 minutes each; FUZZ_SECONDS=3600 for longer
```

or natively with clang and compiler-rt:

```sh
CC=clang meson setup build-fuzz -Dfuzzer=true -Db_sanitize=address,undefined -Db_lundef=false
meson compile -C build-fuzz
mkdir corpus && cp fuzz/seed/* corpus
./build-fuzz/fuzz/xp3parse-fuzz -dict=fuzz/xp3.dict -max_len=10000 corpus
```

For the evaluation fuzzer, build `xp3eval-fuzz` the same way and seed it from `fuzz/seed-eval/`. The `fuzz` CI job runs both for two minutes each on every pipeline, and keeps any crash inputs as artifacts. The manual `fuzz:long` job runs them for 50 minutes each on demand, in a child pipeline that the pipeline itself doesn't wait for.

For long runs, such as overnight, run both fuzzers in Docker with the machine kept awake, here for 8 hours each. Each writes its log and any crash inputs to its folder; `fuzz.log` ends in `exit=0` when nothing was found.

```sh
# macOS
caffeinate -i -s sh .gitlab-ci/fuzz-long.sh parse 28800 fuzz-out/parse &
caffeinate -i -s sh .gitlab-ci/fuzz-long.sh eval 28800 fuzz-out/eval &

# Linux
systemd-inhibit --what=sleep sh .gitlab-ci/fuzz-long.sh parse 28800 fuzz-out/parse &
systemd-inhibit --what=sleep sh .gitlab-ci/fuzz-long.sh eval 28800 fuzz-out/eval &

# Windows: in a WSL2 shell, with Docker Desktop running
sh .gitlab-ci/fuzz-long.sh parse 28800 fuzz-out/parse &
sh .gitlab-ci/fuzz-long.sh eval 28800 fuzz-out/eval &
```

On Windows, keep the machine awake on the Windows side, because `systemd-inhibit` in WSL2 only affects the WSL virtual machine: turn on PowerToys Awake for the night, or set `powercfg /change standby-timeout-ac 0` and restore the previous value afterwards. These runs fuzz the Linux build in a container on every system; fuzzing the native Windows build (MSVC, clang-cl) comes with the Windows CI jobs.

The `clang:tsan` job runs the tests under ThreadSanitizer, among them `testthreads`, where several threads evaluate one compiled expression at once.

Every CI pipeline runs the `qt3` job: it fetches QT3 at the pinned commit, runs all three levels under AddressSanitizer and UndefinedBehaviorSanitizer, and ends its log with the pass rate of each level. To use a newer QT3, update the commit in `.gitlab-ci.yml`, `.gitlab-ci/run-local.sh` and this section together.

The `coverage` job measures line coverage of the library sources over the unit tests, the replayed fuzz corpus and the QT3 run, with gcov and [gcovr](https://gcovr.com/). GitLab shows the percentage on the pipeline and the merge request, and marks covered and uncovered lines in merge request diffs; the full HTML report is kept as a job artifact. To produce it locally: `COVERAGE_OUT=coverage-report sh .gitlab-ci/run-local.sh coverage`, then open `coverage-report/coverage/index.html`.

The code follows libxml2's style. No formatter can reproduce it (clang-format cannot keep `return(x)`, for example), so the `style` job checks the mechanical rules instead: at most 79 columns, no tabs or trailing whitespace, no `//` comments, and `return(x)`. Run it before pushing with `python3 .gitlab-ci/check-style.py`. The `.editorconfig` sets up editors to match.

## Building the project

Requirements: Meson 1.3 or later, Ninja, a C99 compiler and libxml2 2.9 or later. Doxygen is optional, for the API documentation. CMake 3.18 or later can be used instead of Meson.

The code is C99, with no variable-length arrays and no compiler extensions.

### macOS

Install the tools and a current libxml2 with Homebrew:

```sh
brew install meson ninja libxml2 doxygen
```

Then build and test:

```sh
export PKG_CONFIG_PATH="$(brew --prefix)/opt/libxml2/lib/pkgconfig"   # Homebrew libxml2, not macOS's 2.9.13

meson setup build            # once
meson compile -C build       # build
meson test -C build          # run tests
```

### Linux

Install the compiler, the build tools and the libxml2 development package.

Debian and Ubuntu:

```sh
sudo apt install build-essential meson ninja-build pkg-config libxml2-dev doxygen
```

Fedora:

```sh
sudo dnf install gcc meson ninja-build pkgconf-pkg-config libxml2-devel doxygen
```

If your distribution's Meson is older than 1.3 (for example Debian 12 or Ubuntu 22.04), install a current one with `pipx install meson`.

Then build and test:

```sh
meson setup build
meson compile -C build
meson test -C build
```

### Windows

Windows builds are not covered by CI yet.

**MSYS2 (MinGW).** In the MSYS2 UCRT64 shell:

```sh
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-meson mingw-w64-ucrt-x86_64-ninja \
          mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-libxml2

meson setup build
meson compile -C build
meson test -C build
```

On Windows on ARM, use the CLANGARM64 shell and the `mingw-w64-clang-aarch64-` packages instead.

**Visual Studio (MSVC).** In a Developer PowerShell or Developer Command Prompt, get libxml2 from [vcpkg](https://vcpkg.io) and build with CMake:

```powershell
vcpkg install libxml2:x64-windows

cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
ctest --test-dir build -C Release
```

MSVC has no separate C99 mode. Meson builds with C11 there, and CMake uses MSVC's default C mode.

### API documentation

With Doxygen installed, configure with `meson setup build -Ddocs=enabled`. The HTML ends up in `build/doc/html/`. With CMake, pass `-DXP3_BUILD_DOCS=ON`.

### CMake

CMake works on every platform:

```sh
cmake -S . -B build-cmake            # on macOS add -DCMAKE_PREFIX_PATH="$(brew --prefix)/opt/libxml2"
cmake --build build-cmake
ctest --test-dir build-cmake
```

## Specifications

- [XML Path Language (XPath) 3.1](https://www.w3.org/TR/xpath-31/)
- [XPath and XQuery Functions and Operators 3.1](https://www.w3.org/TR/xpath-functions-31/)
- [XQuery and XPath Data Model 3.1](https://www.w3.org/TR/xpath-datamodel-31/)

## Contributing

Development happens on GNOME GitLab. Issues and merge requests go to https://gitlab.gnome.org/mutantkeyboard/libxpath3.

Merge requests target the `test` branch; `main` gets releases.

The GitHub copy is a read-only mirror, so please open issues and merge requests on GNOME GitLab.

Related upstream discussions:

- [libxml2#31](https://gitlab.gnome.org/GNOME/libxml2/-/issues/31): XPath 3.0/3.1 support in libxml2
- [libxslt#153](https://gitlab.gnome.org/GNOME/libxslt/-/issues/153): XSLT 2.0 and 3.0 support in libxslt

## Licence

MIT. See [COPYING](COPYING).
