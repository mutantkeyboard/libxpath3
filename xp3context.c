/*
 * xp3context.c: static context
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <string.h>

#include <libxml/hash.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

#include "private/xp3context.h"
#include "private/xp3error.h"
#include "private/xp3func.h"
#include "private/xp3static.h"


/* Namespaces bound in every new static context */
static const struct {
    const char *prefix;
    const char *uri;
} xp3PredeclaredNs[] = {
    { "xml",   XP3_NS_XML },
    { "xs",    XP3_NS_XS },
    { "xsi",   XP3_NS_XSI },
    { "fn",    XP3_NS_FN },
    { "math",  XP3_NS_MATH },
    { "map",   XP3_NS_MAP },
    { "array", XP3_NS_ARRAY },
    { "err",   XP3_ERR_NS }
};

/*
 * Bind a prefix, replacing an existing binding. The URI is copied.
 *
 * Returns 0 on success, -1 if memory allocation failed. On failure the
 * previous binding is left unchanged.
 */
static int
xp3BindNs(xp3StaticContext *ctx, const xmlChar *prefix, const xmlChar *uri) {
    xmlChar *copy;

    copy = xmlStrdup(uri);
    if (copy == NULL)
        return(-1);
    if (xmlHashUpdateEntry(ctx->namespaces, prefix, copy,
                           xmlHashDefaultDeallocator) < 0) {
        xmlFree(copy);
        return(-1);
    }
    return(0);
}

/* Free the entries of a name in the function registry */
static void
xp3FreeFunctions(void *payload, const xmlChar *name) {
    xp3ExtFunction *func = payload;

    (void) name;
    while (func != NULL) {
        xp3ExtFunction *next = func->next;

        xmlFree(func);
        func = next;
    }
}

/**
 * Create a static context.
 *
 * The new context accepts XPath 3.1 and has the prefixes xml, xs, xsi,
 * fn, math, map, array and err bound to their standard namespaces.
 *
 * @returns the new context, or NULL if memory allocation failed
 */
xp3StaticContext *
xp3NewStaticContext(void) {
    xp3StaticContext *ctx;
    size_t i;

    ctx = xmlMalloc(sizeof(*ctx));
    if (ctx == NULL)
        return(NULL);
    memset(ctx, 0, sizeof(*ctx));
    ctx->level = XP3_DEFAULT_LANGUAGE_LEVEL;
    ctx->maxExprLength = XP3_DEFAULT_MAX_EXPR_LENGTH;
    ctx->maxParseDepth = XP3_DEFAULT_MAX_PARSE_DEPTH;

    ctx->namespaces = xmlHashCreate(0);
    if (ctx->namespaces == NULL)
        goto error;

    for (i = 0; i < sizeof(xp3PredeclaredNs) / sizeof(xp3PredeclaredNs[0]);
         i++) {
        if (xp3BindNs(ctx, BAD_CAST xp3PredeclaredNs[i].prefix,
                      BAD_CAST xp3PredeclaredNs[i].uri) < 0)
            goto error;
    }

    return(ctx);

error:
    xp3FreeStaticContext(ctx);
    return(NULL);
}

/**
 * Free a static context. NULL is ignored.
 *
 * @param ctx  the context
 */
void
xp3FreeStaticContext(xp3StaticContext *ctx) {
    if (ctx == NULL)
        return;
    xmlHashFree(ctx->namespaces, xmlHashDefaultDeallocator);
    xmlHashFree(ctx->variables, NULL);
    xmlHashFree(ctx->functions, xp3FreeFunctions);
    xmlFree(ctx->defaultElementNs);
    xmlFree(ctx);
}

/**
 * Set the XPath version that expressions compiled with this context
 * may use.
 *
 * At a lower level, constructs and functions from later versions are
 * rejected when compiling. The semantics of accepted expressions do
 * not change.
 *
 * @param ctx    the context
 * @param level  #XP3_XPATH_20, #XP3_XPATH_30 or #XP3_XPATH_31
 * @returns 0 on success, -1 if ctx is NULL or level is not one of the
 *          values above
 */
int
xp3SetStaticContextLanguageLevel(xp3StaticContext *ctx,
                                 xp3LanguageLevel level) {
    if (ctx == NULL)
        return(-1);

    switch (level) {
        case XP3_XPATH_20:
        case XP3_XPATH_30:
        case XP3_XPATH_31:
            ctx->level = level;
            return(0);
        default:
            return(-1);
    }
}

/**
 * Bind a namespace prefix for expressions compiled with this context.
 *
 * An existing binding of the prefix is replaced. A NULL or empty URI
 * removes the binding. The prefix "xmlns" cannot be bound, and "xml"
 * can only be bound to its standard namespace.
 *
 * @param ctx     the context
 * @param prefix  the prefix, a valid NCName
 * @param uri     the namespace URI, or NULL or "" to remove the binding
 * @returns 0 on success, -1 if an argument is invalid or memory
 *          allocation failed. On failure the previous binding is kept.
 */
int
xp3DeclareNamespace(xp3StaticContext *ctx, const xmlChar *prefix,
                    const xmlChar *uri) {
    if ((ctx == NULL) || (prefix == NULL))
        return(-1);
    if (xmlValidateNCName(prefix, 0) != 0)
        return(-1);
    if (xmlStrEqual(prefix, BAD_CAST "xmlns"))
        return(-1);
    if (xmlStrEqual(prefix, BAD_CAST "xml"))
        return(xmlStrEqual(uri, BAD_CAST XP3_NS_XML) ? 0 : -1);

    if ((uri == NULL) || (*uri == 0)) {
        xmlHashRemoveEntry(ctx->namespaces, prefix, xmlHashDefaultDeallocator);
        return(0);
    }

    return(xp3BindNs(ctx, prefix, uri));
}

/**
 * Set the default element/type namespace: the namespace of unprefixed
 * element and type names in expressions compiled with this context.
 * Attribute, variable and function names are not affected. The default
 * is no namespace.
 *
 * @param ctx  the context
 * @param uri  the namespace URI, or NULL or "" for no namespace
 * @returns 0 on success, -1 if ctx is NULL or memory allocation failed.
 *          On failure the previous namespace is kept.
 */
int
xp3SetDefaultElementNamespace(xp3StaticContext *ctx, const xmlChar *uri) {
    xmlChar *copy = NULL;

    if (ctx == NULL)
        return(-1);
    if ((uri != NULL) && (*uri != 0)) {
        copy = xmlStrdup(uri);
        if (copy == NULL)
            return(-1);
    }
    xmlFree(ctx->defaultElementNs);
    ctx->defaultElementNs = copy;
    return(0);
}

/**
 * Declare a variable that expressions compiled with this context may
 * use. Its value is supplied when evaluating. Declaring a variable
 * again has no effect.
 *
 * @param ctx        the context
 * @param uri        the namespace URI of the name, or NULL or "" for
 *                   no namespace
 * @param localName  the local name, a valid NCName
 * @returns 0 on success, -1 if an argument is invalid or memory
 *          allocation failed
 */
int
xp3DeclareVariable(xp3StaticContext *ctx, const xmlChar *uri,
                   const xmlChar *localName) {
    if ((ctx == NULL) || (localName == NULL) ||
        (xmlValidateNCName(localName, 0) != 0))
        return(-1);
    if ((uri != NULL) && (*uri == 0))
        uri = NULL;
    if (xp3StaticContextHasVariable(ctx, uri, localName))
        return(0);
    if (ctx->variables == NULL) {
        ctx->variables = xmlHashCreate(0);
        if (ctx->variables == NULL)
            return(-1);
    }
    /* The table copies the names; the value only marks the entry. */
    if (xmlHashAddEntry2(ctx->variables, localName, uri, (void *) 1) < 0)
        return(-1);
    return(0);
}

/**
 * Register an extension function, for expressions compiled with this
 * context afterwards.
 *
 * The signature is written as F&O writes them, for example
 * "u:decimal-or-zero($value as item()*) as xs:decimal", and parsed
 * once, here. Its prefixes and the default element namespace of type
 * names are those of the context so far, and parameter names are only
 * for messages. A parameter without "as" is item()*, and so is the
 * result. The name must be in a namespace, and not in one of the
 * namespaces XQuery 3.1, 4.18 reserves: xml, xs, xsi, fn, math, map,
 * array and http://www.w3.org/2012/xquery. An unprefixed name is in
 * the default function namespace, fn, so it is reserved too. One name
 * may have several arities, each registered once.
 *
 * Without #XP3_FUNCTION_DETERMINISTIC in flags, the function is
 * nondeterministic: it is called on each evaluation, also with
 * constant arguments. Compiled expressions copy what they need, so
 * the context may be freed after compiling.
 *
 * Errors: XPST0003 for a syntax error in the signature, XPST0081 for
 * an undeclared prefix, XPST0051 for an unknown or non-atomic type
 * name, XQST0060 for a name in no namespace, XQST0045 for a reserved
 * namespace, XQST0070 for the xmlns namespace, XQST0034 for a name
 * and arity registered already, XQST0039 for a parameter name used
 * twice, UNSUPPORTED for item types the function conversion rules
 * can't check yet (element and attribute tests with a name or type,
 * schema-element(), schema-attribute(), document-node() with an
 * element test, processing-instruction() with a name, typed function,
 * map and array tests, and xs:error), LIMIT for a signature beyond
 * the limits of expressions, and NO_MEMORY. On error nothing is
 * registered.
 *
 * @param ctx        the context
 * @param signature  the signature, a NUL-terminated UTF-8 string
 * @param flags      xp3FunctionFlags, 0 for none
 * @param impl       the implementation
 * @param userData   passed to impl; never freed by the library, so it
 *                   must outlive every expression compiled with ctx
 * @param error      where to store the error, or NULL. It is set to
 *                   NULL on success, and must be freed with
 *                   xp3ErrorFree() otherwise.
 * @returns 0 on success, -1 on error. A NULL ctx, signature or impl,
 *          or an unknown flag, returns -1 without an error.
 */
int
xp3RegisterFunction(xp3StaticContext *ctx, const xmlChar *signature,
                    int flags, xp3FunctionImpl impl, void *userData,
                    xp3Error **error) {
    xp3ExtFunction *func;
    xp3ExtFunction *first;

    if (error != NULL)
        *error = NULL;
    if ((ctx == NULL) || (signature == NULL) || (impl == NULL) ||
        ((flags & ~XP3_FUNCTION_DETERMINISTIC) != 0))
        return(-1);

    func = xp3CompileSignature(ctx, signature, error);
    if (func == NULL)
        return(-1);
    func->impl = impl;
    func->userData = userData;
    if ((flags & XP3_FUNCTION_DETERMINISTIC) == 0)
        func->info.flags |= XP3_FUNC_NONDETERMINISTIC;

    if (ctx->functions == NULL) {
        ctx->functions = xmlHashCreate(0);
        if (ctx->functions == NULL)
            goto no_memory;
    }
    /* xp3CompileSignature() checked that the arity is new */
    first = xmlHashLookup2(ctx->functions, BAD_CAST func->info.name,
                           func->uri);
    if (first != NULL) {
        func->next = first->next;
        first->next = func;
        return(0);
    }
    if (xmlHashAddEntry2(ctx->functions, BAD_CAST func->info.name,
                         func->uri, func) < 0)
        goto no_memory;
    return(0);

no_memory:
    xmlFree(func);
    if (error != NULL)
        *error = xp3ErrorNoMemory();
    return(-1);
}

/**
 * Look up the namespace URI bound to a prefix. Without a context, the
 * prefixes bound in every new context are used.
 *
 * @param ctx     the context, or NULL
 * @param prefix  the prefix
 * @returns the namespace URI, or NULL if the prefix is not bound or
 *          prefix is NULL
 */
const xmlChar *
xp3StaticContextLookupNs(const xp3StaticContext *ctx, const xmlChar *prefix) {
    size_t i;

    if (prefix == NULL)
        return(NULL);
    if (ctx != NULL)
        return(xmlHashLookup(ctx->namespaces, prefix));
    for (i = 0; i < sizeof(xp3PredeclaredNs) / sizeof(xp3PredeclaredNs[0]);
         i++) {
        if (xmlStrEqual(prefix, BAD_CAST xp3PredeclaredNs[i].prefix))
            return(BAD_CAST xp3PredeclaredNs[i].uri);
    }
    return(NULL);
}

/**
 * Whether a variable is declared in the context.
 *
 * @param ctx        the context, or NULL for none
 * @param uri        the namespace URI, or NULL for no namespace
 * @param localName  the local name
 * @returns 1 if it is declared, otherwise 0
 */
int
xp3StaticContextHasVariable(const xp3StaticContext *ctx, const xmlChar *uri,
                            const xmlChar *localName) {
    if ((ctx == NULL) || (ctx->variables == NULL) || (localName == NULL))
        return(0);
    return(xmlHashLookup2(ctx->variables, localName, uri) != NULL);
}

/**
 * Get the extension functions registered with a name.
 *
 * @param ctx        the context, or NULL for none
 * @param uri        the namespace URI
 * @param localName  the local name
 * @returns the entry of one arity, with the others chained by next,
 *          or NULL if none is registered
 */
const xp3ExtFunction *
xp3StaticContextFunctions(const xp3StaticContext *ctx, const xmlChar *uri,
                          const xmlChar *localName) {
    if ((ctx == NULL) || (ctx->functions == NULL) || (uri == NULL) ||
        (localName == NULL))
        return(NULL);
    return(xmlHashLookup2(ctx->functions, localName, uri));
}
