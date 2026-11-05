/*
 * testcontext.c: tests for the static context
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdio.h>
#include <string.h>

#include <libxml/parser.h>

#include <libxpath3/xpath3.h>
#include "private/xp3context.h"
#include "private/xp3error.h"
#include "private/xp3func.h"
#include "private/xp3static.h"
#include "testalloc.h"

static int failures = 0;

static void
check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static int
strEq(const xmlChar *a, const char *b) {
    if ((a == NULL) || (b == NULL))
        return((a == NULL) && (b == NULL));
    return(strcmp((const char *) a, b) == 0);
}

static void
testDefaults(void) {
    xp3StaticContext *ctx = xp3NewStaticContext();

    check(ctx != NULL, "context created");
    if (ctx == NULL)
        return;
    check(ctx->level == XP3_XPATH_31, "default language level is 3.1");
    check(ctx->maxExprLength == XP3_DEFAULT_MAX_EXPR_LENGTH,
          "default expression length limit");
    check(ctx->maxParseDepth == XP3_DEFAULT_MAX_PARSE_DEPTH,
          "default parse depth limit");

    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "xml"), XP3_NS_XML),
          "xml predeclared");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "xs"), XP3_NS_XS),
          "xs predeclared");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "xsi"), XP3_NS_XSI),
          "xsi predeclared");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "fn"), XP3_NS_FN),
          "fn predeclared");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "math"), XP3_NS_MATH),
          "math predeclared");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "map"), XP3_NS_MAP),
          "map predeclared");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "array"), XP3_NS_ARRAY),
          "array predeclared");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "err"), XP3_ERR_NS),
          "err predeclared");
    check(xp3StaticContextLookupNs(ctx, BAD_CAST "foo") == NULL,
          "unknown prefix is unbound");

    xp3FreeStaticContext(ctx);
    xp3FreeStaticContext(NULL);    /* must be ignored */
}

static void
testLanguageLevel(void) {
    xp3StaticContext *ctx = xp3NewStaticContext();

    check(xp3SetStaticContextLanguageLevel(ctx, XP3_XPATH_20) == 0,
          "level 2.0 accepted");
    check(ctx->level == XP3_XPATH_20, "level 2.0 stored");
    check(xp3SetStaticContextLanguageLevel(ctx, XP3_XPATH_30) == 0,
          "level 3.0 accepted");
    check(xp3SetStaticContextLanguageLevel(ctx, XP3_XPATH_31) == 0,
          "level 3.1 accepted");

    check(xp3SetStaticContextLanguageLevel(ctx, (xp3LanguageLevel) 0) < 0,
          "level 0 rejected");
    check(xp3SetStaticContextLanguageLevel(ctx, (xp3LanguageLevel) 21) < 0,
          "level 21 rejected");
    check(xp3SetStaticContextLanguageLevel(ctx, (xp3LanguageLevel) 40) < 0,
          "level 40 rejected");
    check(ctx->level == XP3_XPATH_31, "rejected level leaves level unchanged");

    check(xp3SetStaticContextLanguageLevel(NULL, XP3_XPATH_31) < 0,
          "NULL context rejected");

    xp3FreeStaticContext(ctx);
}

static void
testDeclareNamespace(void) {
    xp3StaticContext *ctx = xp3NewStaticContext();

    check(xp3DeclareNamespace(ctx, BAD_CAST "ex", BAD_CAST "urn:example") == 0,
          "declare new prefix");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "ex"), "urn:example"),
          "new prefix bound");

    check(xp3DeclareNamespace(ctx, BAD_CAST "ex", BAD_CAST "urn:other") == 0,
          "override prefix");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "ex"), "urn:other"),
          "override replaces the binding");

    check(xp3DeclareNamespace(ctx, BAD_CAST "fn", BAD_CAST "urn:mine") == 0,
          "predeclared prefix can be rebound");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "fn"), "urn:mine"),
          "predeclared prefix rebound");

    check(xp3DeclareNamespace(ctx, BAD_CAST "ex", BAD_CAST "") == 0,
          "empty URI removes binding");
    check(xp3StaticContextLookupNs(ctx, BAD_CAST "ex") == NULL,
          "binding removed");
    check(xp3DeclareNamespace(ctx, BAD_CAST "ex", NULL) == 0,
          "removing an unbound prefix succeeds");

    check(xp3DeclareNamespace(ctx, BAD_CAST "xml", BAD_CAST XP3_NS_XML) == 0,
          "xml bound to its own namespace is accepted");
    check(xp3DeclareNamespace(ctx, BAD_CAST "xml", BAD_CAST "urn:x") < 0,
          "xml bound elsewhere is rejected");
    check(xp3DeclareNamespace(ctx, BAD_CAST "xml", NULL) < 0,
          "xml cannot be removed");
    check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "xml"), XP3_NS_XML),
          "xml binding unchanged");
    check(xp3DeclareNamespace(ctx, BAD_CAST "xmlns", BAD_CAST "urn:x") < 0,
          "xmlns rejected");
    check(xp3DeclareNamespace(ctx, BAD_CAST "", BAD_CAST "urn:x") < 0,
          "empty prefix rejected");
    check(xp3DeclareNamespace(ctx, BAD_CAST "1abc", BAD_CAST "urn:x") < 0,
          "invalid NCName rejected");
    check(xp3DeclareNamespace(ctx, BAD_CAST "a:b", BAD_CAST "urn:x") < 0,
          "prefix with colon rejected");

    check(xp3DeclareNamespace(NULL, BAD_CAST "p", BAD_CAST "urn:x") < 0,
          "NULL context rejected");
    check(xp3DeclareNamespace(ctx, NULL, BAD_CAST "urn:x") < 0,
          "NULL prefix rejected");
    check(xmlStrEqual(xp3StaticContextLookupNs(NULL, BAD_CAST "xs"),
                      BAD_CAST XP3_NS_XS),
          "lookup with NULL context uses the predeclared prefixes");
    check(xp3StaticContextLookupNs(ctx, NULL) == NULL,
          "lookup with NULL prefix");

    xp3FreeStaticContext(ctx);
}

/*
 * Extension functions
 */

#define EX_NS "urn:example"

/* Registration never calls the implementation */
static xp3Sequence *
neverCalled(xp3FunctionCall *call, xp3Sequence **args, int count,
            void *userData) {
    (void) call;
    (void) args;
    (void) count;
    (void) userData;
    return(NULL);
}

/* A context with the prefix ex bound */
static xp3StaticContext *
exampleContext(void) {
    xp3StaticContext *ctx = xp3NewStaticContext();

    if ((ctx != NULL) &&
        (xp3DeclareNamespace(ctx, BAD_CAST "ex", BAD_CAST EX_NS) < 0)) {
        xp3FreeStaticContext(ctx);
        ctx = NULL;
    }
    return(ctx);
}

/* The registered entry of a name and arity, or NULL */
static const xp3ExtFunction *
findFunction(const xp3StaticContext *ctx, const char *uri, const char *name,
             int arity) {
    const xp3ExtFunction *func;

    for (func = xp3StaticContextFunctions(ctx, BAD_CAST uri, BAD_CAST name);
         func != NULL; func = func->next) {
        if (func->info.arity == arity)
            return(func);
    }
    return(NULL);
}

static int
typeIs(const xp3FuncType *type, xp3AstKind kind, int value,
       xp3Occurrence occurrence) {
    return((type->kind == kind) && (type->type == value) &&
           (type->occurrence == occurrence));
}

static void
testRegisterFunction(void) {
    xp3StaticContext *ctx = exampleContext();
    const xp3ExtFunction *func;
    xp3Error *err = NULL;
    int data = 0;

    if (ctx == NULL) {
        check(0, "context for registering");
        return;
    }

    check(xp3RegisterFunction(ctx, BAD_CAST "ex:f($a as xs:integer, "
                              "$b as node()?) as xs:string*", 0,
                              neverCalled, &data, &err) == 0 &&
          err == NULL, "a function is registered");
    func = findFunction(ctx, EX_NS, "f", 2);
    check(func != NULL, "the registered arity is found by its name");
    if (func != NULL) {
        check(strEq(func->uri, EX_NS) && (strcmp(func->info.name, "f") == 0),
              "the entry has the resolved name");
        check(strcmp(func->info.signature, "ex:f($a as xs:integer, "
                     "$b as node()?) as xs:string*") == 0,
              "the entry keeps the signature text");
        check(typeIs(&func->info.params[0], XP3_AST_ATOMIC_TYPE,
                     XP3_TYPE_INTEGER, XP3_OCC_ONE) &&
              typeIs(&func->info.params[1], XP3_AST_KIND_TEST, XP3_KIND_ANY,
                     XP3_OCC_OPTIONAL),
              "parameter types are xp3FuncType data");
        check(typeIs(&func->info.result, XP3_AST_ATOMIC_TYPE,
                     XP3_TYPE_STRING, XP3_OCC_ZERO_OR_MORE),
              "the result type is xp3FuncType data");
        check((func->impl == neverCalled) && (func->userData == &data),
              "the entry has the implementation and user data");
        check((func->info.flags & XP3_FUNC_EXTENSION) &&
              (func->info.flags & XP3_FUNC_NONDETERMINISTIC),
              "without flags a function is nondeterministic");
        check(func->info.level == XP3_XPATH_20,
              "an extension function exists at every level");
    }

    check(xp3RegisterFunction(ctx, BAD_CAST "ex:f()",
                              XP3_FUNCTION_DETERMINISTIC, neverCalled, NULL,
                              &err) == 0,
          "another arity of the same name is registered");
    func = findFunction(ctx, EX_NS, "f", 0);
    check((func != NULL) && (func->info.params == NULL) &&
          typeIs(&func->info.result, XP3_AST_ANY_ITEM_TYPE, 0,
                 XP3_OCC_ZERO_OR_MORE) &&
          !(func->info.flags & XP3_FUNC_NONDETERMINISTIC),
          "a deterministic function without parameters returns item()*");
    check(findFunction(ctx, EX_NS, "f", 2) != NULL,
          "the first arity stays registered");

    check(xp3RegisterFunction(ctx, BAD_CAST "Q{urn:other}g($a, $b)", 0,
                              neverCalled, NULL, NULL) == 0,
          "a Q{} name is registered without an error argument");
    func = findFunction(ctx, "urn:other", "g", 2);
    check((func != NULL) &&
          typeIs(&func->info.params[0], XP3_AST_ANY_ITEM_TYPE, 0,
                 XP3_OCC_ZERO_OR_MORE) &&
          typeIs(&func->info.params[1], XP3_AST_ANY_ITEM_TYPE, 0,
                 XP3_OCC_ZERO_OR_MORE),
          "a parameter without a type is item()*");

    /* Invalid arguments: -1, and no error object */
    err = xp3ErrorNoMemory();
    check(xp3RegisterFunction(NULL, BAD_CAST "ex:h()", 0, neverCalled, NULL,
                              &err) < 0 && err == NULL, "NULL context");
    check(xp3RegisterFunction(ctx, NULL, 0, neverCalled, NULL, &err) < 0 &&
          err == NULL, "NULL signature");
    check(xp3RegisterFunction(ctx, BAD_CAST "ex:h()", 0, NULL, NULL,
                              &err) < 0 && err == NULL, "NULL implementation");
    check(xp3RegisterFunction(ctx, BAD_CAST "ex:h()", 1 << 1, neverCalled,
                              NULL, &err) < 0 && err == NULL, "unknown flag");
    check(findFunction(ctx, EX_NS, "h", 0) == NULL,
          "invalid arguments register nothing");

    xp3FreeStaticContext(ctx);
}

/* Types in signatures, as parameter and as result */
static const struct {
    const char *type;
    xp3AstKind kind;
    int value;
    xp3Occurrence occurrence;
} signatureTypeCases[] = {
    { "xs:integer", XP3_AST_ATOMIC_TYPE, XP3_TYPE_INTEGER, XP3_OCC_ONE },
    { "xs:numeric?", XP3_AST_ATOMIC_TYPE, XP3_SCHEMA_NUMERIC,
      XP3_OCC_OPTIONAL },
    { "xs:anyAtomicType+", XP3_AST_ATOMIC_TYPE, XP3_TYPE_ANY_ATOMIC,
      XP3_OCC_ONE_OR_MORE },
    { "Q{" XP3_NS_XS "}date", XP3_AST_ATOMIC_TYPE, XP3_TYPE_DATE,
      XP3_OCC_ONE },
    { "(xs:string)", XP3_AST_ATOMIC_TYPE, XP3_TYPE_STRING, XP3_OCC_ONE },
    { "item()*", XP3_AST_ANY_ITEM_TYPE, 0, XP3_OCC_ZERO_OR_MORE },
    { "node()", XP3_AST_KIND_TEST, XP3_KIND_ANY, XP3_OCC_ONE },
    { "element()", XP3_AST_KIND_TEST, XP3_KIND_ELEMENT, XP3_OCC_ONE },
    { "element(*)?", XP3_AST_KIND_TEST, XP3_KIND_ELEMENT, XP3_OCC_OPTIONAL },
    { "attribute(*)+", XP3_AST_KIND_TEST, XP3_KIND_ATTRIBUTE,
      XP3_OCC_ONE_OR_MORE },
    { "document-node()", XP3_AST_KIND_TEST, XP3_KIND_DOCUMENT, XP3_OCC_ONE },
    { "text()", XP3_AST_KIND_TEST, XP3_KIND_TEXT, XP3_OCC_ONE },
    { "comment()", XP3_AST_KIND_TEST, XP3_KIND_COMMENT, XP3_OCC_ONE },
    { "processing-instruction()", XP3_AST_KIND_TEST, XP3_KIND_PI,
      XP3_OCC_ONE },
    { "namespace-node()", XP3_AST_KIND_TEST, XP3_KIND_NAMESPACE_NODE,
      XP3_OCC_ONE },
    { "empty-sequence()", XP3_AST_EMPTY_SEQUENCE_TYPE, 0, XP3_OCC_ONE },
    { "function(*)", XP3_AST_FUNCTION_TEST, 0, XP3_OCC_ONE },
    { "map(*)?", XP3_AST_MAP_TEST, 0, XP3_OCC_OPTIONAL },
    { "array(*)*", XP3_AST_ARRAY_TEST, 0, XP3_OCC_ZERO_OR_MORE }
};

static void
testSignatureTypes(void) {
    const xp3ExtFunction *func;
    xp3StaticContext *ctx;
    char sig[200];
    char what[300];
    size_t i;

    for (i = 0; i < sizeof(signatureTypeCases) /
                    sizeof(signatureTypeCases[0]); i++) {
        ctx = exampleContext();
        if (ctx == NULL) {
            check(0, "context for signature types");
            return;
        }
        snprintf(sig, sizeof(sig), "ex:f($a as %s) as %s",
                 signatureTypeCases[i].type, signatureTypeCases[i].type);
        snprintf(what, sizeof(what), "the type of %s", sig);
        check(xp3RegisterFunction(ctx, BAD_CAST sig, 0, neverCalled, NULL,
                                  NULL) == 0, what);
        func = findFunction(ctx, EX_NS, "f", 1);
        check((func != NULL) &&
              typeIs(&func->info.params[0], signatureTypeCases[i].kind,
                     signatureTypeCases[i].value,
                     signatureTypeCases[i].occurrence) &&
              typeIs(&func->info.result, signatureTypeCases[i].kind,
                     signatureTypeCases[i].value,
                     signatureTypeCases[i].occurrence), what);
        xp3FreeStaticContext(ctx);
    }

    /* Type names get the default element namespace, as in expressions */
    ctx = exampleContext();
    if ((ctx == NULL) ||
        (xp3SetDefaultElementNamespace(ctx, BAD_CAST XP3_NS_XS) < 0)) {
        check(0, "context with a default element namespace");
        xp3FreeStaticContext(ctx);
        return;
    }
    check(xp3RegisterFunction(ctx, BAD_CAST "ex:f($a as integer)", 0,
                              neverCalled, NULL, NULL) == 0,
          "an unprefixed type name in the default element namespace");
    func = findFunction(ctx, EX_NS, "f", 1);
    check((func != NULL) &&
          typeIs(&func->info.params[0], XP3_AST_ATOMIC_TYPE,
                 XP3_TYPE_INTEGER, XP3_OCC_ONE),
          "the type name resolved with the default element namespace");
    xp3FreeStaticContext(ctx);
}

/* Registration errors, at their position in the signature */
static const struct {
    const char *signature;
    const char *code;
    int line;
    int column;
} registerErrorCases[] = {
    /* Syntax */
    { "", "XPST0003", 1, 1 },
    { "ex:f", "XPST0003", 1, 5 },
    { "ex:f(", "XPST0003", 1, 6 },
    { "ex:f($a as)", "XPST0003", 1, 11 },
    { "ex:f($a $b)", "XPST0003", 1, 9 },
    { "ex:f(a)", "XPST0003", 1, 6 },
    { "ex:f() as", "XPST0003", 1, 10 },
    { "ex:f() as xs:string extra", "XPST0003", 1, 21 },
    { "ex:f($a as xs:string) { 1 }", "XPST0003", 1, 23 },
    { "if()", "XPST0003", 1, 1 },

    /* Names and types */
    { "nope:f()", "XPST0081", 1, 1 },
    { "ex:f($nope:a)", "XPST0081", 1, 6 },
    { "ex:f($a as nope:t)", "XPST0081", 1, 12 },
    { "ex:f($a as xs:nothing)", "XPST0051", 1, 12 },
    { "ex:f(\n  $a as xs:nothing)", "XPST0051", 2, 9 },
    { "ex:f() as xs:anyType", "XPST0051", 1, 11 },
    { "Q{}f()", "XQST0060", 1, 1 },
    { "Q{http://www.w3.org/2000/xmlns/}f()", "XQST0070", 1, 1 },
    { "ex:dup($b as xs:string)", "XQST0034", 1, 1 },
    { "ex:f($a, $a)", "XQST0039", 1, 10 },
    { "ex:f($ex:a, $Q{" EX_NS "}a)", "XQST0039", 1, 13 },

    /* The reserved namespaces; an unprefixed name is in fn */
    { "f()", "XQST0045", 1, 1 },
    { "fn:f()", "XQST0045", 1, 1 },
    { "xs:f()", "XQST0045", 1, 1 },
    { "xsi:f()", "XQST0045", 1, 1 },
    { "xml:f()", "XQST0045", 1, 1 },
    { "math:f()", "XQST0045", 1, 1 },
    { "map:f()", "XQST0045", 1, 1 },
    { "array:f()", "XQST0045", 1, 1 },
    { "Q{http://www.w3.org/2012/xquery}f()", "XQST0045", 1, 1 },

    /* Item types that xp3FuncType keeps only by kind */
    { "ex:f($a as element(a))", "UNSUPPORTED", 1, 12 },
    { "ex:f($a as attribute(a))", "UNSUPPORTED", 1, 12 },
    { "ex:f($a as element(*, xs:untyped))", "UNSUPPORTED", 1, 12 },
    { "ex:f($a as schema-element(a))", "UNSUPPORTED", 1, 12 },
    { "ex:f($a as schema-attribute(a))", "UNSUPPORTED", 1, 12 },
    { "ex:f($a as document-node(element()))", "UNSUPPORTED", 1, 12 },
    { "ex:f($a as processing-instruction(pi))", "UNSUPPORTED", 1, 12 },
    { "ex:f($a as function() as item())", "UNSUPPORTED", 1, 12 },
    { "ex:f($a as map(xs:string, item()))", "UNSUPPORTED", 1, 12 },
    { "ex:f($a as array(item()))", "UNSUPPORTED", 1, 12 },
    { "ex:f() as xs:error", "UNSUPPORTED", 1, 11 }
};

static void
testRegisterErrors(void) {
    xp3StaticContext *ctx = exampleContext();
    const xp3ExtFunction *func;
    xp3Error *err;
    char what[200];
    size_t i;

    if ((ctx == NULL) ||
        (xp3RegisterFunction(ctx, BAD_CAST "ex:dup($a)", 0, neverCalled,
                             NULL, NULL) < 0)) {
        check(0, "context for registration errors");
        xp3FreeStaticContext(ctx);
        return;
    }

    for (i = 0; i < sizeof(registerErrorCases) /
                    sizeof(registerErrorCases[0]); i++) {
        err = NULL;
        snprintf(what, sizeof(what), "%s raises %s at %d:%d",
                 registerErrorCases[i].signature, registerErrorCases[i].code,
                 registerErrorCases[i].line, registerErrorCases[i].column);
        check((xp3RegisterFunction(ctx,
                                   BAD_CAST registerErrorCases[i].signature,
                                   0, neverCalled, NULL, &err) < 0) &&
              strEq(xp3ErrorCode(err), registerErrorCases[i].code) &&
              (xp3ErrorLine(err) == registerErrorCases[i].line) &&
              (xp3ErrorColumn(err) == registerErrorCases[i].column), what);
        if ((err != NULL) && (xp3ErrorMessage(err) == NULL))
            check(0, "a registration error has a message");
        xp3ErrorFree(err);
        check(xp3RegisterFunction(ctx,
                                  BAD_CAST registerErrorCases[i].signature,
                                  0, neverCalled, NULL, NULL) < 0,
              "an error without an error argument");
    }

    func = xp3StaticContextFunctions(ctx, BAD_CAST EX_NS, BAD_CAST "dup");
    check((func != NULL) && (func->info.arity == 1) && (func->next == NULL) &&
          (func->info.params[0].kind == XP3_AST_ANY_ITEM_TYPE),
          "a duplicate leaves the first registration");
    check(xp3StaticContextFunctions(ctx, BAD_CAST EX_NS,
                                    BAD_CAST "f") == NULL,
          "failed registrations register nothing");

    /* The limits of expressions apply to signatures */
    ctx->maxExprLength = 4;
    err = NULL;
    check((xp3RegisterFunction(ctx, BAD_CAST "ex:f()", 0, neverCalled, NULL,
                               &err) < 0) && strEq(xp3ErrorCode(err), "LIMIT"),
          "a signature longer than the expression limit");
    xp3ErrorFree(err);
    ctx->maxExprLength = XP3_DEFAULT_MAX_EXPR_LENGTH;
    ctx->maxParseDepth = 9;
    check(xp3RegisterFunction(ctx, BAD_CAST "ex:f($a as (xs:string))", 0,
                              neverCalled, NULL, NULL) == 0,
          "an item type nested within the parse depth");
    err = NULL;
    check((xp3RegisterFunction(ctx, BAD_CAST "ex:g($a as (((xs:string))))",
                               0, neverCalled, NULL, &err) < 0) &&
          strEq(xp3ErrorCode(err), "LIMIT"),
          "an item type nested beyond the parse depth");
    xp3ErrorFree(err);

    xp3FreeStaticContext(ctx);
}

static void
testAllocationFailures(void) {
    int i;

    if (!xp3TestAllocInstall()) {
        printf("testcontext: allocation-failure tests skipped: this libxml2 "
               "ignores xmlMemSetup()\n");
        xp3TestAllocRestore();
        return;
    }

    /* Creating a context: every failure must return NULL without leaks. */
    for (i = 0; i < 1000; i++) {
        xp3StaticContext *ctx;

        xp3TestAllocFailAt(i);
        ctx = xp3NewStaticContext();
        if (!xp3TestAllocFailed()) {
            check(ctx != NULL &&
                  strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "err"),
                        XP3_ERR_NS),
                  "context complete after allocation failures");
            xp3FreeStaticContext(ctx);
            break;
        }
        check(ctx == NULL, "xp3NewStaticContext returns NULL on failure");
        xp3FreeStaticContext(ctx);
    }
    check(i > 0 && i < 1000, "xp3NewStaticContext failure points exercised");

    /* Declaring: a failure returns -1 and keeps the previous binding. */
    for (i = 0; i < 1000; i++) {
        xp3StaticContext *ctx;
        int ret;

        xp3TestAllocFailAt(-1);
        ctx = xp3NewStaticContext();
        if (ctx == NULL) {
            check(0, "context for declare test");
            break;
        }
        xp3DeclareNamespace(ctx, BAD_CAST "p", BAD_CAST "urn:old");

        xp3TestAllocFailAt(i);
        ret = xp3DeclareNamespace(ctx, BAD_CAST "p", BAD_CAST "urn:new");
        if (!xp3TestAllocFailed()) {
            check(ret == 0 &&
                  strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "p"),
                        "urn:new"),
                  "declare complete after allocation failures");
            xp3TestAllocFailAt(-1);
            xp3FreeStaticContext(ctx);
            break;
        }
        check(ret < 0, "xp3DeclareNamespace returns -1 on failure");
        check(strEq(xp3StaticContextLookupNs(ctx, BAD_CAST "p"), "urn:old"),
              "previous binding kept on failure");
        xp3TestAllocFailAt(-1);
        xp3FreeStaticContext(ctx);
    }
    check(i > 0 && i < 1000, "xp3DeclareNamespace failure points exercised");

    /* Declaring variables and the default namespace: a failure returns
     * -1 and declares nothing. */
    for (i = 0; i < 1000; i++) {
        xp3StaticContext *ctx;
        int ret;

        xp3TestAllocFailAt(-1);
        ctx = xp3NewStaticContext();
        if (ctx == NULL) {
            check(0, "context for variable test");
            break;
        }

        xp3TestAllocFailAt(i);
        ret = xp3DeclareVariable(ctx, BAD_CAST "urn:x", BAD_CAST "v");
        if (ret == 0)
            ret = xp3SetDefaultElementNamespace(ctx, BAD_CAST "urn:d");
        if (!xp3TestAllocFailed()) {
            check(ret == 0 &&
                  xp3StaticContextHasVariable(ctx, BAD_CAST "urn:x",
                                              BAD_CAST "v") &&
                  strEq(ctx->defaultElementNs, "urn:d"),
                  "variable declared after allocation failures");
            xp3TestAllocFailAt(-1);
            xp3FreeStaticContext(ctx);
            break;
        }
        check(ret < 0, "a failed declaration returns -1");
        check(ctx->defaultElementNs == NULL,
              "default namespace unchanged on failure");
        xp3TestAllocFailAt(-1);
        xp3FreeStaticContext(ctx);
    }
    check(i > 0 && i < 1000, "xp3DeclareVariable failure points exercised");

    /* Registering a function: a failure is NO_MEMORY and registers
     * nothing. */
    for (i = 0; i < 1000; i++) {
        xp3StaticContext *ctx;
        xp3Error *err = NULL;
        int ret;

        xp3TestAllocFailAt(-1);
        ctx = exampleContext();
        if (ctx == NULL) {
            check(0, "context for register test");
            break;
        }

        xp3TestAllocFailAt(i);
        ret = xp3RegisterFunction(ctx, BAD_CAST "ex:f($a as xs:integer, "
                                  "$b as node()?) as xs:string*", 0,
                                  neverCalled, NULL, &err);
        if (!xp3TestAllocFailed()) {
            check(ret == 0 && err == NULL &&
                  findFunction(ctx, EX_NS, "f", 2) != NULL,
                  "function registered after allocation failures");
            xp3TestAllocFailAt(-1);
            xp3FreeStaticContext(ctx);
            break;
        }
        check(ret < 0 && strEq(xp3ErrorCode(err), "NO_MEMORY"),
              "a failed registration returns -1 with NO_MEMORY");
        check(xp3StaticContextFunctions(ctx, BAD_CAST EX_NS,
                                        BAD_CAST "f") == NULL,
              "a failed registration registers nothing");
        xp3ErrorFree(err);
        xp3TestAllocFailAt(-1);
        xp3FreeStaticContext(ctx);
    }
    check(i > 0 && i < 1000, "xp3RegisterFunction failure points exercised");

    /* Another arity of a registered name, and a new name in the table */
    for (i = 0; i < 1000; i++) {
        xp3StaticContext *ctx;
        const xp3ExtFunction *func;
        int ret;

        xp3TestAllocFailAt(-1);
        ctx = exampleContext();
        if ((ctx == NULL) ||
            (xp3RegisterFunction(ctx, BAD_CAST "ex:f()", 0, neverCalled, NULL,
                                 NULL) < 0)) {
            check(0, "context for the second register test");
            xp3FreeStaticContext(ctx);
            break;
        }

        xp3TestAllocFailAt(i);
        ret = xp3RegisterFunction(ctx, BAD_CAST "ex:f($a)", 0, neverCalled,
                                  NULL, NULL);
        if (ret == 0)
            ret = xp3RegisterFunction(ctx, BAD_CAST "ex:g()", 0, neverCalled,
                                      NULL, NULL);
        if (!xp3TestAllocFailed()) {
            check(ret == 0 && findFunction(ctx, EX_NS, "f", 0) != NULL &&
                  findFunction(ctx, EX_NS, "f", 1) != NULL &&
                  findFunction(ctx, EX_NS, "g", 0) != NULL,
                  "arities and names registered after allocation failures");
            xp3TestAllocFailAt(-1);
            xp3FreeStaticContext(ctx);
            break;
        }
        func = findFunction(ctx, EX_NS, "f", 0);
        check(ret < 0 && func != NULL && findFunction(ctx, EX_NS, "g", 0) ==
              NULL, "a failed registration keeps the earlier ones");
        xp3TestAllocFailAt(-1);
        xp3FreeStaticContext(ctx);
    }
    check(i > 0 && i < 1000, "second xp3RegisterFunction failure points "
          "exercised");

    /* A signature with an error: the error, or NO_MEMORY */
    for (i = 0; i < 1000; i++) {
        xp3StaticContext *ctx;
        xp3Error *err = NULL;
        int ret;

        xp3TestAllocFailAt(-1);
        ctx = exampleContext();
        if (ctx == NULL) {
            check(0, "context for the register error test");
            break;
        }

        xp3TestAllocFailAt(i);
        ret = xp3RegisterFunction(ctx, BAD_CAST "ex:f($a, $b as xs:nothing)",
                                  0, neverCalled, NULL, &err);
        check(ret < 0, "a signature with an error is never registered");
        if (!xp3TestAllocFailed()) {
            check(strEq(xp3ErrorCode(err), "XPST0051"),
                  "the signature error after allocation failures");
            xp3ErrorFree(err);
            xp3TestAllocFailAt(-1);
            xp3FreeStaticContext(ctx);
            break;
        }
        check(strEq(xp3ErrorCode(err), "NO_MEMORY") ||
              strEq(xp3ErrorCode(err), "XPST0051"),
              "a signature error or NO_MEMORY under allocation failures");
        xp3ErrorFree(err);
        xp3TestAllocFailAt(-1);
        xp3FreeStaticContext(ctx);
    }
    check(i > 0 && i < 1000, "failure points of a signature error "
          "exercised");

    xp3TestAllocRestore();
}

int
main(void) {
    xmlInitParser();

    testDefaults();
    testLanguageLevel();
    testDeclareNamespace();
    testRegisterFunction();
    testSignatureTypes();
    testRegisterErrors();
    testAllocationFailures();

    if (failures == 0)
        printf("testcontext: OK\n");
    return(failures == 0 ? 0 : 1);
}
