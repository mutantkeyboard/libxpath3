/**
 * @file
 *
 * @brief Static context (internal)
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XP3CONTEXT_H
#define XP3_XP3CONTEXT_H

#include <stddef.h>

#include <libxml/hash.h>
#include <libxml/xmlstring.h>

#include <libxpath3/xpath3.h>

/* Namespaces bound in every new static context */
#define XP3_NS_XML   "http://www.w3.org/XML/1998/namespace"
#define XP3_NS_XS    "http://www.w3.org/2001/XMLSchema"
#define XP3_NS_XSI   "http://www.w3.org/2001/XMLSchema-instance"
#define XP3_NS_FN    "http://www.w3.org/2005/xpath-functions"
#define XP3_NS_MATH  "http://www.w3.org/2005/xpath-functions/math"
#define XP3_NS_MAP   "http://www.w3.org/2005/xpath-functions/map"
#define XP3_NS_ARRAY "http://www.w3.org/2005/xpath-functions/array"

/* Reserved for namespace declarations, never in names */
#define XP3_NS_XMLNS "http://www.w3.org/2000/xmlns/"

/* Default limit on the length of an expression, in bytes */
#define XP3_DEFAULT_MAX_EXPR_LENGTH (1024 * 1024)
/* Default XPath version */
#define XP3_DEFAULT_LANGUAGE_LEVEL XP3_XPATH_31


/* Default limit on parser nesting, the same values as libxml2's
 * XPATH_MAX_RECURSION_DEPTH */
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
  #define XP3_DEFAULT_MAX_PARSE_DEPTH 500
#else
  #define XP3_DEFAULT_MAX_PARSE_DEPTH 5000
#endif

struct _xp3StaticContext {
    xp3LanguageLevel level;     /* accepted XPath version */
    xmlHashTable *namespaces;   /* prefix -> namespace URI (xmlChar *) */
    xmlChar *defaultElementNs;  /* for element and type names, or NULL */
    xmlHashTable *variables;    /* (local name, URI) of declared
                                 * variables, or NULL if none */
    size_t maxExprLength;       /* compile-time limit, in bytes */
    int maxParseDepth;          /* compile-time limit on nesting */
    int noOptimize;             /* internal, for tests and the QT3
                                 * runner: no folding or path rewrites */
};

const xmlChar *
xp3StaticContextLookupNs(const xp3StaticContext *ctx, const xmlChar *prefix);

int
xp3StaticContextHasVariable(const xp3StaticContext *ctx, const xmlChar *uri,
                            const xmlChar *localName);

#endif /* XP3_XP3CONTEXT_H */
