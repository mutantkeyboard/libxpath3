/**
 * @file
 *
 * @brief Static analysis and compiled expressions (internal)
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XP3STATIC_H
#define XP3_XP3STATIC_H

#include <stddef.h>

#include <libxml/xmlstring.h>

#include <libxpath3/xpath3.h>
#include "private/xp3ast.h"
#include "private/xp3types.h"

/*
 * The built-in schema types an ATOMIC_TYPE node can name, stored in its
 * op after analysis: an xp3TypeId for the atomic types, then the other
 * types of XML Schema 1.1 and XDM 3.1 that expressions can name.
 */
typedef enum {
    XP3_SCHEMA_NUMERIC = XP3_TYPE_COUNT, /* xs:numeric, a union (3.1) */
    XP3_SCHEMA_ERROR,           /* xs:error, a union without members */
    XP3_SCHEMA_NMTOKENS,        /* list of xs:NMTOKEN */
    XP3_SCHEMA_IDREFS,          /* list of xs:IDREF */
    XP3_SCHEMA_ENTITIES,        /* list of xs:ENTITY */
    XP3_SCHEMA_ANY_SIMPLE,      /* xs:anySimpleType */
    XP3_SCHEMA_ANY,             /* xs:anyType, complex */
    XP3_SCHEMA_UNTYPED          /* xs:untyped, complex */
} xp3SchemaType;

/* A declared variable that a compiled expression uses */
typedef struct {
    const xmlChar *uri;         /* NULL for no namespace */
    const xmlChar *localName;
    int slot;
} xp3ExprVariable;

/*
 * A compiled expression: the tree annotated by static analysis. It
 * holds copies of everything it needs from the static context, so the
 * context can be freed after compiling, and it is never changed after
 * compiling, so several evaluations can share it.
 */
struct _xp3Expr {
    xp3Ast *ast;                /* owns all memory below */
    xp3LanguageLevel level;
    int slotCount;              /* variable slots evaluation needs */
    int variableCount;
    xp3ExprVariable *variables; /* declared variables used, in the order
                                 * of their first use */
};

xp3Expr *
xp3CompileLength(const xp3StaticContext *ctx, const xmlChar *expr,
                 size_t length, xp3Error **error);

#endif /* XP3_XP3STATIC_H */
