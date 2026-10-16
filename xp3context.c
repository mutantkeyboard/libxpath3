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
