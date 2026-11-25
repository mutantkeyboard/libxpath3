# 1. First query

This page parses a document with libxml2, compiles XPath expressions, evaluates them, walks the results item by item and reports errors. The document is a small library catalogue that was typed in by hand, so it contains most of what real XML contains and what a first XPath program tends to get wrong.

You will learn:

- the life cycle of a query: a static context, `xp3Compile()`, a dynamic context, `xp3Evaluate()`, a sequence, and what to free when
- how to tell nodes from atomic values in a result, and how libxml2's tree maps onto XPath's nodes
- why `@year > 1900` fails on one record, why `string-length(author)` fails on another, and why an empty result is not an error

The full program is [`examples/01-first-query.c`](../../examples/01-first-query.c), its document [`examples/data/library.xml`](../../examples/data/library.xml), and its exact output [`examples/expected/01-first-query.txt`](../../examples/expected/01-first-query.txt). The build runs it as a test and compares the output byte for byte. Run it yourself with the data directory as its argument:

```sh
build/examples/01-first-query examples/data
```

## The data

Six books, in the order they were catalogued:

```xml
<!DOCTYPE library [
  <!ENTITY press "Harbour &amp; Daughters">
]>
<!-- A small catalogue. The records are untidy on purpose. -->
<library>
  <book id="b1" year="1937">
    <title>The <i>Art</i> of Fiction</title>
    <author>Mara Quill</author>
  </book>
  <!-- b2 was copied from a damaged index card -->
  <?catalogue status="checked"?>
  <book id="b2" year="c. 1850">
    <title>Tables &amp; <![CDATA[<Figures>]]></title>
    <author>Osric Vale</author>
  </book>
  <book id="b3" year="1999">
    <title>Three Hands</title>
    <author>Ann Lee</author>
    <author>Ben Ito</author>
    <author>Cy Moss</author>
  </book>
  <book id="b4" year="">
    <title>Caf&#xE9; Notes from &press;</title>
    <isbn/>
  </book>
  <book id="b5">
    <title>Undated</title>
    <author>Dee Roe</author>
    <isbn>0-00-000000-0</isbn>
  </book>
  ...
```

What to look out for:

- **b1** has a title with mixed content: text, an `<i>` element, more text.
- **b2** has a year that is not a number, and a title made of escaped text and a CDATA section.
- **b3** has three authors, **b4** none.
- **b4** has an empty `year=""`, an empty `<isbn/>` and an entity reference, `&press;`, declared in the internal DTD subset.
- **b5** has no `year` attribute at all.
- Comments and a processing instruction sit between the records, and every element is indented, so there is whitespace-only text between all of them.

## Parsing and the contexts

```c
    /* Substitute entities, so that &press; reads as its text */
    doc = exReadDocument(argv[1], "library.xml", XML_PARSE_NOENT);
    sctx = xp3NewStaticContext();
    dctx = xp3NewDynamicContext();
    if ((doc == NULL) || (sctx == NULL) || (dctx == NULL)) {
        failures++;
        goto done;
    }

    /* Relative paths and "/" start from the context node */
    xp3SetContextNode(dctx, (xmlNodePtr) doc);
```

`exReadDocument()` is a small helper in [`examples/common.c`](../../examples/common.c) that joins the directory and the file name and calls libxml2:

```c
    doc = xmlReadFile(path, NULL, options);
```

There are two contexts, as in the specification ([XPath 3.1, 2.1.1](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#static_context) and [2.1.2](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#eval_context)):

- the **static context** is what compiling needs: namespace prefixes, declared variables, extension functions, the language level. This page uses an empty one.
- the **dynamic context** is what evaluating needs: the context node, variable values, the clock, limits.

A compiled expression doesn't refer to the static context, so the static context can be changed or freed after compiling. Both contexts are reused for every query below.

`XML_PARSE_NOENT` matters, see [Entities](#entities-and-xml_parse_noent) below. It also makes libxml2 load external entities, so for documents from untrusted sources add `XML_PARSE_NO_XXE` (libxml2 2.13 and later).

## Compiling, evaluating, walking the result

The whole loop for one query:

```c
static int
runQuery(const xp3StaticContext *sctx, xp3DynamicContext *dctx,
         const char *query, const char *expectedError) {
    xp3Error *error = NULL;
    xp3Expr *expr;
    xp3Sequence *result;
    size_t count;
    size_t i;
    int ret;

    exPrintText(BAD_CAST query);
    printf(" -> ");

    expr = xp3Compile(sctx, BAD_CAST query, &error);
    if (expr == NULL) {
        printError(error, 1);
        ret = checkOutcome(query, error, expectedError);
        xp3ErrorFree(error);
        return(ret);
    }

    result = xp3Evaluate(expr, dctx, &error);
    /* The result doesn't need the compiled expression */
    xp3FreeExpr(expr);
    if (result == NULL) {
        printError(error, 0);
        ret = checkOutcome(query, error, expectedError);
        xp3ErrorFree(error);
        return(ret);
    }

    count = xp3SequenceLength(result);
    if (count == 0)
        printf("()");
    for (i = 0; i < count; i++) {
        if (i > 0)
            printf(", ");
        printItem(result, i);
    }
    printf("\n");

    xp3FreeSequence(result);
    return(checkOutcome(query, NULL, expectedError));
}
```

The rules this follows:

- `xp3Compile()` and `xp3Evaluate()` return NULL on error and set `*error`, which the caller frees with `xp3ErrorFree()`. On success `*error` is NULL.
- The result belongs to the caller and is freed with `xp3FreeSequence()`. It may be freed after the compiled expression; it may not outlive the document, because its nodes are libxml2's nodes.
- An empty result is a sequence of length 0, not NULL.

`checkOutcome()` compares the error code with the one the table below expects, so that the program exits with status 0 only if every query behaved as this page says. `exPrintText()` (in `common.c`) prints a string with control characters escaped, so that a newline in the data shows as `\n`.

Each item is a node or an atomic value:

```c
static void
printItem(const xp3Sequence *result, size_t index) {
    xmlChar *value;
    int quote = 0;

    if (xp3SequenceItemKind(result, index) == XP3_ITEM_NODE) {
        xmlNodePtr node = xp3SequenceNode(result, index);

        printNodeKind(node);
        if (node->type == XML_DOCUMENT_NODE)
            return;
        quote = 1;
    } else {
        xp3AtomicType type;

        type = (xp3AtomicType) xp3SequenceAtomicType(result, index);
        printf("xs:%s", (const char *) xp3AtomicTypeName(type));
        if ((type == XP3_TYPE_UNTYPED_ATOMIC) || (type == XP3_TYPE_ANY_URI) ||
            (xp3AtomicTypeDerivesFrom(type, XP3_TYPE_STRING)))
            quote = 1;
    }

    /* The string value of a node, or an atomic value cast to a string */
    value = xp3SequenceStringValue(result, index);
    if (value == NULL)
        return;
    printf(" ");
    if (quote)
        printf("\"");
    exPrintText(value);
    if (quote)
        printf("\"");
    xmlFree(value);
}
```

A node is a plain `xmlNodePtr` into your document. An atomic value has a type, such as `XP3_TYPE_INTEGER`, and `xp3SequenceStringValue()` gives any item as a string, which you free with `xmlFree()`. [Tutorial 3](03-numbers.md) reads numbers as C numbers.

`printNodeKind()` prints the kind of node the way XPath writes kind tests. One libxml2 detail shows up there already:

```c
        case XML_TEXT_NODE:
        case XML_CDATA_SECTION_NODE:
            printf("text()");
            break;
```

XPath has one kind of text node; libxml2 has two node types for it.

Errors carry a code, a message and, for errors in the expression itself, a position:

```c
static void
printError(const xp3Error *error, int compiling) {
    printf("ERROR %s", (const char *) xp3ErrorCode(error));
    if (compiling) {
        printf(" at line %d, column %d when compiling",
               xp3ErrorLine(error), xp3ErrorColumn(error));
    }
    if (xp3ErrorMessage(error) != NULL) {
        printf(": ");
        exPrintText(xp3ErrorMessage(error));
    }
    printf("\n");
}
```

The codes are the W3C codes, such as `XPTY0004`, plus `NO_MEMORY`, `LIMIT` and `UNSUPPORTED` for libxpath3's own conditions. The codes are stable; the messages may be reworded between versions, so don't match on them.

## The queries

The program runs a table of queries, each with the error it is expected to raise:

```c
static const Query queries[] = {
    { "count(//book)", NULL },
    /* [1] applies to each step: the first author of each book */
    { "//author[1]", NULL },
    { "(//author)[1]", NULL },
```

```text
count(//book) -> xs:integer 6
//author[1] -> element(author) "Mara Quill", element(author) "Osric Vale", element(author) "Ann Lee", element(author) "Dee Roe", element(author) "Eve Ng"
(//author)[1] -> element(author) "Mara Quill"
```

**Why:** `//author[1]` is short for `/descendant-or-self::node()/child::author[1]`, and the predicate belongs to the `child::author` step, so it picks the first author child of every node ([XPath 3.1, 3.3.5](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#abbrev)). The parentheses make the predicate apply to the whole sequence instead. This is the same in XPath 1.0, and still the most common surprise.

### Mixed content, CDATA and entities

```c
    /* Mixed content: the title has an <i> child between two texts */
    { "//book[1]/title", NULL },
    { "//book[1]/title/text()", NULL },
    { "string(//book[1]/title)", NULL },
    /* Text with an escaped & and a CDATA section make one text node */
    { "//book[@id = 'b2']/title/text()", NULL },
    { "//book[title = 'Tables & <Figures>']/string(@id)", NULL },
    { "string(//book[@id = 'b4']/title)", NULL },
```

```text
//book[1]/title -> element(title) "The Art of Fiction"
//book[1]/title/text() -> text() "The ", text() " of Fiction"
string(//book[1]/title) -> xs:string "The Art of Fiction"
//book[@id = 'b2']/title/text() -> text() "Tables & <Figures>"
//book[title = 'Tables & <Figures>']/string(@id) -> xs:string "b2"
string(//book[@id = 'b4']/title) -> xs:string "Café Notes from Harbour & Daughters"
```

**Why:** `text()` selects text node children, and the `<i>` element splits the title's text in two, so `title/text()` misses "Art". The string value of an element is the concatenation of all its descendant text ([XPath 3.1, 2.5.3](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#id-typed-value)), which is what you want for comparing or printing. In the data model there are no CDATA sections and no adjacent text nodes, so `Tables &amp; <![CDATA[<Figures>]]>` is a single text node, and comparing the title with `'Tables & <Figures>'` works.

libxml2 keeps more detail than the data model. The program asks libxml2 what is behind the text node it got:

```c
    /* The node stands for a run of adjacent libxml2 text nodes */
    printf("libxml2 nodes from there ->");
    node = xp3SequenceNode(result, 0);
    while ((node != NULL) && ((node->type == XML_TEXT_NODE) ||
                              (node->type == XML_CDATA_SECTION_NODE))) {
```

```text
string value of the XPath text node -> "Tables & <Figures>"
libxml2 nodes from there -> text "Tables & " CDATA "<Figures>"
```

`xp3SequenceNode()` returns the first libxml2 node of the run, and its `content` is only "Tables & ". Read the text of a node from a result with `xp3SequenceStringValue()`, not from `node->content`.

### Entities and XML_PARSE_NOENT

Without `XML_PARSE_NOENT`, libxml2 keeps `&press;` as an entity reference node, whose content belongs to the entity declaration, not to the place where it is used. libxpath3 skips such nodes (`doc/DESIGN.md` 4.5), so the text of the entity is missing. The program parses the document a second time without the option to show it:

```c
    /* libxml2's default keeps &press; as an entity reference node */
    rawDoc = exReadDocument(argv[1], "library.xml", 0);
```

```text
Without XML_PARSE_NOENT:
string(//book[@id = 'b4']/title) -> xs:string "Café Notes from "
```

The character reference `&#xE9;` and the predefined `&amp;` are always replaced by libxml2; only entities declared in a DTD are affected. Saxon, whose parser always substitutes entities, gives the first result. libxml2's own XPath 1.0 engine includes the entity text even without the option, so code moving from `xmlXPathEval()` to libxpath3 should parse with `XML_PARSE_NOENT`.

### Whitespace, comments and processing instructions

```c
    /* The indentation between elements is text too */
    { "count(//book/text())", NULL },
    { "//book[1]/text()", NULL },
    { "//comment()", NULL },
    { "//processing-instruction()", NULL },
```

```text
count(//book/text()) -> xs:integer 21
//book[1]/text() -> text() "\n    ", text() "\n    ", text() "\n  "
//comment() -> comment() " A small catalogue. The records are untidy on purpose. ", comment() " b2 was copied from a damaged index card "
//processing-instruction() -> processing-instruction(catalogue) "status="checked""
```

**Why:** the indentation is character data like any other, so each book has a whitespace-only text node before, between and after its child elements. Position-based steps such as `node()[2]` count them. libxml2's `XML_PARSE_NOBLANKS` drops them while parsing, but it guesses which whitespace is insignificant; it is usually simpler to select elements with `*` and leave the whitespace alone. The comment before `<library>` is a child of the document node, which `//` includes.

### A year that is not a number

```c
    /* "c. 1850" is not a number (XPath 3.1, 3.7.2) */
    { "//book[@year > 1900]/string(@id)", "FORG0001" },
    { "//book[@year castable as xs:integer]"
      "[xs:integer(@year) > 1900]/string(@id)", NULL },
    /* A quoted number is a string, so this compares strings */
    { "//book[@year > '1990']/string(@id)", NULL },
    { "//book[not(@year)]/string(@id)", NULL },
    { "//book[@year = '']/string(@id)", NULL },
```

```text
//book[@year > 1900]/string(@id) -> ERROR FORG0001: the untyped value "c. 1850" is not a valid xs:double
//book[@year castable as xs:integer][xs:integer(@year) > 1900]/string(@id) -> xs:string "b1", xs:string "b3", xs:string "b6"
//book[@year > '1990']/string(@id) -> xs:string "b2", xs:string "b3", xs:string "b6"
//book[not(@year)]/string(@id) -> xs:string "b5"
//book[@year = '']/string(@id) -> xs:string "b4"
```

**Why:** without a schema, an attribute's value is `xs:untypedAtomic`. In a general comparison with a number, an untyped value is cast to `xs:double` ([XPath 3.1, 3.7.2](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#id-general-comparisons)), and casting `"c. 1850"`, or the empty `""` of b4, raises `FORG0001`. XPath 1.0 turned such values into NaN and carried on; XPath 2.0 and later stop. The error ends the whole evaluation, not just the one book.

The fix is to test first with `castable as` ([3.14.3](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#id-castable)) in a predicate of its own, so the cast in the second predicate only sees values that pass. `number(@year) > 1900` also works, because `fn:number` returns NaN instead of raising an error, and NaN compares false.

Comparing with `'1990'` instead of `1990` swaps the problem for a quieter one: with a string on one side, the untyped value is compared as a string, so "c. 1850" is greater than "1990" because "c" comes after "1". A missing attribute is the empty sequence, which makes any general comparison false, so b5 never fails; `@year = ''` finds the empty one.

### Three authors where one is expected

```c
    /* Three authors where one string is expected (XPath 3.1, 3.1.5.2) */
    { "string-length(//book[3]/author)", "XPTY0004" },
    { "//book[3]/author/string-length()", NULL },
    { "string-join(//book[3]/author, '; ')", NULL },
```

```text
string-length(//book[3]/author) -> ERROR XPTY0004: argument 1 of string-length#1 is a sequence of more than one item, where xs:string? is expected
//book[3]/author/string-length() -> xs:integer 7, xs:integer 7, xs:integer 7
string-join(//book[3]/author, '; ') -> xs:string "Ann Lee; Ben Ito; Cy Moss"
```

**Why:** `fn:string-length` takes `xs:string?`, at most one string. The function conversion rules ([XPath 3.1, 3.1.5.2](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#id-function-conversion-rules)) atomize the three author elements into three values, and three values don't fit `?`, so the call is a type error. XPath 1.0 silently took the first node. Either apply the function to each author with a path step, or join the authors first.

### Empty and missing elements

```c
    /* An empty element, a missing one */
    { "//book[not(author)]/string(@id)", NULL },
    { "//book[isbn]/string(@id)", NULL },
    { "//book[isbn = '']/string(@id)", NULL },
    { "//book[1]/isbn", NULL },
    { "string(//book[1]/isbn)", NULL },
```

```text
//book[not(author)]/string(@id) -> xs:string "b4"
//book[isbn]/string(@id) -> xs:string "b4", xs:string "b5"
//book[isbn = '']/string(@id) -> xs:string "b4"
//book[1]/isbn -> ()
string(//book[1]/isbn) -> xs:string ""
```

**Why:** `[isbn]` tests whether the element exists, so b4's empty `<isbn/>` counts. `isbn = ''` compares its string value, which is empty for b4 and absent for books without an `isbn`. A path that selects nothing is the empty sequence, not an error, and `fn:string` of the empty sequence is the empty string ([F&O 3.1, 2.3](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#func-string)), so `string()` can't tell an empty element from a missing one; `exists()` and `empty()` can.

### Mixed results

```c
    /* Nodes and atomic values in one result */
    { "(//book[1]/title, count(//book[3]/author), 'done')", NULL },
    /* ... but not as the result of a path (XPath 3.1, 3.3.1.1) */
    { "//book[1]/(title, 1)", "XPTY0018" },
    { "//book[@year = '1066']", NULL },
```

```text
(//book[1]/title, count(//book[3]/author), 'done') -> element(title) "The Art of Fiction", xs:integer 3, xs:string "done"
//book[1]/(title, 1) -> ERROR XPTY0018: the last step of a path returns both nodes and other items
//book[@year = '1066'] -> ()
```

**Why:** a sequence may hold nodes and atomic values together ([XPath 3.1, 3.4.1](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#construct_seq)), so a program that walks results has to check `xp3SequenceItemKind()` for every item, as `printItem()` does. The last step of a path is different: its result must be all nodes, which are then put in document order, or all atomic values ([XPath 3.1, 3.3.1.1](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#id-path-operator)), so mixing them is `XPTY0018`. A query that matches nothing returns the empty sequence.

### A syntax error

```c
    { "//book[\n  @year >= ]", "XPST0003" },
    { "count(//book)", NULL }
};
```

```text
//book[\n  @year >= ] -> ERROR XPST0003 at line 2, column 12 when compiling: expected an expression, found ']'
count(//book) -> xs:integer 6
```

**Why:** syntax errors are static errors, raised by `xp3Compile()` before anything is evaluated ([XPath 3.1, 2.3.1](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#id-kinds-of-errors)). The position is 1-based, in lines and characters of the expression; the specification leaves positions to the implementation. The last query shows that after all these errors the same two contexts still work: an error never leaves a context in a broken state.

## Cleaning up

```c
done:
    xp3FreeDynamicContext(dctx);
    xp3FreeStaticContext(sctx);
    xmlFreeDoc(rawDoc);
    xmlFreeDoc(doc);
```

Every free function accepts NULL, so one exit path serves the error cases too. Free the documents last: results and the dynamic context may point into them.

Next: [2. Variables, namespaces, reuse](02-namespaces-variables.md).
