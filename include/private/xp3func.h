/**
 * @file
 *
 * @brief Built-in function table (internal)
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XP3FUNC_H
#define XP3_XP3FUNC_H

#include <stddef.h>

#include <libxml/xmlstring.h>

#include <libxpath3/xpath3.h>

/* Namespace of a built-in function */
typedef enum {
    XP3_FUNC_NS_FN = 0,         /* http://www.w3.org/2005/xpath-functions */
    XP3_FUNC_NS_MATH,           /* .../xpath-functions/math */
    XP3_FUNC_NS_MAP,            /* .../xpath-functions/map */
    XP3_FUNC_NS_ARRAY           /* .../xpath-functions/array */
} xp3FuncNamespace;

/*
 * Flags of a signature. Apart from XP3_FUNC_VARIADIC, they are the
 * function properties of F&O 3.1, 1.7.4. Constant folding needs them:
 * a call can only be folded if none of the first four are set.
 * XP3_FUNC_ORDER does not prevent folding, because any one order is a
 * valid result; it tells the evaluator that the order of the result
 * cannot be relied on between calls.
 */
#define XP3_FUNC_VARIADIC (1 << 0)  /* fn:concat: also any higher arity */
#define XP3_FUNC_FOCUS (1 << 1)     /* focus-dependent */
#define XP3_FUNC_CONTEXT (1 << 2)   /* context-dependent */
#define XP3_FUNC_NONDETERMINISTIC (1 << 3)
#define XP3_FUNC_HIGHER_ORDER (1 << 4) /* takes or returns a function */
#define XP3_FUNC_ORDER (1 << 5)     /* nondeterministic with respect to
                                     * ordering: fn:distinct-values,
                                     * fn:unordered, map:keys... */

/* One signature of a built-in function, with a fixed arity */
typedef struct _xp3FuncInfo xp3FuncInfo;

struct _xp3FuncInfo {
    xp3FuncNamespace ns;
    const char *name;           /* local name, for example "substring" */
    int arity;                  /* the minimum if XP3_FUNC_VARIADIC */
    xp3LanguageLevel level;     /* the first level that has it */
    int flags;
    const char *signature;      /* as written in F&O 3.1 */
};

const xp3FuncInfo *
xp3FuncLookup(const xmlChar *uri, const xmlChar *localName, int arity,
              xp3LanguageLevel level);

int
xp3FuncNameExists(const xmlChar *uri, const xmlChar *localName,
                  xp3LanguageLevel level);

const xmlChar *
xp3FuncNamespaceUri(xp3FuncNamespace ns);

size_t
xp3FuncCount(void);

const xp3FuncInfo *
xp3FuncAt(size_t index);

#endif /* XP3_XP3FUNC_H */
