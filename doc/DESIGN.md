# libxpath3 design

How libxpath3 is built. This document changes along with the code.

## 1. Purpose

libxpath3 evaluates XPath 3.1 directly on documents parsed with libxml2, without converting them to another tree. It is built in stages: the XPath 2.0 core first, then 3.0 and 3.1. It is meant to be the foundation for XSLT 2.0 and 3.0 in libxslt.

The README covers scope and building. This document covers how the library works inside.

## 1a. References

The specifications this library implements, and related ones:

| Specification | URL |
|---|---|
| XML Path Language (XPath) 3.1 | https://www.w3.org/TR/xpath-31/ |
| XPath and XQuery Functions and Operators 3.1 | https://www.w3.org/TR/xpath-functions-31/ |
| XQuery and XPath Data Model 3.1 | https://www.w3.org/TR/xpath-datamodel-31/ |
| XML Path Language (XPath) 2.0 | https://www.w3.org/TR/xpath20/ |
| XQuery 1.0 and XPath 2.0 Functions and Operators | https://www.w3.org/TR/xquery-operators/ |
| XML Schema 1.1 Part 2: Datatypes (atomic types, lexical forms) | https://www.w3.org/TR/xmlschema11-2/ |
| XSL Transformations (XSLT) 3.0, for the libxslt work | https://www.w3.org/TR/xslt-30/ |
| W3C QT3 test suite | https://github.com/w3c/qt3tests |
| XPath 4.0 work (QT4 community group) | https://qt4cg.org/ |

## 2. Constraints

These come from the goal of possibly merging into libxml2 or libxslt later. They apply from the first commit.

| Constraint | Consequence |
|---|---|
| libxml2 public API only | Works against any recent libxml2 2.x, and nothing breaks when libxml2's internals change. Node order, namespace lookup and string values are computed through public functions and struct fields. |
| MIT licence | Same as libxml2 and libxslt. Third-party code may only be included under MIT-compatible terms, with attribution. |
| C99, no VLAs | The project standard is C99: Meson `c_std=c99` (falling back to `c11` for MSVC, which has no C99 mode), CMake `C_STANDARD 99`. Variable-length arrays are not allowed (`-Werror=vla`), and there are no compiler extensions. C99 gives guaranteed 64-bit integers (`<stdint.h>`), which decimal arithmetic, integers, durations and overflow checks need. libxml2 itself still compiles as C89 (its CI has a `gcc:c89` job). If the library is ever merged, the C99 parts, mainly the 64-bit types, are the thing to discuss. |
| No new required dependencies | Decimal arithmetic, regex, date/time and number formatting are implemented in-tree. Optional dependencies (for example ICU for collations) can come later behind a build option. |
| libxml2 memory functions | All allocation goes through `xmlMalloc`, `xmlRealloc` and `xmlFree`, so applications that replace the allocator keep working. |
| Every allocation failure handled | libxml2 has reported out-of-memory reliably since 2.13, and its fuzzers inject allocation failures. libxpath3 must handle them the same way: no crash, no leak, a clean error. |
| Bounded resources | Limits on recursion depth, operation count, and sequence and string size. Every limit produces an error, never a crash. Evaluating untrusted expressions on untrusted documents must be safe. |
| No side effects by default | No file or network access. `fn:doc`, `fn:unparsed-text` and similar functions fail unless the application installs a resolver. |

## 2a. Relationship to libxml2's XPath 1.0 code

libxpath3 is a new engine that sits beside libxml2. It does not extend or modify `xpath.c`.

- **The data models don't match.** libxml2's `xmlXPathObject` has four result types: node-set, boolean, number (a double) and string. XPath 2.0 and later is built on typed sequences: decimals, dates, untypedAtomic, and later maps, arrays and functions. That model runs through every operator and function, so retrofitting it would mean rewriting nearly all of `xpath.c`.
- **The semantics differ.** Comparisons, arithmetic and number-to-string conversion behave differently in 1.0 and 2.0+. Changing them in place risks breaking every existing libxml2, lxml, Nokogiri and PHP user.
- **Past attempts.** A previous effort to move libxml2's XPath to sequences took about a year and was never published (libxslt#153). The libxml2 maintainers suggested a separate library first (libxml2#31).
- **Reuse, not modify.** libxpath3 uses libxml2's public building blocks (tree navigation, dictionaries and hash tables, UTF-8 and URI helpers, namespace lookup) and doesn't duplicate them. Small helpers from `xpath.c` may be copied with attribution where useful, since the licence is the same.
- **Merging later.** If the library is merged, libxml2 can expose it next to the 1.0 API, for example through an option or a separate entry point. Existing behaviour stays unchanged.

## 2b. Repository and workflow

libxpath3 lives in its own repository on GNOME GitLab (`mutantkeyboard/libxpath3`), with a read-only mirror on GitHub (maybe). It is not developed in a fork or branch of libxml2 or libxslt.

- **Matches the maintainers' request.** The libxml2 maintainer suggested a separate library, with integration considered later (libxml2#31).
- **No long-lived fork.** libxml2 master changes every week. A fork would mean constant rebasing and, at the end, one very large MR.
- **The public-API rule is enforced by the build.** The repo builds against an installed or separately built libxml2, so private headers (`libxml.h`, `include/private/`) are not reachable.
- **Independently usable.** It has its own CI, releases and issues, so bindings can use it before any integration decision.

Clones of libxml2 and libxslt are used for:

1. **Reference:** coding conventions, error handling, the fuzz harness and the Unicode generator to adapt, and edge-case behaviour in `xpath.c`.
2. **CI:** libxpath3 is built and tested against libxml2 master built from source, as well as against the latest release.
3. **Small upstream changes:** if libxpath3 needs something libxml2 does not expose, that becomes a separate, focused MR to libxml2, not a workaround or a dependency on internals.

### Layout

Organised the way it would sit inside libxml2 or libxslt, so a later merge is mostly a directory move:

```
libxpath3/
├── include/
│   ├── libxpath3/xpath3.h      public API (single header to start)
│   └── private/                 internal headers, not installed
├── *.c                          sources at the top level, as in libxml2
├── codegen/                     generators and their committed output (built-in function table; Unicode tables, adapted from libxml2)
├── fuzz/                        fuzzers and dictionaries, libxml2 style
├── test/                        unit tests
├── doc/                         Doxygen configuration and this document
├── qt3/                         QT3 runner (the test suite itself is fetched at a pinned commit, not vendored)
├── meson.build, CMakeLists.txt
├── README.md, COPYING
└── .gitlab-ci.yml
```

## 3. Architecture

```
expression string
      │
      ▼
  lexer ──► parser ──► AST ──► static analysis ──► compiled expression (immutable, shareable)
                                                        │
                    dynamic context (context item,      │
                    variables, current time, resolvers) ┼──► evaluator ──► result sequence
                                                        │
                                             libxml2 tree (read-only)
```

- **Compile once, evaluate many times.** A compiled expression is immutable and can be shared between threads. Everything that changes per call lives in the dynamic context.
- **The libxml2 tree is read-only.** The library never writes into the document. libxml2's `xmlXPathOrderDocElems` stores document order by writing a negative counter into each element's `content` field. libxpath3 keeps any such cache in its own side tables instead.
- **Tree-walking interpreter first.** The evaluator walks the analysed AST. A bytecode or iterator-based evaluator is a later optimisation, and the design keeps that possible (see 7.1).

## 4. Data model (XDM 3.1)

### 4.1 Items and sequences

A value is a sequence of items. An item is one of:

| Item kind | Representation |
|---|---|
| Node | `xmlNode *`, borrowed. The document is owned by the caller and must outlive results that reference it. |
| Atomic value | Tagged value with a type ID (4.2). |
| Function item | Reference to a built-in or user function, with captured variables for inline functions and partial application. Stage 2. |
| Map | Persistent hash map from atomic keys to sequences. Stage 2. |
| Array | Persistent vector of sequences. Stage 2. |

- **Room for stage 2 items from day one.** The item tag and memory management include function items, maps and arrays from the start, even though stage 1 never creates them. Adding them later is then an extension, not a rewrite of the core.
- **Values are immutable and reference counted.** Sequences, maps, arrays and closures share structure instead of copying. Nodes are never reference counted, because libxml2 owns them.
- **Sequence representation.** Empty sequences and singletons need no allocation beyond the item. Longer sequences are reference-counted arrays. Integer ranges (`1 to 1000000`) are stored as a range and expanded only when needed.
- **Range bounds are 64-bit.** A range stores its first and last value as 64-bit integers and is never expanded. A range with a bound outside 64 bits, such as `1000000000000000000000 to 1000000000000000000003`, is built item by item instead, as long as it is no longer than the maximum sequence length (7.3); a longer one raises `LIMIT`, an implementation limit the spec allows. Since 7c; before, any bound outside 64 bits raised `LIMIT`. QT3 has two tests with ranges of millions of such integers, `op-to/RangeExpr-409c` and `-409d`, which stay at `LIMIT`.
- **No small-integer fast path, for now.** Every `xs:integer` is an arbitrary-precision decimal (4.3), even `1`. That keeps one representation and one set of arithmetic rules. A fast path for integers that fit in 64 bits can be added later behind the same functions, if profiling shows the need.

### 4.2 Atomic types

Without schema awareness, element content is `xs:untyped` and attribute values are `xs:untypedAtomic`, so atomizing a node gives its string value as `xs:untypedAtomic`.

Stage 1 types:

| Group | Types | Implementation notes |
|---|---|---|
| String-like | `xs:string`, `xs:untypedAtomic`, `xs:anyURI` | UTF-8, as libxml2. Positions in functions such as `substring` count code points, not bytes. |
| Boolean | `xs:boolean` | |
| Decimal | `xs:decimal`, `xs:integer` and the built-in integer subtypes | Arbitrary precision, in-tree (4.3). |
| Floating point | `xs:double`, `xs:float` | IEEE 754. Converting to string needs shortest round-trip formatting (4.4). |
| Date and time | `xs:dateTime`, `xs:date`, `xs:time`, the `xs:gYear` family | Timezone handling and comparison per the F&O rules, using the implicit timezone from the dynamic context. |
| Durations | `xs:duration`, `xs:yearMonthDuration`, `xs:dayTimeDuration` | Stored as months plus seconds, with exact decimal seconds. |
| Other | `xs:QName`, `xs:hexBinary`, `xs:base64Binary` | |

Derived types such as `xs:nonNegativeInteger` or `xs:token` share the representation of their primitive type and carry their own type ID, which the cast and `instance of` checks use.

Casting follows F&O 3.1, 19, with two choices where the spec and the reference implementations part (7c):

- **`xs:double` to `xs:decimal` gives the shortest decimal that reads back as the double**, as Saxon does: `xs:decimal(0.1e0)` is `0.1`. Read literally, "the decimal numerically closest to the double" (19.1.2.3) is the double's exact binary value, `0.1000000000000000055511151231257827021181583404541015625`, because libxpath3's decimals are long enough to hold it. The shortest form is read as what a double's precision can represent, and it is what users expect. Casting to `xs:integer` keeps the exact value, truncated.
- **`xs:double` to `xs:float` rounds to nearest**, as IEEE 754, Java and Saxon do. 19.1.2.1 describes truncating the mantissa, but QT3 expects rounding (`prod-CastExpr/CastAs206` expects `-0.00175` for `xs:double("-1.75e-3") cast as xs:float`), and truncation would make `xs:float("x")` differ from `xs:float(xs:double("x"))` for the same number (`prod-Predicate/predicates-11`). The test suite wins here.
- **Not yet castable:** strings to `xs:QName` and `xs:NOTATION` need the namespaces of the expression, which the compiled form doesn't keep yet; the date, time and duration types come with Part 10. Both raise `UNSUPPORTED`.

### 4.3 Decimal arithmetic

Exact decimal arithmetic is a hard requirement. Rules that compare sums of amounts fail on binary floating-point rounding, which is exactly what `xs:decimal` exists to prevent.

- **In-tree implementation.** Arbitrary precision, stored as base-10⁹ limbs with a sign and a scale. Add, subtract and multiply are exact. Division and the `round` family take an explicit precision and rounding mode.
- **Division precision is implementation-defined, and libxpath3 matches Saxon.** The spec requires at least 18 digits. Saxon is the de facto reference: existing stylesheets and Schematron rules are developed and tested against it, so a different rounding would make the same document validate differently. The rule comes from Saxon-HE 12.5's source (`Calculator.internalDecimalDivide`, `Int64Value.div` and `isLong`, and the `BigDecimalValue` constructors), and was confirmed by measuring about 90 divisions with Saxon-HE 10.9 and 12.5, which agree on all of them:
  1. The quotient is rounded to max(18, s₁ − s₂ + 18) digits after the decimal point, to nearest, with ties rounded toward zero (Java's `RoundingMode.HALF_DOWN`), and trailing zeros are removed.
  2. s₁ and s₂ are the operands' scales: the number of digits after the point once trailing zeros are removed, which is negative for values that end in zeros (`300000` has scale −5). That is how an `xs:decimal` operand counts.
  3. An `xs:integer` operand counts with scale 0 instead, without removing its zeros, with one exception: when both operands are integers and either is negative or at least 2³¹, both count like decimals. (Saxon sends such pairs through Java's `BigInteger`, and its `isLong` test is `(value >> 31) != 0`, which is also true for every negative value.)

  For example `1 div 3` is `0.333333333333333333`, `0.001 div 3` is `0.000333333333333333333` (21 digits), `1 div 0.7` is `1.428571428571428571` (the 18-digit floor), `1 div 300000` is `0.000003333333333333` (18 digits: two small integers), while `1 div 300000.0` is `0.00000333333333333333333` (23: a decimal divisor) and `1 div 3000000000000000000` has 36 digits (an integer above 2³¹). `25 div 10000000000000000000` is exactly `0.0000000000000000025`, `1 div 2^100` is `0`, and `3 div 524288` is `0.000005722045898437`, where the exact value ends in a 5 and the tie goes toward zero, not to the even digit. `xp3DecimalDivTyped()` implements this, and the measured cases are test vectors in `test/testdecimal.c`. To repeat the measurement, run an XQuery file of divisions through `java -cp Saxon-HE.jar net.sf.saxon.Query -q:file.xq`.
- **The spec wins where it is explicit.** Saxon is followed only where the spec leaves a choice. For example, the quotient of two integers is an `xs:decimal` even when it is whole, as the spec requires, although Saxon also reports `(6 div 2) instance of xs:integer` as true.
- **`xs:integer` is arbitrary precision too.** For example, IBAN validation computes `mod 97` on a 30-digit integer.

### 4.4 Converting numbers to strings

Casting `xs:double` to `xs:string` must produce the shortest representation that converts back to the same value. Numbers between 1e-6 and 1e6 use plain decimal notation, others use exponent notation. libxml2's XPath 1.0 formatting does not follow these rules.

**Decided: a round-trip search, not Ryu.** The shortest digits are found by trying 1 to 17 significant digits with `snprintf("%.*e")` and keeping the first result that `strtod` reads back as exactly the same value. Seventeen digits always round-trip for IEEE 754 doubles, so the search always ends; `xs:float` works the same way with up to 9 digits. The XDM notation rules are then applied to those digits.

The trade-off, for anyone who wants to revisit it:

| | Round-trip search (chosen) | Ryu |
|---|---|---|
| What it is | Brute force over libc: format, parse back, compare | A dedicated algorithm (Ulf Adams, 2018) that computes the shortest digits directly with precomputed tables and integer arithmetic |
| Correctness | Correct by construction, because the round trip is the requirement. Relies on a correctly rounded `strtod`, which glibc, macOS and musl have | Correct, and proven so in the paper |
| Speed | Up to 17 `snprintf` and `strtod` calls per number, roughly 10 to 50 times slower | One pass, among the fastest known methods |
| Code | About 30 lines | About 2,000 lines of dense C to vendor, with tables |
| Licence | Nothing to carry | Apache 2.0 or Boost, compatible with MIT, but the notice has to be kept |

**One case follows Saxon rather than the bare minimum.** The spec allows several strings for the same value, as long as they read back as it, and leaves the choice to the implementation, so the D3 policy applies. Exponent notation always shows at least two digits (`1.0E7`). When one digit would round-trip, Saxon, through Java's `toString`, shows the closest two-digit value instead of padding with 0. That only changes the smallest subnormals: `4.9E-324` rather than `5.0E-324`, and `1.4E-45` for the smallest float. libxpath3 does the same, with one extra `snprintf` call.

Both methods produce identical output, because both are defined by the shortest digits that round-trip. Converting numbers to strings is not a hot path for the intended uses, so the simpler code wins. Ryu can replace the search later without any caller changing, because everything goes through one function in `xp3double.c`. The trigger to do that is a profile, for example of `fn:string` over millions of numbers, not an assumption.

### 4.5 Nodes

- **Document order.** Computed from the tree through public fields and kept in a side table that the dynamic context owns: a hash table from node pointers to positions, filled one whole tree at a time, the first time a node of that tree is compared. Trees are ordered by when they were first indexed, which keeps the order between documents stable, as XDM 3.1 2.4 requires. Nodes are never modified. Sorting input that is already in order moves nothing, and sorting a single node doesn't index its tree.
- **Node identity.** Pointer identity of `xmlNode *`. A text node is identified by one libxml2 node of its run (below).
- **Namespace axis.** Not supported, which the spec allows. Using it raises `XPST0010`. libxml2 represents namespace nodes in an unusual way. Leaving them out avoids that, and the namespace axis is deprecated in XPath 2.0 and later anyway. `fn:in-scope-prefixes` and `fn:namespace-uri-for-prefix` still work.
- **Text nodes.** libxml2 can produce adjacent text nodes and CDATA sections, and empty text nodes, but XDM has neither. Navigation presents each run of adjacent text and CDATA nodes as one XDM text node, represented by the first libxml2 node of the run that has content; its string value is the text of the whole run. A run without content is no node. Every axis returns only representatives, and `xp3NodeCanonical()` maps any node of a run to its representative, for nodes that come from the application.
- **Nodes XDM doesn't have.** The DTD, XInclude markers and declarations are skipped by navigation. Text on both sides of a skipped node belongs to one run.
- **Entity references.** Without `XML_PARSE_NOENT`, libxml2 keeps references to user-defined entities as `XML_ENTITY_REF_NODE`. Their content is shared by every reference to the entity, and its parent is the declaration, not the reference, so it can't be shown in place without breaking node identity, document order and the parent axis. Entity references are skipped with their content, in navigation and in string values alike, attribute values included, so `string(e)` always equals the concatenated text nodes of `e`. Applications parse with `XML_PARSE_NOENT` to see the content, as libxslt's `XSLT_PARSE_OPTIONS` already does.

## 5. Static and dynamic context

**Static context**, set before compiling:
- namespace bindings
- the default element/type namespace and the default function namespace
- in-scope variable names
- the function registry, including extension functions (6.3)
- collations
- the base URI
- the language level: XPath 2.0, 3.0 or 3.1 (5.1)

**Dynamic context**, set per evaluation:
- the context item, position and size
- variable values
- the current date and time. It is captured once and stays stable for the whole evaluation, as the spec requires.
- the implicit timezone
- the default collation
- document and text resolvers (none by default)
- resource limits

Collations: stage 1 supports the Unicode codepoint collation only. XPath 3.1's HTML ASCII case-insensitive collation follows in stage 2. Language-aware collations would need ICU, which is not planned for now. Requesting any other collation raises `FOCH0002`.

### 5.1 Language level

There is one API for all XPath versions from 2.0 on. There is no separate XPath 2.0 interface.

- **Why one API is enough.** XPath 3.1 is backwards compatible with 2.0: valid 2.0 expressions are valid 3.1 and give the same results. The few differences are listed in the incompatibilities appendices of XPath 3.0 and 3.1, for example `function` and `switch` becoming reserved function names, and new syntax such as `||`, `!` and `Q{...}` names being recognised. Saxon handles 2.0 code the same way, with one engine. A second interface would duplicate every function and its documentation.
- **What the setting does.** The static context holds a language level: `XP3_XPATH_20`, `XP3_XPATH_30` or `XP3_XPATH_31`, with 3.1 as the default. At a lower level, the parser and the function library reject constructs and functions from later versions with a static error. They do not change the semantics.
- **What it's for:**
  - applications that need their expressions to stay portable to other XPath 2.0 engines
  - running the QT3 tests per version (`XP20`, `XP30`, `XP31`)
  - the staged delivery: the 2.0 level is complete before 3.1 is
- **XPath 1.0 compatibility mode** is a separate flag, designed for but not built yet. It changes semantics, not just the accepted syntax.

## 6. Expressions and functions

### 6.1 Parser

- **Hand-written recursive descent**, with precedence climbing for operators. No parser generator, which matches libxml2's style and keeps error messages under control.
- **Full XPath 3.1 grammar from the start.** Expressions from later stages parse correctly and fail with "not implemented yet" during static analysis. That way the grammar is designed once.
- **XPath keywords are not reserved.** `for`, `if`, `div` and others are also valid element names, so the lexer decides from what follows (for example `if (` versus `if/x`). A leading `/` followed by an operator, such as `/ * 5`, needs the special handling the spec describes.
- **Errors carry a W3C error code and a position**, for example `XPST0003` with line and column.

### 6.2 Static analysis

`xp3Compile()` parses an expression, then walks the tree once and resolves every name against the static context. The results are stored in the tree itself, which becomes the compiled expression.

- **Names.** Prefixes resolve through the statically known namespaces (XPST0081). Unprefixed element and type names get the default element/type namespace, function names the function namespace, and attribute and variable names no namespace. A name in the namespace reserved for xmlns attributes is an error (XQST0070, XPath 3.1 2.4.5).
- **Variables become slots.** Every range variable, inline function parameter and declared variable in use gets a slot number, and every reference resolves to one (XPST0008). Evaluation can then keep variables in an array instead of looking them up by name. The declared variables in use are listed with their slots, so that a dynamic context can bind them by name.
- **Functions** are checked against the built-in function table at the language level, so a 3.0 function is unknown in an XPath 2.0 context (XPST0017). Functions that are not implemented yet still compile, and fail when called.
- **Constructor functions become casts.** `xs:integer($x)` compiles to `($x cast as xs:integer?)`, which is how XPath 3.1, 3.14.4 defines it, so evaluation only has to implement cast. A constructor function used as a function item (`xs:date#1`, `xs:integer(?)`) stays a call.
- **Type names** follow the rule for where they appear. A sequence type needs a generalized atomic type, which is an atomic type or a union of them such as `xs:numeric` (XPST0051). Cast and castable need a simple type that is not abstract (XQST0052, and XPST0080 for `xs:NOTATION`, `xs:anySimpleType` and `xs:anyAtomicType`); XPath 2.0 only casts to atomic types and calls the error XPST0051. Element and attribute tests need a known type (XPST0008). Without a schema there are no element or attribute declarations, so `schema-element()` and `schema-attribute()` always raise XPST0008.
- **Also static:** the namespace axis, which is not supported (XPST0010), and duplicate parameter names in an inline function (XQST0039).
- **The compiled expression is self-contained and immutable.** URIs from the static context are copied into the expression's arena, so the context can be changed or freed after compiling. Nothing writes to the expression afterwards, so several threads can evaluate it at once.
- **Positions.** An error carries the position of its node: the start of the name, or for an arrow (`=> f()`), the start of its left side, because the tree does not keep the position of the function name.
- **Optimizing (since 7d).** After names are resolved, the tree is optimized; nothing changes the result, and nothing makes compiling fail. An internal switch in the static context turns it off, for tests and for the QT3 runner's comparison (9).
  - **Constant folding.** A subexpression is *closed* if it needs nothing from outside: no focus (an inner focus, as in a predicate, doesn't count), no variable bound outside it, no function call (until Part 8, which can fold calls to functions without the focus-dependent, context-dependent or nondeterministic properties of the 6a table). Each largest closed subexpression is evaluated once, by the evaluator, and becomes a `CONSTANT` node: `1 + 2`, `'12' cast as xs:integer`, `for $i in 1 to 3 return $i * 2`, or just a number literal, which then isn't parsed on every evaluation. Variables bound inside get their slots during analysis after every variable visible from outside, so "refers to no outer variable" is a comparison of slot numbers, and the whole pass is linear.
  - **Errors stay dynamic.** A fold that raises an error is dropped and the subexpression kept, so `if (0) then 1 div 0 else 1` folds to `1`, and `1 div 0` alone still fails when evaluated (XPath 3.1 2.3.4).
  - **Budget.** Folding happens on every compile, also of untrusted expressions, so each fold gets at most 10,000 evaluation steps and 10,000 items, and all folds of an expression 1,000,000 steps together (7.3). A closed but expensive subexpression is just left as it is.
  - **Pruning.** An `if` whose condition is constant becomes the branch it picks; an `and` with a constant false operand becomes false, an `or` with a constant true one true (XPath 3.1 3.8 lets the other operand go unevaluated).
  - **Constants are shared without locks.** Several threads may evaluate one compiled expression, so a constant and its atomic values carry a count that the reference functions leave alone (-1), and evaluating one writes no memory. Constants are deep copies, so no atomic value belongs to two of them, and `xp3FreeExpr()` frees them. A result of `xp3Evaluate()` that contains parts of a constant gets copies of them instead, so results still outlive their expression.
  - **Paths** get their ordering properties and the `//x` rewrite (7.1).

### 6.3 Function library and extension functions

- **One table of built-in functions**, each entry holding name, arity, signature and implementation. Function calls apply the function conversion rules from the signature: atomize, cast `xs:untypedAtomic`, promote numeric types.
- **The table is generated from the W3C specifications.** `codegen/genFunctions.py` reads the F&O 3.1 function catalog, the XML source of the Recommendation's function descriptions, and writes `codegen/functions.inc`. Each entry is one signature: namespace, local name, arity, the first language level that has it, the function properties of F&O 3.1 1.7.4 (focus-dependent, context-dependent, nondeterministic, higher-order, and nondeterministic with respect to ordering); constant folding skips a call with any of the first four, while the fifth only tells the evaluator not to rely on the order of the result, and the signature text. The language level comes from comparing with F&O 1.0 (XPath 2.0) and F&O 3.0 by name and arity, so `fn:round#1` is 2.0 while `fn:round#2` is 3.0. Generating the 273 signatures avoids copying mistakes, and the few corrections to the catalog's own mistakes are listed in one place, the generator. The generator is Python, like libxml2's `codegen/`; the output is committed, so building needs no Python. The signature text is the 3.1 one at every level: where a type changed between versions, as the argument of `fn:string-join` did (`xs:string*` before 3.1, `xs:anyAtomicType*` since), the 3.1 type applies at level 2.0 and 3.0 too, because the level restricts which functions exist, not their types (5.1). Constructor functions such as `xs:integer(...)` are not in the table: they follow from the atomic types (4.2).
- **Stage 1 priority:** the functions that real-world validation rule sets use most come first. Those are `exists`, `count`, `sum`, `round`, `abs`, `matches`, `tokenize`, `replace`, `string-join`, `string-to-codepoints`, `upper-case`, `distinct-values`, `ends-with` and the XPath 1.0 string functions. The rest of the XPath 2.0 function library follows by chapter of the spec.
- **Extension functions:** applications register C functions with a signature written as text, for example `xs:decimal u:decimalOrZero(item()*)`. libxpath3 parses the signature and applies the conversion rules before calling the C function, so extension functions receive correctly typed arguments.

### 6.4 Regular expressions

XPath uses the XML Schema regex dialect plus `^`/`$` anchors, the `s m i x q` flags, back-references, reluctant quantifiers and capture groups. libxml2's `xmlRegexp` cannot be reused, because it only matches whole strings and has no capture groups or search.

- **Own backtracking engine**, with a step budget so that pathological patterns fail with an error instead of hanging.
- **Character classes:** includes class subtraction (`[a-z-[aeiou]]`), `\i` and `\c`, and `\p{...}` categories and blocks.
- **Unicode tables generated in-tree.** libxml2 removed the public `xmlUCSIs*` functions (`xmlunicode.h` is now an empty, deprecated header). It still generates internal tables for its own regex engine (`codegen/genUnicode.py` producing `codegen/unicode.inc`), and libxpath3 can adapt that generator with attribution, since the licence is the same. The tables target the current Unicode version, not libxml2's 4.0.1 from 2004, and are regenerated when new Unicode versions come out.
- **Its own fuzzer.**

## 7. Evaluation

### 7.1 Strategy

- **Stage 1 evaluates eagerly**, with explicit short-circuits where it matters: `exists`, `empty`, `[1]`, `head`, `some`/`every`, `and`/`or`, and effective boolean value.
- **Built-in functions and operators consume their arguments through a small iterator interface**, even while everything behind it is still eager. That keeps a later switch to lazy evaluation local to the evaluator.
- **Path expressions sort into document order and remove duplicates only when needed.** One axis step from one context node is already in document order, because the step collects its axis in order and reverses a reverse axis after its predicates. Anything else (several context nodes, or a non-step on the right of `/`) goes through the document-order table of 4.5, which checks input that is already in order without moving it. That table indexes the whole document, so static analysis proves order where it can (since 7d): each step of a path gets properties of the nodes so far: in document order without duplicates, no node containing another ("peers"), at most one node. A child or attribute step from ordered peers is ordered peers again, and a descendant step from them is ordered; one axis from a single node is always ordered. Predicates only filter, so they keep the properties. A step proven ordered is marked, and evaluation skips the sort: `/a/b/c` or `a/@id` never build the table.
- **`//x` becomes `descendant::x`.** `//x` is `descendant-or-self::node()/child::x`, which visits every node and then its children, and from more than one node isn't in order. When no predicate of the child step can depend on position, it is rewritten to `descendant::x[...]`, the same nodes in one pass, as Saxon does. A predicate counts as position-free if its value can only be a boolean or nodes: a comparison, `and`, `or`, `some`, `every`, `instance of`, `castable`, or a path ending in an axis step. `//x[@id]` is rewritten, `//x[1]` and `//x[$n]` are not.
- **The focus and variables.** The focus (context item, position, size) is kept in the evaluation state and saved around the expressions that change it: path steps, predicates and filters. Variables live in an array of slots that static analysis numbered (6.2); declared variables get their values from the dynamic context when an evaluation starts, and a missing one is `XPDY0002`.
- **Clauses loop, they don't recurse.** `for`, `let`, `some` and `every` iterate over their bindings with an explicit state per binding. A long list of bindings is not nesting, so the parser's depth limit doesn't bound it, and recursing per binding would let a 1 MB expression overflow the stack.
- **Predicates.** A predicate whose value is a single number selects by position; any other value counts by its effective boolean value (XPath 3.1, 3.2.1, 3.3.3 and 2.4.3).
- **The document-order table lives for one evaluation.** It is created when a path first needs sorting and freed at the end, so a tree changed between evaluations never meets a stale table.

### 7.2 Errors

- Every error carries a code (an `xs:QName`, for example `err:FOAR0001`), a message and a source position.
- Errors are returned through the evaluation call, so there is no global error state. libxpath3 has its own `xp3Error` type, plus an adapter that forwards errors to a libxml2 structured error handler. libxml2 has an `XML_FROM_XPATH` domain, but XPath 3 error codes are QNames, not integers.
- `fn:error` raises user-defined errors through the same mechanism.
- **Not implemented yet.** Until every part of the language is evaluated, an expression that needs a missing part (an operator before Part 7c, a function call before Part 8, maps and arrays before Part 12) compiles, and raises `UNSUPPORTED` when evaluated. Like `NO_MEMORY` and `LIMIT`, it is not a W3C code and has no namespace, so it can't be mistaken for a real XPath error.

### 7.3 Limits

Defaults are generous and configurable per dynamic context, following the example of libxml2's XPath operation limit:

- maximum recursion depth for parsing and evaluation
- maximum operation count
- maximum sequence length and string length
- the regex step budget

The maximum sequence length exists since 7b: `xp3SetMaxSequenceLength()`, 10 million items by default like libxml2's node-set limit, and integer ranges don't count because they are never expanded. Evaluation recursion follows the tree, whose depth the parser limits, and clauses loop (7.1). The evaluator counts steps since 7d (each expression evaluated, each node an axis visits, each pair a general comparison tries, each item of an expanded range); constant folding uses it as its budget (6.2). At run time it is not limited yet, so nested `for` clauses can still take as long as their combinations do; a public limit on it is the next step for untrusted expressions.

## 8. Public API sketch

Library name `libxpath3`, API prefix `xp3`. Compiling and evaluating exist since 6b and 7b with these names; extension functions come with Part 8.

```c
/* Static context and compilation */
xp3StaticContext *xp3NewStaticContext(void);
int  xp3DeclareNamespace(xp3StaticContext *sctx, const xmlChar *prefix, const xmlChar *uri);
int  xp3DeclareVariable(xp3StaticContext *sctx, const xmlChar *uri, const xmlChar *localName);
int  xp3SetDefaultElementNamespace(xp3StaticContext *sctx, const xmlChar *uri);
int  xp3RegisterFunction(xp3StaticContext *sctx, const char *signature,
                         xp3FunctionImpl impl, void *userData);
int  xp3SetStaticContextLanguageLevel(xp3StaticContext *sctx, xp3LanguageLevel level);  /* XP3_XPATH_20, _30, _31 (default) */
xp3Expr *xp3Compile(const xp3StaticContext *sctx, const xmlChar *expr, xp3Error **err);
void xp3FreeExpr(xp3Expr *expr);

/* Evaluation */
xp3DynamicContext *xp3NewDynamicContext(void);
void xp3FreeDynamicContext(xp3DynamicContext *dctx);
int  xp3SetContextNode(xp3DynamicContext *dctx, xmlNode *node);  /* any node of a text run stands for the run */
int  xp3BindVariable(xp3DynamicContext *dctx, const xmlChar *uri, const xmlChar *localName,
                     xp3Sequence *value);                           /* adds a reference; NULL unbinds */
int  xp3SetMaxSequenceLength(xp3DynamicContext *dctx, size_t max);
xp3Sequence *xp3Evaluate(const xp3Expr *expr, xp3DynamicContext *dctx, xp3Error **err);
int  xp3EvaluateBoolean(const xp3Expr *expr, xp3DynamicContext *dctx, xp3Error **err);  /* 1, 0, -1 */

/* Sequences: immutable and reference counted */
xp3Sequence  *xp3NewNodeSequence(xmlNode **nodes, size_t count);
xp3Sequence  *xp3NewStringSequence(const xmlChar *str);
void          xp3FreeSequence(xp3Sequence *seq);
size_t        xp3SequenceLength(const xp3Sequence *seq);
int           xp3SequenceItemKind(const xp3Sequence *seq, size_t i);     /* xp3ItemKind, -1 past the end */
xmlNode      *xp3SequenceNode(const xp3Sequence *seq, size_t i);
xmlChar      *xp3SequenceStringValue(const xp3Sequence *seq, size_t i);  /* caller frees with xmlFree */

/* Errors */
const xmlChar *xp3ErrorCode(const xp3Error *err);      /* e.g. "FOAR0001" */
const xmlChar *xp3ErrorMessage(const xp3Error *err);
void           xp3ErrorFree(xp3Error *err);
```

Schematron-style use: compile each rule once at startup, then evaluate it against each document with a fresh dynamic context. Bindings for Ruby (Nokogiri), Python (lxml) and PHP can be thin, because the API works on the `xmlNode *` they already hold.

## 9. Testing

- **QT3 runner:** reads the W3C QT3 catalogue and selects tests by spec dependency (`XP20`, `XP30`, `XP31`), running each set at the matching language level (5.1), and by feature, skipping schema awareness and the namespace axis. It reports pass, fail and not-applicable per test set. The pass rate is published in the README and tracked in CI, so regressions fail the build. Its compile-only mode is the gate: each expression is compiled with the namespaces and variables of its test's environment, expected static errors must be raised by compiling, unless the expression calls `fn:error`, and other expected errors count as "may compile". Tests that conflict with the specification are listed in `qt3/known-failures-*.txt` with the reason. Since 7b, evaluation mode (`-e`) also evaluates each expression, with the source documents and parameters of its environment, and checks the assertions. Assertions written in XPath (`assert`, `assert-eq`, `assert-deep-eq`, `assert-type`) are themselves evaluated with `$result` bound, so they become checkable as the operators and functions they use are implemented. A test that needs anything not implemented yet, in the expression, its environment or its assertions, is "not judged", counted apart from passes and failures so the pass rate stays honest. CI runs evaluation as a report: its failures don't fail the job yet, but crashes and sanitizer errors do.
- **Unit tests** for the parts that are easy to get subtly wrong: decimal arithmetic, double-to-string conversion, date and duration arithmetic, regex, document order with adjacent text nodes.
- **Differential testing (optional):** compare results with Saxon on a corpus of real expressions, such as published Schematron rule sets. Saxon runs only in CI and is never a dependency.
- **Fuzzers**, modelled on libxml2's `fuzz/xpath.c` with a dictionary of XPath tokens:
  - an expression fuzzer, compile only
  - an expression plus document fuzzer, for evaluation
  - a regex fuzzer
  - allocation-failure injection in all of them, using the same approach as libxml2's fuzz helpers (`xmlFuzzInjectFailure`, `xmlFuzzCheckFailureReport`)

  The goal is OSS-Fuzz once the code is stable.
- **CI on GNOME GitLab:** builds with Meson and CMake, with ASan and UBSan, against libxml2 master and the latest release.
- **Coverage:** a CI job builds with gcov, runs the unit tests, the replayed fuzz corpus and QT3, and reports line coverage of the library sources with gcovr in GitLab's native format: the percentage on pipelines and merge requests, covered and uncovered lines marked in merge request diffs. No external service such as Codecov, which would need an account, a token and an upload step for little more. Only this job sets GitLab's coverage value, because GitLab averages the values of all jobs; the QT3 pass rate is in the `qt3` job log and the README instead.
- **Code style, no formatter:** the code follows libxml2's style, which libxml2 keeps by hand: `return(x)`, the function name at the start of its line, hand-aligned tables and comments. clang-format cannot express it. Even a tuned configuration rewrites about 20% of the lines, including every `return(x)` as `return (x)`, because its only option for that space also changes `if (`. So a `style` CI job (`.gitlab-ci/check-style.py`) checks the mechanical rules instead: at most 79 columns, no tabs except in the public header's libxml2-style declarations, no trailing whitespace, no `//` comments, and `return(x)` or `return;`. An `.editorconfig` based on libxml2's sets up editors.

### Platforms

Development happens on macOS (Apple Silicon). Linux is tested locally in Docker. Windows containers need a Windows kernel and cannot run on macOS, so Windows is tested in CI. A local VM is used only to debug Windows-specific failures.

| Platform | Local | CI |
|---|---|---|
| macOS | Development machine | GitHub Actions on the mirror (optional) |
| Linux | Docker | GNOME GitLab: gcc, a strict C99 job (`-std=c99 -pedantic-errors -Werror=vla`), clang with ASan and UBSan, CMake, against libxml2 master and the latest release |
| Windows | Windows 11 on ARM in UTM or VMware Fusion, only when needed | GNOME GitLab `win32-ps` runners if available, otherwise GitHub Actions: MSVC and MinGW |

- **GNOME Windows runners.** libxml2 runs its MinGW (`mingw:*`, `cmake:mingw:*`) and MSVC (`cmake:msvc:*`) jobs on GNOME's runners tagged `win32-ps`. Those jobs are the template. The Linux jobs already run on GNOME's shared runners. Access to the Windows runners is not confirmed yet. If not, Windows and macOS run on GitHub Actions on the read-only mirror, which is free for public repositories.
- **Local Windows VM.** The guest must be Windows on ARM, because the host is Apple Silicon. VirtualBox support for Apple Silicon is immature, so UTM, VMware Fusion or Parallels is better. arm64 builds run natively and x64 builds run under emulation, which is enough for tests but not for performance measurements. Windows needs a licence for long-term use.
- **Windows-specific points in the code.** Symbols are exported through the `XP3PUBFUN` macro (`dllexport`/`dllimport`). MSVC needs `/std:c11` and has no variable-length arrays, which the no-VLA rule already covers. On Windows, libxml2 comes from vcpkg or MSYS2, or is built from source as in libxml2's own CI.

## 10. Risks

| Risk | Mitigation |
|---|---|
| The work is large and it's one person's spare time | Stages that are useful on their own. The XPath 2.0 core already covers real Schematron rule sets. |
| Subtle conformance errors (decimals, dates, regex) | QT3 in CI, focused unit tests, differential tests against Saxon. |
| Memory-safety bugs in C | Fuzzing with allocation-failure injection from the start, ASan and UBSan in CI, resource limits. |
| Integration never happens | Usable standalone. Bindings can link it directly, as they would any other library. |
| libxml2 changes public behaviour this depends on | Public API only, CI against libxml2 master. |
