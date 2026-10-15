/*
 * xp3func.c: the table of built-in functions
 *
 * Every function of F&O 3.1 in the fn, math, map and array namespaces,
 * with one entry per arity: its signature, the first language level
 * that has it, and its properties. Static analysis checks function
 * calls and named function references against it (XPST0017), so a
 * 3.0 or 3.1 function is unknown at a lower language level, as in the
 * static context of that version.
 *
 * The table itself is codegen/functions.inc, generated from the W3C
 * specifications by codegen/genFunctions.py. Constructor functions
 * such as xs:integer() (F&O 3.1, 18) are not in it: they follow from
 * the atomic types in xp3types.c.
 *
 * The signature text is the one of F&O 3.1 at every level. Where a
 * type changed between the versions, as the argument of
 * fn:string-join did, the 3.1 type applies at level 2.0 and 3.0 too:
 * the level restricts which functions exist, not their types
 * (doc/DESIGN.md 5.1).
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stddef.h>
#include <string.h>

#include <libxml/xmlstring.h>

#include "private/xp3context.h"
#include "private/xp3func.h"

#include "codegen/functions.inc"

#define XP3_TABLE_SIZE(t) (sizeof(t) / sizeof((t)[0]))

/* Indexed by xp3FuncNamespace */
static const char *const xp3FuncNamespaces[] = {
    XP3_NS_FN,
    XP3_NS_MATH,
    XP3_NS_MAP,
    XP3_NS_ARRAY
};

/*
 * Find the namespace of a URI. Returns -1 if no built-in function has
 * that namespace.
 */
static int
xp3FuncFindNamespace(const xmlChar *uri) {
    size_t i;

    if (uri == NULL)
        return(-1);
    for (i = 0; i < XP3_TABLE_SIZE(xp3FuncNamespaces); i++) {
        if (xmlStrEqual(uri, BAD_CAST xp3FuncNamespaces[i]))
            return((int) i);
    }
    return(-1);
}

/*
 * Find the first entry of a name with a binary search, as the table is
 * sorted by namespace, then name, then arity. Returns its index, or -1
 * if the name is not in the table.
 */
static int
xp3FuncFindName(int ns, const xmlChar *localName) {
    int low = 0;
    int high = (int) XP3_TABLE_SIZE(xp3Functions);

    while (low < high) {
        int mid = low + (high - low) / 2;
        const xp3FuncInfo *info = &xp3Functions[mid];
        int cmp;

        if ((int) info->ns != ns)
            cmp = ((int) info->ns < ns) ? -1 : 1;
        else
            cmp = strcmp(info->name, (const char *) localName);
        if (cmp < 0)
            low = mid + 1;
        else
            high = mid;
    }
    if ((low < (int) XP3_TABLE_SIZE(xp3Functions)) &&
        ((int) xp3Functions[low].ns == ns) &&
        (strcmp(xp3Functions[low].name, (const char *) localName) == 0))
        return(low);
    return(-1);
}

/**
 * Find the built-in function with a name and arity, as static analysis
 * does for a function call or a named function reference such as
 * fn:substring#2.
 *
 * The parser marks an arity that does not fit in an int with
 * XP3_ARITY_OVERFLOW, which is negative. The caller reports it as
 * FOAR0002 before looking the function up; here it only finds nothing.
 *
 * @param uri        namespace URI of the name
 * @param localName  local name
 * @param arity      number of arguments, at least 0
 * @param level      language level of the static context
 * @returns the signature, or NULL if the function does not exist with
 *          that arity at that level
 */
const xp3FuncInfo *
xp3FuncLookup(const xmlChar *uri, const xmlChar *localName, int arity,
              xp3LanguageLevel level) {
    int ns = xp3FuncFindNamespace(uri);
    int i;

    if ((ns < 0) || (localName == NULL) || (arity < 0))
        return(NULL);
    i = xp3FuncFindName(ns, localName);
    if (i < 0)
        return(NULL);
    for (; (i < (int) XP3_TABLE_SIZE(xp3Functions)) &&
           ((int) xp3Functions[i].ns == ns) &&
           (strcmp(xp3Functions[i].name, (const char *) localName) == 0);
         i++) {
        const xp3FuncInfo *info = &xp3Functions[i];

        if (info->level > level)
            continue;
        if ((info->arity == arity) ||
            ((info->flags & XP3_FUNC_VARIADIC) && (arity > info->arity)))
            return(info);
    }
    return(NULL);
}

/**
 * Whether a built-in function with a name exists at a language level,
 * with any arity. Static analysis uses it to tell "unknown function"
 * from "wrong number of arguments" in XPST0017 messages.
 *
 * @param uri        namespace URI of the name
 * @param localName  local name
 * @param level      language level of the static context
 * @returns 1 if it exists, otherwise 0
 */
int
xp3FuncNameExists(const xmlChar *uri, const xmlChar *localName,
                  xp3LanguageLevel level) {
    int ns = xp3FuncFindNamespace(uri);
    int i;

    if ((ns < 0) || (localName == NULL))
        return(0);
    i = xp3FuncFindName(ns, localName);
    if (i < 0)
        return(0);
    for (; (i < (int) XP3_TABLE_SIZE(xp3Functions)) &&
           ((int) xp3Functions[i].ns == ns) &&
           (strcmp(xp3Functions[i].name, (const char *) localName) == 0);
         i++) {
        if (xp3Functions[i].level <= level)
            return(1);
    }
    return(0);
}

/**
 * Get the URI of a function namespace.
 *
 * @param ns  the namespace
 * @returns the URI, or NULL for an invalid namespace
 */
const xmlChar *
xp3FuncNamespaceUri(xp3FuncNamespace ns) {
    if ((unsigned) ns >= XP3_TABLE_SIZE(xp3FuncNamespaces))
        return(NULL);
    return(BAD_CAST xp3FuncNamespaces[ns]);
}

/**
 * Get the number of signatures in the table.
 *
 * @returns the number of entries
 */
size_t
xp3FuncCount(void) {
    return(XP3_TABLE_SIZE(xp3Functions));
}

/**
 * Get a signature by its position in the table, for example to list
 * all built-in functions.
 *
 * @param index  position, from 0 to xp3FuncCount() - 1
 * @returns the signature, or NULL if index is out of range
 */
const xp3FuncInfo *
xp3FuncAt(size_t index) {
    if (index >= XP3_TABLE_SIZE(xp3Functions))
        return(NULL);
    return(&xp3Functions[index]);
}
