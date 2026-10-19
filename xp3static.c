/*
 * xp3static.c: static analysis, from a parse tree to a compiled
 * expression
 *
 * One walk over the tree resolves every name against the static
 * context and reports the static errors of XPath 3.1, section 2.3.1:
 *
 * - prefixes, with the default element/type namespace for element and
 *   type names and the default function namespace for function names
 *   [err:XPST0081]
 * - variables, from range variables, inline function parameters and
 *   the variables declared in the static context [err:XPST0008]. Each
 *   binding gets a slot number, so evaluation can keep variables in an
 *   array instead of looking them up by name.
 * - function calls and named function references, against the built-in
 *   functions of the language level (xp3func.c) and the constructor
 *   functions [err:XPST0017]
 * - type names in sequence types [err:XPST0051], in cast and castable
 *   [err:XQST0052, err:XPST0080] and in element and attribute tests
 *   [err:XPST0008]
 * - the namespace axis, which is not supported [err:XPST0010],
 *   duplicate parameter names [err:XQST0039], and names in the
 *   namespace reserved for xmlns attributes [err:XQST0070]
 *
 * The results are stored in the tree itself (xp3ast.h says which
 * fields). A constructor function call such as xs:integer($x) becomes
 * ($x cast as xs:integer?), which is how XPath 3.1, 3.14.4 defines it.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

#include "private/xp3context.h"
#include "private/xp3error.h"
#include "private/xp3func.h"
#include "private/xp3parser.h"
#include "private/xp3static.h"

/* Room for a name in a message, longer ones are cut */
#define XP3_NAME_BUF 200

/* URIs copied into the tree that are remembered, see staticUri() */
#define XP3_URI_CACHE 8

/* Which default namespace an unprefixed name gets */
typedef enum {
    XP3_DEFAULT_NONE = 0,       /* variables, attributes */
    XP3_DEFAULT_ELEMENT,        /* elements and types */
    XP3_DEFAULT_FUNCTION        /* functions */
} xp3DefaultNs;

/* A variable in scope, or a declared variable in use */
typedef struct {
    const xmlChar *uri;
    const xmlChar *localName;
    int slot;
} xp3Binding;

typedef struct {
    xp3Binding *items;
    int count;
    int max;
} xp3Bindings;

typedef struct {
    const xp3StaticContext *ctx;    /* NULL for the defaults */
    xp3LanguageLevel level;
    xp3Ast *ast;
    xp3Error *err;                  /* the first error */
    xp3Bindings scope;              /* in scope, innermost last */
    xp3Bindings declared;           /* declared variables in use */
    int slotCount;
    struct {
        const xmlChar *from;        /* a URI in the static context */
        const xmlChar *copy;        /* its copy in the tree */
    } uris[XP3_URI_CACHE];
    int uriCount;
} xp3Analysis;

/* The built-in schema types that are not atomic, see xp3SchemaType */
static const struct {
    const char *name;
    xp3SchemaType type;
    xp3LanguageLevel level;         /* the first level that has it */
} xp3OtherTypes[] = {
    { "numeric",        XP3_SCHEMA_NUMERIC,    XP3_XPATH_31 },
    { "error",          XP3_SCHEMA_ERROR,      XP3_XPATH_20 },
    { "NMTOKENS",       XP3_SCHEMA_NMTOKENS,   XP3_XPATH_20 },
    { "IDREFS",         XP3_SCHEMA_IDREFS,     XP3_XPATH_20 },
    { "ENTITIES",       XP3_SCHEMA_ENTITIES,   XP3_XPATH_20 },
    { "anySimpleType",  XP3_SCHEMA_ANY_SIMPLE, XP3_XPATH_20 },
    { "anyType",        XP3_SCHEMA_ANY,        XP3_XPATH_20 },
    { "untyped",        XP3_SCHEMA_UNTYPED,    XP3_XPATH_20 }
};

static int
analyzeNode(xp3Analysis *st, xp3AstNode *node);

/*
 * Errors
 */

/* Record an error at a node, unless one is recorded already. */
static int
staticError(xp3Analysis *st, xp3ErrCode code, const xp3AstNode *node,
            const char *format, ...) {
    va_list args;

    if (st->err != NULL)
        return(-1);
    /* Never NULL: the out-of-memory error if allocation fails */
    va_start(args, format);
    st->err = xp3ErrorNewV(code, node->line, node->column, format, args);
    va_end(args);
    return(-1);
}

static int
staticNoMemory(xp3Analysis *st) {
    if (st->err == NULL)
        st->err = xp3ErrorNoMemory();
    return(-1);
}

/* The name of a node as written, for messages. */
static const char *
displayName(const xp3AstNode *node, char *buf, size_t size) {
    const char *local = node->text ? (const char *) node->text : "";

    if (node->prefix != NULL)
        snprintf(buf, size, "%s:%s", (const char *) node->prefix, local);
    else if (node->uri != NULL)
        snprintf(buf, size, "Q{%s}%s", (const char *) node->uri, local);
    else
        snprintf(buf, size, "%s", local);
    return(buf);
}

/* The name of a level below 3.1, where every function exists */
static const char *
levelName(xp3LanguageLevel level) {
    return((level == XP3_XPATH_20) ? "XPath 2.0" : "XPath 3.0");
}

/*
 * Names
 */

/*
 * A copy in the tree of a URI from the static context, so that the
 * compiled expression does not depend on the context. Each URI is
 * copied once per expression, as long as the cache has room.
 */
static const xmlChar *
staticUri(xp3Analysis *st, const xmlChar *uri) {
    const xmlChar *copy;
    int i;

    for (i = 0; i < st->uriCount; i++) {
        if (st->uris[i].from == uri)
            return(st->uris[i].copy);
    }
    copy = xp3AstStrndup(st->ast, uri, strlen((const char *) uri));
    if (copy == NULL)
        return(NULL);
    if (st->uriCount < XP3_URI_CACHE) {
        st->uris[st->uriCount].from = uri;
        st->uris[st->uriCount].copy = copy;
        st->uriCount++;
    }
    return(copy);
}

/*
 * Resolve the name of a node: set uri to its namespace, or to NULL for
 * no namespace. The namespace of xmlns attributes cannot be used in
 * names (XPath 3.1, 2.4.5) [err:XQST0070].
 */
static int
resolveName(xp3Analysis *st, xp3AstNode *node, xp3DefaultNs defaultNs) {
    const xmlChar *uri;
    char name[XP3_NAME_BUF];

    if (node->uri != NULL) {
        /* Q{uri}local, where Q{} means no namespace */
        uri = node->uri;
        if (*uri == 0)
            node->uri = NULL;
    } else if (node->prefix != NULL) {
        uri = xp3StaticContextLookupNs(st->ctx, node->prefix);
        if (uri == NULL)
            return(staticError(st, XP3_ERR_XPST0081, node,
                               "namespace prefix '%s' in %s is not "
                               "declared", (const char *) node->prefix,
                               displayName(node, name, sizeof(name))));
    } else if (defaultNs == XP3_DEFAULT_ELEMENT) {
        uri = (st->ctx != NULL) ? st->ctx->defaultElementNs : NULL;
    } else if (defaultNs == XP3_DEFAULT_FUNCTION) {
        /* A string constant needs no copy */
        node->uri = BAD_CAST XP3_NS_FN;
        return(0);
    } else {
        uri = NULL;
    }
    if ((uri == NULL) || (*uri == 0))
        return(0);
    if (xmlStrEqual(uri, BAD_CAST XP3_NS_XMLNS))
        return(staticError(st, XP3_ERR_XQST0070, node,
                           "%s is in the namespace %s, which is reserved",
                           displayName(node, name, sizeof(name)),
                           XP3_NS_XMLNS));
    if (node->uri == uri)
        return(0);      /* Q{uri}: already in the tree */
    node->uri = staticUri(st, uri);
    return((node->uri == NULL) ? staticNoMemory(st) : 0);
}

/* Whether two resolved names are the same. */
static int
sameName(const xmlChar *uri1, const xmlChar *local1, const xmlChar *uri2,
         const xmlChar *local2) {
    return(xmlStrEqual(uri1, uri2) && xmlStrEqual(local1, local2));
}

/*
 * Types
 */

/*
 * The built-in schema type a resolved name refers to: an xp3TypeId or
 * an xp3SchemaType, or -1 if there is none at the language level.
 */
static int
schemaType(const xp3Analysis *st, const xp3AstNode *name) {
    int atomic;
    size_t i;

    if ((name->uri == NULL) || !xmlStrEqual(name->uri, BAD_CAST XP3_NS_XS))
        return(-1);
    atomic = xp3TypeLookup(name->text);
    if (atomic >= 0)
        return(atomic);
    for (i = 0; i < sizeof(xp3OtherTypes) / sizeof(xp3OtherTypes[0]); i++) {
        if (xmlStrEqual(name->text, BAD_CAST xp3OtherTypes[i].name))
            return((xp3OtherTypes[i].level <= st->level) ?
                   (int) xp3OtherTypes[i].type : -1);
    }
    return(-1);
}

/*
 * Whether a type is a generalized atomic type: an atomic type, or a
 * union of atomic types (XPath 3.1, 2.5.4). Only these can be named in
 * a sequence type.
 */
static int
isGeneralizedAtomic(int type) {
    return(((type >= 0) && (type < XP3_TYPE_COUNT)) ||
           (type == XP3_SCHEMA_NUMERIC) || (type == XP3_SCHEMA_ERROR));
}

/* Whether a type is a list type */
static int
isListType(int type) {
    return((type == XP3_SCHEMA_NMTOKENS) || (type == XP3_SCHEMA_IDREFS) ||
           (type == XP3_SCHEMA_ENTITIES));
}

/* Whether a type cannot be the target of a cast [err:XPST0080] */
static int
isAbstractSimple(int type) {
    return((type == XP3_TYPE_ANY_ATOMIC) || (type == XP3_TYPE_NOTATION) ||
           (type == XP3_SCHEMA_ANY_SIMPLE));
}

/*
 * Whether a type can be the target of a cast, and so has a constructor
 * function: a simple type that is not abstract. XPath 2.0 only casts to
 * atomic types.
 */
static int
isCastTarget(const xp3Analysis *st, int type) {
    if (isAbstractSimple(type))
        return(0);
    if (st->level == XP3_XPATH_20)
        return((type >= 0) && (type < XP3_TYPE_COUNT));
    return(isGeneralizedAtomic(type) || isListType(type));
}

/* An ATOMIC_TYPE in a sequence type [err:XPST0051] */
static int
analyzeAtomicType(xp3Analysis *st, xp3AstNode *node) {
    char name[XP3_NAME_BUF];
    int type;

    displayName(node, name, sizeof(name));
    if (resolveName(st, node, XP3_DEFAULT_ELEMENT) < 0)
        return(-1);
    type = schemaType(st, node);
    if (!isGeneralizedAtomic(type))
        return(staticError(st, XP3_ERR_XPST0051, node,
                           (type < 0) ? "unknown type %s" :
                           "%s is not an atomic type", name));
    node->op = type;
    return(0);
}

/*
 * The SEQUENCE_TYPE of a cast or castable expression, whose child is an
 * ATOMIC_TYPE [err:XQST0052, err:XPST0080].
 */
static int
analyzeCastTarget(xp3Analysis *st, xp3AstNode *seqType) {
    xp3AstNode *node = seqType->first;
    char name[XP3_NAME_BUF];
    int type;

    displayName(node, name, sizeof(name));
    if (resolveName(st, node, XP3_DEFAULT_ELEMENT) < 0)
        return(-1);
    type = schemaType(st, node);
    if (isAbstractSimple(type) &&
        ((type != XP3_SCHEMA_ANY_SIMPLE) || (st->level != XP3_XPATH_20)))
        return(staticError(st, XP3_ERR_XPST0080, node,
                           "cannot cast to the abstract type %s", name));
    if (!isCastTarget(st, type)) {
        /* XPath 2.0 named this XPST0051 */
        xp3ErrCode code = (st->level == XP3_XPATH_20) ? XP3_ERR_XPST0051 :
                                                        XP3_ERR_XQST0052;

        return(staticError(st, code, node,
                           (type < 0) ? "unknown type %s" :
                           "cannot cast to %s", name));
    }
    node->op = type;
    return(0);
}

/*
 * The type annotation in element(N, T) or attribute(N, T): any type in
 * the in-scope schema types [err:XPST0008].
 */
static int
analyzeTypeAnnotation(xp3Analysis *st, xp3AstNode *seqType) {
    xp3AstNode *node = seqType->first;
    char name[XP3_NAME_BUF];
    int type;

    displayName(node, name, sizeof(name));
    if (resolveName(st, node, XP3_DEFAULT_ELEMENT) < 0)
        return(-1);
    type = schemaType(st, node);
    if (type < 0)
        return(staticError(st, XP3_ERR_XPST0008, node, "unknown type %s",
                           name));
    node->op = type;
    return(0);
}

/* A NAME_TEST, for elements or attributes */
static int
analyzeNameTest(xp3Analysis *st, xp3AstNode *test, xp3DefaultNs defaultNs) {
    switch (test->op) {
        case XP3_NAMETEST_NAME:
            return(resolveName(st, test, defaultNs));
        case XP3_NAMETEST_ANY_LOCAL:
            /* p:* or Q{uri}*: no default namespace */
            return(resolveName(st, test, XP3_DEFAULT_NONE));
        default:
            return(0);
    }
}

static int
analyzeKindTest(xp3Analysis *st, xp3AstNode *test) {
    xp3AstNode *child = test->first;
    xp3DefaultNs defaultNs = XP3_DEFAULT_ELEMENT;
    char name[XP3_NAME_BUF];

    switch (test->op) {
        case XP3_KIND_ATTRIBUTE:
            defaultNs = XP3_DEFAULT_NONE;
            /* Fall through */
        case XP3_KIND_ELEMENT:
            for (; child != NULL; child = child->next) {
                if (child->kind == XP3_AST_NAME_TEST) {
                    if (analyzeNameTest(st, child, defaultNs) < 0)
                        return(-1);
                } else if (analyzeTypeAnnotation(st, child) < 0) {
                    return(-1);
                }
            }
            return(0);
        case XP3_KIND_SCHEMA_ATTRIBUTE:
            defaultNs = XP3_DEFAULT_NONE;
            /* Fall through */
        case XP3_KIND_SCHEMA_ELEMENT:
            /* Without a schema there are no declarations to refer to */
            displayName(child, name, sizeof(name));
            if (analyzeNameTest(st, child, defaultNs) < 0)
                return(-1);
            return(staticError(st, XP3_ERR_XPST0008, child,
                               "no %s declaration for %s",
                               (test->op == XP3_KIND_SCHEMA_ELEMENT) ?
                               "element" : "attribute", name));
        case XP3_KIND_DOCUMENT:
            return((child != NULL) ? analyzeKindTest(st, child) : 0);
        default:
            return(0);
    }
}

/*
 * A sequence type or item type, anywhere other than cast and castable.
 */
static int
analyzeType(xp3Analysis *st, xp3AstNode *node) {
    xp3AstNode *child;

    switch (node->kind) {
        case XP3_AST_ATOMIC_TYPE:
            return(analyzeAtomicType(st, node));
        case XP3_AST_KIND_TEST:
            return(analyzeKindTest(st, node));
        default:
            for (child = node->first; child != NULL; child = child->next) {
                if (analyzeType(st, child) < 0)
                    return(-1);
            }
            return(0);
    }
}

/*
 * Variables
 */

static int
bindingsPush(xp3Analysis *st, xp3Bindings *list, const xmlChar *uri,
             const xmlChar *localName, int slot) {
    if (list->count >= list->max) {
        int max = (list->max > 0) ? list->max * 2 : 16;
        xp3Binding *items;

        if (max > INT_MAX / (int) sizeof(xp3Binding))
            return(staticNoMemory(st));
        items = xmlRealloc(list->items, (size_t) max * sizeof(xp3Binding));
        if (items == NULL)
            return(staticNoMemory(st));
        list->items = items;
        list->max = max;
    }
    list->items[list->count].uri = uri;
    list->items[list->count].localName = localName;
    list->items[list->count].slot = slot;
    list->count++;
    return(0);
}

/*
 * Bring the variable a BINDING or PARAM node declares into scope with a
 * new slot.
 */
static int
bindVariable(xp3Analysis *st, xp3AstNode *node) {
    node->op = st->slotCount++;
    return(bindingsPush(st, &st->scope, node->uri, node->text, node->op));
}

/* A VARREF: find its slot [err:XPST0008]. */
static int
analyzeVarRef(xp3Analysis *st, xp3AstNode *node) {
    char name[XP3_NAME_BUF];
    int i;

    displayName(node, name, sizeof(name));
    if (resolveName(st, node, XP3_DEFAULT_NONE) < 0)
        return(-1);
    for (i = st->scope.count - 1; i >= 0; i--) {
        const xp3Binding *var = &st->scope.items[i];

        if (sameName(var->uri, var->localName, node->uri, node->text)) {
            node->op = var->slot;
            return(0);
        }
    }
    for (i = 0; i < st->declared.count; i++) {
        const xp3Binding *var = &st->declared.items[i];

        if (sameName(var->uri, var->localName, node->uri, node->text)) {
            node->op = var->slot;
            return(0);
        }
    }
    if (!xp3StaticContextHasVariable(st->ctx, node->uri, node->text))
        return(staticError(st, XP3_ERR_XPST0008, node,
                           "variable $%s is not declared", name));
    node->op = st->slotCount++;
    return(bindingsPush(st, &st->declared, node->uri, node->text,
                        node->op));
}

/* for, let, some and every: BINDING..., then the last expression */
static int
analyzeClauses(xp3Analysis *st, xp3AstNode *node) {
    int mark = st->scope.count;
    xp3AstNode *child;
    int ret = 0;

    for (child = node->first; (child != NULL) && (ret == 0);
         child = child->next) {
        if (child->kind == XP3_AST_BINDING) {
            /* The variable is in scope after its own expression */
            if ((analyzeNode(st, child->first) < 0) ||
                (resolveName(st, child, XP3_DEFAULT_NONE) < 0) ||
                (bindVariable(st, child) < 0))
                ret = -1;
        } else {
            ret = analyzeNode(st, child);
        }
    }
    st->scope.count = mark;
    return(ret);
}

/* function(PARAM...) as T { body } [err:XQST0039] */
static int
analyzeInlineFunction(xp3Analysis *st, xp3AstNode *node) {
    int mark = st->scope.count;
    xp3AstNode *child;
    xp3AstNode *other;
    char name[XP3_NAME_BUF];
    int ret = 0;

    for (child = node->first; child != NULL; child = child->next) {
        if (child->kind != XP3_AST_PARAM)
            continue;
        displayName(child, name, sizeof(name));
        if (resolveName(st, child, XP3_DEFAULT_NONE) < 0)
            return(-1);
        for (other = node->first; other != child; other = other->next) {
            if (sameName(other->uri, other->text, child->uri, child->text))
                return(staticError(st, XP3_ERR_XQST0039, child,
                                   "duplicate parameter $%s", name));
        }
        if ((child->first != NULL) && (analyzeType(st, child->first) < 0))
            return(-1);
    }

    for (child = node->first; (child != NULL) && (ret == 0);
         child = child->next) {
        if (child->kind == XP3_AST_PARAM)
            ret = bindVariable(st, child);
        else if (child->next != NULL)
            ret = analyzeType(st, child);       /* the return type */
        else
            ret = analyzeNode(st, child);       /* the body */
    }
    st->scope.count = mark;
    return(ret);
}

/*
 * Functions
 */

/*
 * Turn a constructor function call T(arg) into (arg cast as T?), its
 * definition. node is the FUNCTION_CALL or ARROW and arg its only
 * child.
 */
static int
rewriteConstructor(xp3Analysis *st, xp3AstNode *node, int type) {
    xp3AstNode *seqType;
    xp3AstNode *target;

    seqType = xp3AstNewNode(st->ast, XP3_AST_SEQUENCE_TYPE, node->line,
                            node->column);
    target = xp3AstNewNode(st->ast, XP3_AST_ATOMIC_TYPE, node->line,
                           node->column);
    if ((seqType == NULL) || (target == NULL))
        return(staticNoMemory(st));
    seqType->op = XP3_OCC_OPTIONAL;
    target->op = type;
    target->prefix = node->prefix;
    target->uri = node->uri;
    target->text = node->text;
    xp3AstAppend(seqType, target);

    node->kind = XP3_AST_CAST_AS;
    node->op = 0;
    node->prefix = NULL;
    node->uri = NULL;
    node->text = NULL;
    xp3AstAppend(node, seqType);
    return(0);
}

/* Whether a call has an argument placeholder, making it a partial
 * function application. */
static int
hasPlaceholder(const xp3AstNode *node) {
    const xp3AstNode *child;

    for (child = node->first; child != NULL; child = child->next) {
        if (child->kind == XP3_AST_ARG_PLACEHOLDER)
            return(1);
    }
    return(0);
}

/*
 * Resolve the function of a FUNCTION_CALL, NAMED_FUNCTION_REF or
 * ARROW with a name [err:XPST0017].
 */
static int
resolveFunction(xp3Analysis *st, xp3AstNode *node, int arity) {
    const xp3FuncInfo *info;
    char name[XP3_NAME_BUF];
    int type;

    displayName(node, name, sizeof(name));
    if (resolveName(st, node, XP3_DEFAULT_FUNCTION) < 0)
        return(-1);
    info = xp3FuncLookup(node->uri, node->text, arity, st->level);
    if (info != NULL) {
        node->func = info;
        return(0);
    }

    type = schemaType(st, node);
    if ((type >= 0) && isCastTarget(st, type) && (arity == 1)) {
        if ((node->kind == XP3_AST_NAMED_FUNCTION_REF) ||
            hasPlaceholder(node))
            return(0);
        return(rewriteConstructor(st, node, type));
    }

    if ((type >= 0) && isCastTarget(st, type))
        return(staticError(st, XP3_ERR_XPST0017, node,
                           "the constructor function %s takes one "
                           "argument, not %d", name, arity));
    if (type >= 0)
        return(staticError(st, XP3_ERR_XPST0017, node,
                           "%s has no constructor function", name));
    if (xp3FuncNameExists(node->uri, node->text, st->level))
        return(staticError(st, XP3_ERR_XPST0017, node,
                           "function %s does not take %d argument%s",
                           name, arity, (arity == 1) ? "" : "s"));
    if (xp3FuncNameExists(node->uri, node->text, XP3_XPATH_31))
        return(staticError(st, XP3_ERR_XPST0017, node,
                           "function %s#%d is not available in %s", name,
                           arity, levelName(st->level)));
    return(staticError(st, XP3_ERR_XPST0017, node,
                       "unknown function %s#%d", name, arity));
}

/* name#arity, where the arity may not fit in an int */
static int
analyzeFunctionRef(xp3Analysis *st, xp3AstNode *node) {
    char name[XP3_NAME_BUF];

    if (node->op != XP3_ARITY_OVERFLOW)
        return(resolveFunction(st, node, node->op));
    /* Only a variadic function can have such an arity, and then the
     * number is the problem, not the function. */
    displayName(node, name, sizeof(name));
    if (resolveName(st, node, XP3_DEFAULT_FUNCTION) < 0)
        return(-1);
    if (xp3FuncLookup(node->uri, node->text, INT_MAX, st->level) != NULL)
        return(staticError(st, XP3_ERR_FOAR0002, node,
                           "the arity of %s is too large", name));
    return(staticError(st, XP3_ERR_XPST0017, node,
                       "function %s does not take that many arguments",
                       name));
}

/*
 * The walk
 */

static int
analyzeChildren(xp3Analysis *st, xp3AstNode *node) {
    xp3AstNode *child;

    for (child = node->first; child != NULL; child = child->next) {
        if (analyzeNode(st, child) < 0)
            return(-1);
    }
    return(0);
}

static int
analyzeStep(xp3Analysis *st, xp3AstNode *step) {
    xp3AstNode *child;

    if (step->op == XP3_AXIS_NAMESPACE)
        return(staticError(st, XP3_ERR_XPST0010, step,
                           "the namespace axis is not supported"));
    for (child = step->first; child != NULL; child = child->next) {
        int ret;

        if (child->kind == XP3_AST_NAME_TEST)
            ret = analyzeNameTest(st, child,
                                  (step->op == XP3_AXIS_ATTRIBUTE) ?
                                  XP3_DEFAULT_NONE : XP3_DEFAULT_ELEMENT);
        else if (child->kind == XP3_AST_KIND_TEST)
            ret = analyzeKindTest(st, child);
        else
            ret = analyzeNode(st, child);
        if (ret < 0)
            return(-1);
    }
    return(0);
}

static int
analyzeNode(xp3Analysis *st, xp3AstNode *node) {
    switch (node->kind) {
        case XP3_AST_VARREF:
            return(analyzeVarRef(st, node));
        case XP3_AST_FOR:
        case XP3_AST_LET:
        case XP3_AST_SOME:
        case XP3_AST_EVERY:
            return(analyzeClauses(st, node));
        case XP3_AST_INLINE_FUNCTION:
            return(analyzeInlineFunction(st, node));
        case XP3_AST_STEP:
            return(analyzeStep(st, node));
        case XP3_AST_FUNCTION_CALL:
            /* The arguments first, as a constructor call is rewritten */
            if (analyzeChildren(st, node) < 0)
                return(-1);
            return(resolveFunction(st, node,
                                   (int) xp3AstChildCount(node)));
        case XP3_AST_NAMED_FUNCTION_REF:
            return(analyzeFunctionRef(st, node));
        case XP3_AST_ARROW:
            if (analyzeChildren(st, node) < 0)
                return(-1);
            if (node->op != XP3_ARROW_NAME)
                return(0);
            /* The left side is the first argument */
            return(resolveFunction(st, node,
                                   (int) xp3AstChildCount(node)));
        case XP3_AST_CAST_AS:
        case XP3_AST_CASTABLE_AS:
            if (analyzeNode(st, node->first) < 0)
                return(-1);
            return(analyzeCastTarget(st, node->last));
        case XP3_AST_INSTANCE_OF:
        case XP3_AST_TREAT_AS:
            if (analyzeNode(st, node->first) < 0)
                return(-1);
            return(analyzeType(st, node->last));
        default:
            return(analyzeChildren(st, node));
    }
}

/*
 * Compiling
 */

/* Copy the declared variables in use into the tree. */
static int
saveVariables(xp3Analysis *st, xp3Expr *expr) {
    int i;

    expr->variableCount = st->declared.count;
    if (st->declared.count == 0)
        return(0);
    expr->variables = xp3AstAlloc(st->ast, (size_t) st->declared.count *
                                           sizeof(xp3ExprVariable));
    if (expr->variables == NULL)
        return(staticNoMemory(st));
    for (i = 0; i < st->declared.count; i++) {
        expr->variables[i].uri = st->declared.items[i].uri;
        expr->variables[i].localName = st->declared.items[i].localName;
        expr->variables[i].slot = st->declared.items[i].slot;
    }
    return(0);
}

/**
 * Compile an expression of a given length, which may contain NUL
 * bytes (they are syntax errors). xp3Compile() is this for a
 * NUL-terminated string.
 *
 * @param ctx     the static context, or NULL for a default one
 * @param expr    the expression in UTF-8, or NULL for an empty one
 * @param length  its length in bytes
 * @param error   where to store the error, or NULL
 * @returns the compiled expression, or NULL on error
 */
xp3Expr *
xp3CompileLength(const xp3StaticContext *ctx, const xmlChar *expr,
                 size_t length, xp3Error **error) {
    xp3Analysis st;
    xp3Expr *ret = NULL;
    xp3Ast *ast;

    if (error != NULL)
        *error = NULL;
    memset(&st, 0, sizeof(st));
    ast = xp3Parse(ctx, expr, length, &st.err);
    if (ast == NULL)
        goto done;

    st.ctx = ctx;
    st.level = (ctx != NULL) ? ctx->level : XP3_DEFAULT_LANGUAGE_LEVEL;
    st.ast = ast;
    if (analyzeNode(&st, ast->root) < 0)
        goto done;

    ret = xmlMalloc(sizeof(*ret));
    if (ret == NULL) {
        staticNoMemory(&st);
        goto done;
    }
    memset(ret, 0, sizeof(*ret));
    ret->level = st.level;
    ret->slotCount = st.slotCount;
    if (saveVariables(&st, ret) < 0) {
        xmlFree(ret);
        ret = NULL;
        goto done;
    }
    ret->ast = ast;
    ast = NULL;

done:
    xp3AstFree(ast);
    xmlFree(st.scope.items);
    xmlFree(st.declared.items);
    if (error != NULL)
        *error = st.err;
    else
        xp3ErrorFree(st.err);
    return(ret);
}

/**
 * Compile an expression: parse it and check it against the static
 * context, so that it can be evaluated any number of times.
 *
 * Names are resolved with the namespaces, default element namespace
 * and declared variables of the context, and functions are checked
 * against its language level. The compiled expression does not refer
 * to the context, which can be changed or freed afterwards.
 *
 * @param ctx    the static context, or NULL for a default one
 * @param expr   the expression, a NUL-terminated UTF-8 string
 * @param error  where to store the error, or NULL. It is set to NULL
 *               on success, and must be freed with xp3ErrorFree()
 *               otherwise.
 * @returns the compiled expression, to be freed with xp3FreeExpr(), or
 *          NULL on error
 */
xp3Expr *
xp3Compile(const xp3StaticContext *ctx, const xmlChar *expr,
           xp3Error **error) {
    return(xp3CompileLength(ctx, expr,
                            (expr != NULL) ? strlen((const char *) expr) : 0,
                            error));
}

/**
 * Free a compiled expression. NULL is ignored.
 *
 * @param expr  the expression
 */
void
xp3FreeExpr(xp3Expr *expr) {
    if (expr == NULL)
        return;
    xp3AstFree(expr->ast);
    xmlFree(expr);
}
