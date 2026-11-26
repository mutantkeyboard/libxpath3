# 2. Variables, namespaces, reuse

Real documents use namespaces, and real applications compile an expression once and evaluate it many times, with different values and on different nodes. This page does both, on a feed whose namespaces are as mixed as they get.

You will learn:

- why an unprefixed name in a namespaced document matches nothing, and three ways to fix it
- that the prefixes of an expression and the prefixes of the document are unrelated
- how to declare variables for compiling and bind them for evaluating, and what `=` and `eq` do with the empty sequence and with several values
- how to evaluate one compiled expression on several context nodes

The full program is [`examples/02-namespaces-variables.c`](../../examples/02-namespaces-variables.c), its document [`examples/data/feed.xml`](../../examples/data/feed.xml), and its output [`examples/expected/02-namespaces-variables.txt`](../../examples/expected/02-namespaces-variables.txt). From here on the programs use the helpers in [`examples/common.c`](../../examples/common.c): `exRun()` compiles, evaluates and prints one query the way [tutorial 1](01-first-query.md) did, and `exEvaluate()` does the same for an expression compiled already.

## The data

```xml
<feed xmlns="http://www.w3.org/2005/Atom" xml:lang="en"
      xmlns:media="http://example.com/ns/media/1"
      xmlns:x="http://example.com/ns/extra">
  <title>Harbour notes</title>
  <entry xml:lang="de">
    <title>Hafenbericht</title>
    <media:thumbnail url="tide.png" media:width="120"/>
    <x:rating x:scale="5">4</x:rating>
  </entry>
  <entry>
    <title>Tide tables</title>
    <link href="tides.html" rel="alternate"/>
  </entry>
  <entry xml:lang="en-GB" xmlns:media="http://example.com/ns/media/2">
    <title>Colour charts</title>
    <media:thumbnail url="charts.png" media:width="240"/>
    <x:rating>3</x:rating>
    <x:rating>5</x:rating>
  </entry>
  <entry>
    <title>Raw import</title>
    <payload xmlns="">
      <title>not an Atom title</title>
    </payload>
  </entry>
</feed>
```

What to look out for:

- The root declares a **default namespace**, so `feed`, `entry` and `title` are in the Atom namespace although they have no prefix.
- The prefix `media` is bound to **two different URIs**: `.../media/1` at the root, `.../media/2` on the third entry.
- `x:rating` comes from a **foreign namespace**, and the third entry has **two** of them.
- `<payload xmlns="">` **undeclares** the default namespace, so the `title` inside it is in no namespace.
- `xml:lang` is set on the root and on two entries; the others **inherit** it.
- `url` and `href` are attributes **without** a namespace, `media:width` and `x:scale` **with** one.

## Unprefixed names

```c
    printf("No default element namespace:\n");
    failures += exRun(sctx, dctx, NULL, "count(/feed/entry)", NULL);
    failures += exRun(sctx, dctx, NULL, "//title", NULL);
```

```text
No default element namespace:
count(/feed/entry) -> xs:integer 0
//title -> element(title) "not an Atom title"
```

**Why:** an unprefixed element name in an expression is in the default element namespace of the *static context* ([XPath 3.1, 2.1.1](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#static_context)), which is no namespace unless you set one. The `xmlns="..."` of the document plays no part. So `feed` means the element `feed` in no namespace, which this document doesn't have, and the query returns 0 without any error. The only `title` in no namespace is the one in `<payload xmlns="">`, the one you didn't want.

### Fix 1: a default element namespace

```c
    printf("Default element namespace Atom:\n");
    if (xp3SetDefaultElementNamespace(sctx, ATOM_NS) < 0) {
        xp3FreeStaticContext(sctx);
        return(1);
    }
    failures += exRun(sctx, dctx, NULL, "count(/feed/entry)", NULL);
    failures += exRun(sctx, dctx, NULL, "//title", NULL);
    failures += exRun(sctx, dctx, NULL, "//Q{}title", NULL);
    failures += exRun(sctx, dctx, NULL, "//link/@href", NULL);
```

```text
Default element namespace Atom:
count(/feed/entry) -> xs:integer 4
//title -> element(title) "Harbour notes", element(title) "Hafenbericht", element(title) "Tide tables", element(title) "Colour charts", element(title) "Raw import"
//Q{}title -> element(title) "not an Atom title"
//link/@href -> attribute(href) "tides.html"
```

Now `title` means the Atom title, and the payload's title needs `Q{}title`, a name in no namespace written as a URI-qualified name. `@href` still works: the default element namespace applies to element and type names only, never to attributes.

### Fix 2 and 3: prefixes, and names that carry their URI

The main static context declares three prefixes. The document happens to use `media` and `x` too, but the expression's `media` is whatever you bind:

```c
    /* The prefixes of the expression, which the document doesn't decide */
    if ((xp3DeclareNamespace(sctx, BAD_CAST "a", ATOM_NS) < 0) ||
        (xp3DeclareNamespace(sctx, BAD_CAST "media", MEDIA_NS) < 0) ||
        (xp3DeclareNamespace(sctx, BAD_CAST "x", EXTRA_NS) < 0)) {
        failures++;
        goto done;
    }
```

```c
static const Query prefixedQueries[] = {
    { "count(/a:feed/a:entry)", NULL },
    { "count(/Q{http://www.w3.org/2005/Atom}feed"
      "/Q{http://www.w3.org/2005/Atom}entry)", NULL },
    { "count(/*:feed/*:entry)", NULL },
    { "count(//*:title)", NULL },
```

```text
Prefixes a, media and x:
count(/a:feed/a:entry) -> xs:integer 4
count(/Q{http://www.w3.org/2005/Atom}feed/Q{http://www.w3.org/2005/Atom}entry) -> xs:integer 4
count(/*:feed/*:entry) -> xs:integer 4
count(//*:title) -> xs:integer 6
```

**Why:** a prefix in an expression is looked up in the static context ([XPath 3.1, 2.1.1](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#static_context)); `Q{uri}local` names the namespace directly and needs no declaration ([XPath 3.1, 2](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#id-basics), EQName); `*:local` matches the local name in any namespace or none ([3.3.2.2](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#node-tests)). `*:title` finds all six titles, Atom or not, which is convenient for exploring and wrong for most production queries.

### One prefix, two namespaces

```c
    /* Two elements written media:thumbnail, in two namespaces */
    { "//*:thumbnail/name()", NULL },
    { "//*:thumbnail/namespace-uri()", NULL },
    { "namespace-uri-for-prefix('media', (//*:thumbnail)[2])", NULL },
    { "//*:payload/namespace-uri()", NULL },
    { "//media:thumbnail/@url", NULL },
    { "//*:thumbnail/@media:width", NULL },
    { "//*:thumbnail/@*:width", NULL },
```

```text
//*:thumbnail/name() -> xs:string "media:thumbnail", xs:string "media:thumbnail"
//*:thumbnail/namespace-uri() -> xs:anyURI "http://example.com/ns/media/1", xs:anyURI "http://example.com/ns/media/2"
namespace-uri-for-prefix('media', (//*:thumbnail)[2]) -> xs:anyURI "http://example.com/ns/media/2"
//*:payload/namespace-uri() -> xs:anyURI ""
//media:thumbnail/@url -> attribute(url) "tide.png"
//*:thumbnail/@media:width -> attribute(media:width) "120"
//*:thumbnail/@*:width -> attribute(media:width) "120", attribute(media:width) "240"
```

**Why:** both elements are written `media:thumbnail`, and `name()` reports exactly that, but a name is its namespace URI plus its local name; the prefix is only how it was written ([F&O 3.1, 13.1](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-name) and [13.3](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-namespace-uri)). Never select by comparing `name()` with a string. With `media` bound to `.../media/1`, `media:thumbnail` finds the first one only, and so does `@media:width`. An element in no namespace has the namespace URI `""`, not the empty sequence.

### Attributes and xml:lang

```c
    /* An unprefixed attribute is in no namespace */
    { "//a:link/@href", NULL },
    { "//a:link/@a:href", NULL },
    { "//x:rating/@x:scale", NULL },
    /* xml:lang is inherited, and "en" matches "en-GB" */
    { "//a:entry[lang('en')]/a:title/string()", NULL },
    { "//a:entry[lang('de')]/a:title/string()", NULL },
    { "//*:payload/*:title/lang('en')", NULL },
```

```text
//a:link/@href -> attribute(href) "tides.html"
//a:link/@a:href -> ()
//x:rating/@x:scale -> attribute(x:scale) "5"
//a:entry[lang('en')]/a:title/string() -> xs:string "Tide tables", xs:string "Colour charts", xs:string "Raw import"
//a:entry[lang('de')]/a:title/string() -> xs:string "Hafenbericht"
//*:payload/*:title/lang('en') -> xs:boolean true
```

**Why:** in XML Namespaces an unprefixed attribute is in no namespace, even on an element in a namespace, so `@a:href` finds nothing. `fn:lang` looks for the nearest `xml:lang` on the node or its ancestors, ignores case, and also matches a tag that continues with a hyphen, so `en` matches `en-GB` ([F&O 3.1, 13.4](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-lang)). The `xml` prefix is always bound; you never declare it.

### An undeclared prefix

```c
    /* No prefix is bound to the Dublin Core namespace */
    { "//dc:creator", "XPST0081" }
};
```

```text
//dc:creator -> ERROR XPST0081 when compiling
```

A prefix the static context doesn't know is a static error, raised by `xp3Compile()` ([XPath 3.1, 2.1.1](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#static_context)). This is the safe side of namespaces: a typo in a prefix fails, while a missing namespace on an unprefixed name silently matches nothing.

## Variables

A variable is declared in the static context, so that compiling knows the name, and bound in the dynamic context, so that evaluating has a value:

```c
    printf("Variables:\n");
    failures += exRun(sctx, dctx, NULL, "$limit > 2", "XPST0008");

    if ((xp3DeclareVariable(sctx, NULL, BAD_CAST "ids") < 0) ||
        (xp3DeclareVariable(sctx, NULL, BAD_CAST "missing") < 0)) {
        xp3FreeStaticContext(sctx);
        return(1);
    }
    failures += exRun(sctx, dctx, NULL, "$missing", "XPDY0002");
```

```text
Variables:
$limit > 2 -> ERROR XPST0008 when compiling
$missing -> ERROR XPDY0002
```

**Why:** a reference to a variable that isn't in scope is a static error, `XPST0008`, so a misspelt variable is caught when compiling, before any data is read ([XPath 3.1, 3.1.2](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#id-variables)). A declared variable without a value can only be noticed when evaluating, as `XPDY0002`, the error for a missing part of the dynamic context ([2.1.2](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#eval_context)).

Binding takes a sequence. The context keeps a reference of its own, so the caller frees its copy at once:

```c
    value = xp3NewIntegerSequence(ids, count);
    if ((value == NULL) ||
        (xp3BindVariable(dctx, NULL, BAD_CAST "ids", value) < 0)) {
        xp3FreeSequence(value);
        return(1);
    }
    /* The dynamic context keeps a reference of its own */
    xp3FreeSequence(value);

    failures += exRun(sctx, dctx, NULL, "$ids = 2", NULL);
    failures += exRun(sctx, dctx, NULL, "$ids != 2", NULL);
    failures += exRun(sctx, dctx, NULL, "$ids eq 2", eqError);
```

```text
$ids bound to ():
$ids = 2 -> xs:boolean false
$ids != 2 -> xs:boolean false
$ids eq 2 -> ()
$ids bound to (1, 2, 3):
$ids = 2 -> xs:boolean true
$ids != 2 -> xs:boolean true
$ids eq 2 -> ERROR XPTY0004
```

**Why:** `=` and `!=` are general comparisons: true if *some* pair of values compares so ([XPath 3.1, 3.7.2](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#id-general-comparisons)). With no values, no pair exists, so both are false and `not($ids = 2)` is not the same as `$ids != 2`. With 1, 2 and 3, both are true. `eq` is a value comparison of exactly one value with one value ([3.7.1](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#id-value-comparisons)): the empty sequence gives the empty sequence, which is false as a predicate, and more than one value is a type error. Use `eq` when you mean "this one value", and get an error instead of a silent existential test when the data has more than you thought.

`xp3NewNodeSequence()`, `xp3NewStringSequence()`, `xp3NewDoubleSequence()`, `xp3NewBooleanSequence()` and `xp3NewAtomicSequence()` make the other kinds of values. Binding the same name again replaces the value.

## One expression, many context nodes

Compile once, then move the context node:

```c
    printf("x:rating + 1 on each entry:\n");
    for (i = 0; i < xp3SequenceLength(list); i++) {
        char label[32];

        snprintf(label, sizeof(label), "entry %d", (int) i + 1);
        xp3SetContextNode(dctx, xp3SequenceNode(list, i));
        failures += exEvaluate(rating, dctx, label, expected[i]);
    }
    failures += exEvaluate(focus, dctx, "position(), last()", NULL);
```

`list` is the result of `/a:feed/a:entry`, `rating` is `x:rating + 1` and `focus` is `position(), last()`, each compiled once.

```text
x:rating + 1 on each entry:
entry 1 -> xs:double 5
entry 2 -> ()
entry 3 -> ERROR XPTY0004
entry 4 -> ()
position(), last() -> xs:integer 1, xs:integer 1
```

**Why:** the element `<x:rating>4</x:rating>` is untyped, and arithmetic casts untyped values to `xs:double` ([XPath 3.1, 3.5](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#id-arithmetic)), so the result is the double 5, not the integer. Where there is no rating, the operand is empty and so is the result. Where there are two, arithmetic on a sequence of two values is a type error. The error belongs to that one evaluation; the loop carries on with the next entry.

The C API sets the context item, not the context position and size. The specification leaves the initial focus to the host ([XPath 3.1, C.2](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#id-xp-evaluation-context-components)); libxpath3 makes both 1. An expression that needs "the third of five" has to compute it, for example `count(preceding-sibling::a:entry) + 1`, or receive it as a variable.

Every result in the loop is freed by `exEvaluate()`; the nodes in `list` stay valid because they belong to the document, which outlives everything.

Next: [3. Numbers](03-numbers.md).
