# 6. Extension functions

An application can add its own functions, written in C and called from XPath like the built-in ones. This page registers a handful over the inventory of [tutorial 3](03-numbers.md), whose prices and quantities are as messy as before, and looks at everything that happens between the call in the expression and the C code: conversions, type errors, errors of your own, constant folding and user data.

You will learn:

- how to register a function with a signature, and what the library checks before your code runs
- how an implementation reads its arguments, returns a result and raises an error
- what `XP3_FUNCTION_DETERMINISTIC` changes, and why it is not the default
- how to pass per-registration and per-evaluation data to a function

The full program is [`examples/06-extension-functions.c`](../../examples/06-extension-functions.c), its document [`examples/data/inventory.xml`](../../examples/data/inventory.xml), and its output [`examples/expected/06-extension-functions.txt`](../../examples/expected/06-extension-functions.txt).

## Registering

A function is registered in the static context with a signature written as the specifications write them, a set of flags, the C implementation and a pointer of your own:

```c
static const Registration registrations[] = {
    { "inv:parse-price($raw as xs:string?) as xs:decimal?",
      0, parsePrice, NULL },
    { "inv:restock($qty as xs:integer) as xs:integer",
      XP3_FUNCTION_DETERMINISTIC, restock, NULL },
    { "inv:restock($qty as xs:integer, $minimum as xs:integer)"
      " as xs:integer",
      XP3_FUNCTION_DETERMINISTIC, restock, NULL },
    { "inv:decimal-places($amount as xs:decimal) as xs:integer",
      XP3_FUNCTION_DETERMINISTIC, decimalPlaces, NULL },
    { "inv:stock($sku as xs:string) as xs:integer",
      0, stock, NULL },
    { "inv:broken() as xs:integer", 0, brokenResult, NULL },
    { "inv:untyped() as xs:integer", 0, untypedResult, NULL },
    /* A namespace written out, without a prefix for it */
    { "Q{" LABEL_NS "}tag($sku as xs:string) as xs:string",
      XP3_FUNCTION_DETERMINISTIC, tag, NULL },
    /* Mistakes in signatures are reported when registering */
    { "stock:level($sku as xs:string)", 0, stock, "XPST0081" },
    { "inv:age($days as xs:days)", 0, stock, "XPST0051" },
    { "inv:stock($code as xs:string)", 0, stock, "XQST0034" }
};
```

```c
    if (xp3DeclareNamespace(sctx, BAD_CAST "inv", BAD_CAST INV_NS) < 0) {
        failures++;
        goto done;
    }
    for (i = 0; i < sizeof(registrations) / sizeof(registrations[0]); i++)
        failures += registerFunction(sctx, registrations[i].signature,
                                     registrations[i].flags,
                                     registrations[i].impl, &calls,
                                     registrations[i].expectedError);
```

```text
register stock:level($sku as xs:string) -> ERROR XPST0081
register inv:age($days as xs:days) -> ERROR XPST0051
register inv:stock($code as xs:string) -> ERROR XQST0034
```

The rules for signatures:

- The name must be in a namespace, and the prefix must be declared in the static context *before* registering, `XPST0081` otherwise. `Q{uri}name` needs no prefix at all; the expressions then call it the same way, `Q{urn:example:labels}tag('A1')`.
- Parameter and result types are sequence types: an atomic type with `?`, `*` or `+`, `item()*`, `node()` and so on. A type that doesn't exist is `XPST0051`.
- One name may have several arities, here `inv:restock#1` and `inv:restock#2`, each registered once; registering a name and arity again is `XQST0034`, the code XQuery uses for a duplicate function declaration.
- The parameter names (`$raw`) only appear in messages.

`&calls` is the registration's user data. Every function here counts its calls in it, which shows below exactly when the C code ran.

## Implementing

An implementation gets the call, the arguments and the user data. Here is `inv:parse-price`, which reads a price as people type it:

```c
static xp3Sequence *
parsePrice(xp3FunctionCall *call, xp3Sequence **args, int count,
           void *userData) {
    Calls *calls = userData;
    xp3Sequence *result;
    xp3Error *error = NULL;
    xmlChar *raw;
    xmlChar *clean;
    const xmlChar *cur;
    size_t length = 0;

    (void) count;
    calls->parsePrice++;

    /* xs:string? : the empty sequence or one string */
    if (xp3SequenceLength(args[0]) == 0)
        return(emptySequence());
    raw = xp3SequenceStringValue(args[0], 0);
    if (raw == NULL)
        return(NULL);
```

and, after removing spaces and commas into `clean`:

```c
    if (length == 0) {
        result = emptySequence();
    } else {
        const xmlChar *lexical = clean;

        result = xp3NewAtomicSequence(XP3_TYPE_DECIMAL, &lexical, 1, &error);
        if ((result == NULL) && (error != NULL) &&
            (!xmlStrEqual(xp3ErrorCode(error), BAD_CAST "NO_MEMORY")))
            xp3FunctionError(call, BAD_CAST INV_NS, BAD_CAST "BADPRICE",
                             BAD_CAST "not a decimal number");
        xp3ErrorFree(error);
    }
```

The contract ([`xpath3.h`](../../include/libxpath3/xpath3.h), `xp3FunctionImpl`):

- The arguments are borrowed and already converted to the declared types (below). `count` is the arity of the signature, so one implementation can serve several arities.
- The result is a new sequence, which the library owns from then on. `xp3NewAtomicSequence()` makes a value of any atomic type from its lexical form, here an `xs:decimal`, which C has no type for. Any constructor with no values makes the empty sequence.
- An error is raised with `xp3FunctionError()`, followed by returning NULL. The code is a QName: a namespace of your own and a local name. NULL without an error means a failed allocation, `NO_MEMORY`, which is why the code passes an out-of-memory error from `xp3NewAtomicSequence()` on by returning NULL without raising anything.

## Calling

```c
static const Query queries[] = {
    /* An optional parameter, given the empty sequence */
    { "inv:parse-price(())", NULL },
    { "inv:parse-price(//item[@sku = 'A6']/@price)", NULL },
    { "//item[@sku = ('A1', 'A11')] ! inv:parse-price(@price)", NULL },
    { "//item ! inv:parse-price(@price)", "BADPRICE" },
```

```text
inv:parse-price(()) -> ()
inv:parse-price(//item[@sku = 'A6']/@price) -> ()
//item[@sku = ('A1', 'A11')] ! inv:parse-price(@price) -> xs:decimal 12.5, xs:decimal 1250
//item ! inv:parse-price(@price) -> ERROR Q{urn:example:inventory}BADPRICE: not a decimal number
```

The missing price of A6 is the empty sequence, which `xs:string?` accepts. `!` calls the function once per item, and the first price it can't read, the `1e3` of A2, ends the evaluation with the function's own error, its namespace, code and message as the C code gave them.

### Conversions before the C code runs

```c
    /* Converted before the C code runs: untyped values are cast */
    { "inv:decimal-places(//item[@sku = 'A1']/@price)", NULL },
    { "inv:decimal-places(//item[@sku = 'A7']/@price)", NULL },
    { "inv:decimal-places(12)", NULL },
    { "inv:decimal-places(//item[@sku = 'A2']/@price)", "FORG0001" },
    { "inv:decimal-places(//item[@sku = 'A6']/@price)", "XPTY0004" },
    { "inv:decimal-places('12.50')", "XPTY0004" },
    { "inv:decimal-places(1.5e0)", "XPTY0004" },
```

```text
inv:decimal-places(//item[@sku = 'A1']/@price) -> xs:integer 1
inv:decimal-places(//item[@sku = 'A7']/@price) -> xs:integer 30
inv:decimal-places(12) -> xs:integer 0
inv:decimal-places(//item[@sku = 'A2']/@price) -> ERROR FORG0001
inv:decimal-places(//item[@sku = 'A6']/@price) -> ERROR XPTY0004
inv:decimal-places('12.50') -> ERROR XPTY0004
inv:decimal-places(1.5e0) -> ERROR XPTY0004
```

`decimalPlaces()` counts the digits after the point in the string value of its argument:

```c
    text = xp3SequenceStringValue(args[0], 0);
    if (text == NULL)
        return(NULL);
    point = xmlStrchr(text, '.');
    if (point != NULL)
        places = xmlStrlen(point + 1);
```

**Why:** arguments go through the function conversion rules, as for built-in functions ([XPath 3.1, 3.1.5.2](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#id-function-conversion-rules)). The attribute is atomized to an untyped value and cast to the declared `xs:decimal`: `" 12.50 "` becomes 12.5, so the C code sees `12.5` and counts one place, not the two that were typed. A value that doesn't cast, `1e3`, fails with the cast's `FORG0001`. An integer is a decimal and passes as it is. A string literal is not cast, because only untyped values are, so `'12.50'` is a type error; neither is a double, which can be promoted to nothing but itself. A missing attribute is the empty sequence, which `xs:decimal` without `?` doesn't allow. The counter below shows that none of these errors reached the C code.

### Two arities, and arguments that don't fit

```c
    /* Two arities of one name, and a third that doesn't exist */
    { "inv:restock(3)", NULL },
    { "inv:restock(3, 10)", NULL },
    { "inv:restock(1, 2, 3)", "XPST0017" },
    { "inv:restock(//item/@qty)", "XPTY0004" },
    /* Once per item, quantities beyond 64 bits included */
    { "//item ! inv:restock(@qty)", NULL },
```

```text
inv:restock(3) -> xs:integer 2
inv:restock(3, 10) -> xs:integer 7
inv:restock(1, 2, 3) -> ERROR XPST0017 when compiling
inv:restock(//item/@qty) -> ERROR XPTY0004
//item ! inv:restock(@qty) -> xs:integer 2, xs:integer 4, xs:integer 5, xs:integer 3, xs:integer 4, xs:integer 0, xs:integer 4, xs:integer 0, xs:integer 1, xs:integer 1, xs:integer 4
```

A call with an arity nobody registered is a static error, `XPST0017`, raised when compiling ([XPath 3.1, 3.1.5](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#id-function-calls)). Eleven quantities where one is declared is a type error before the call.

The declared `xs:integer` has no size limit, and A8's quantity has 40 digits, so the C code can't assume an `int64_t`:

```c
    /* Integers have no size limit, so the C code checks */
    fits = xp3SequenceInteger(args[0], 0, &quantity);
    if (fits == 1) {
        xmlChar *text = xp3SequenceStringValue(args[0], 0);
        int negative;

        if (text == NULL)
            return(NULL);
        negative = (text[0] == '-');
        xmlFree(text);
```

A positive quantity that large needs no restocking, so the result for A8 is 0; a negative one raises an error of the function's own.

### Results are checked too

```c
    /* Results are converted and checked like arguments */
    { "inv:broken()", "XPTY0004" },
    { "inv:untyped()", NULL },
    { "Q{" LABEL_NS "}tag('A1')", NULL },
    { "inv:stock('A1')", "NOWAREHOUSE" }
};
```

```text
inv:broken() -> ERROR XPTY0004
inv:untyped() -> xs:integer 7
Q{urn:example:labels}tag('A1') -> xs:string "SKU-A1"
inv:stock('A1') -> ERROR Q{urn:example:inventory}NOWAREHOUSE: the evaluation has no warehouse
calls of inv:parse-price -> 6
calls of inv:decimal-places -> 3
calls of inv:restock -> 13
```

`inv:broken()` is declared `as xs:integer` and returns the string "five"; the library applies the same conversion rules to the result as to arguments, as XQuery does for the result of a function it declares, and a string is not an integer: `XPTY0004`. `inv:untyped()` returns the untyped value "7", which is cast to the integer 7, as an untyped argument would be. A C function can't return something its signature doesn't allow.

The counters: `inv:parse-price` ran 6 times, once per call and per item until the error. `inv:decimal-places` ran 3 times for 7 calls, because the 4 errors came from the conversions. `inv:restock` ran 13 times: once each for the two literal calls, 11 times over the items, never for the two calls that failed before it.

## Deterministic or not

Without flags, a function is taken to be nondeterministic, and every call runs it. With `XP3_FUNCTION_DETERMINISTIC`, a call whose arguments are all constants may be evaluated once, while compiling ([F&O 3.1, 1.7.4](https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/#properties-of-functions) defines the property). The program registers the same C function in two static contexts, once with the flag and once without, and compiles the same expression in both:

```c
    failures += registerFunction(once, signature,
                                 XP3_FUNCTION_DETERMINISTIC, unitsIn,
                                 &onceCalls, NULL);
    failures += registerFunction(always, signature, 0, unitsIn,
                                 &alwaysCalls, NULL);
```

```c
    expr = xp3Compile(sctx, BAD_CAST query, NULL);
    if (expr == NULL)
        return(1);
    printf("calls after compiling %s -> %d\n", query, *counter);
    failures += exEvaluate(expr, dctx, "first evaluation", expectedError);
    failures += exEvaluate(expr, dctx, "second evaluation", expectedError);
    printf("calls after evaluating twice -> %d\n", *counter);
```

```text
Deterministic:
calls after compiling inv:units-in('gross') * 2 -> 1
first evaluation -> xs:integer 288
second evaluation -> xs:integer 288
calls after evaluating twice -> 1
Not deterministic:
calls after compiling inv:units-in('gross') * 2 -> 0
first evaluation -> xs:integer 288
second evaluation -> xs:integer 288
calls after evaluating twice -> 2
Deterministic, raising an error:
calls after compiling inv:units-in('crate') -> 1
first evaluation -> ERROR Q{urn:example:inventory}UNKNOWNPACK: no such pack
second evaluation -> ERROR Q{urn:example:inventory}UNKNOWNPACK: no such pack
calls after evaluating twice -> 3
```

With the flag, `xp3Compile()` called the function, and the evaluations used the folded constant. Without it, each evaluation called it. When the folded call raises an error, the error is not raised by compiling, since the expression might never be evaluated, or only on a branch that skips the call ([XPath 3.1, 2.3.4](https://www.w3.org/TR/2017/REC-xpath-31-20170321/#id-errors-and-opt)); it is dropped, and the call runs and fails at each evaluation.

So a function registered as deterministic must really be one: no clock, no file, no counter, no global state, no dependence on the dynamic context's user data, which a folded call doesn't have. That is why the flag is opt-in (`doc/DESIGN.md` 6.3).

## User data per evaluation

The registration's user data is fixed. For data that changes per evaluation, such as the request an expression is evaluated for, the dynamic context has a pointer of its own, which the function reads with `xp3FunctionUserData()`:

```c
    warehouse = xp3FunctionUserData(call);
    if (warehouse == NULL) {
        xp3FunctionError(call, BAD_CAST INV_NS, BAD_CAST "NOWAREHOUSE",
                         BAD_CAST "the evaluation has no warehouse");
        return(NULL);
    }
```

```c
    xp3SetDynamicContextUserData(dctx, &north);
    failures += exEvaluate(expr, dctx, "low stock in the north", NULL);
    xp3SetDynamicContextUserData(dctx, &south);
    failures += exEvaluate(expr, dctx, "low stock in the south", NULL);
    xp3SetDynamicContextUserData(dctx, NULL);
```

```text
low stock in the north -> xs:string "A2", xs:string "A9"
low stock in the south -> xs:string "A1"
calls of inv:stock -> 7
```

One compiled expression, two warehouses, two answers. The earlier call of `inv:stock('A1')` without any user data shows why the function checks for NULL.

Next: [7. Untrusted expressions](07-untrusted-expressions.md).
