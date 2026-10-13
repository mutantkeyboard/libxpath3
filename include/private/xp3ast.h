/**
 * @file
 *
 * @brief Abstract syntax tree for XPath 3.1 expressions (internal)
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XP3AST_H
#define XP3_XP3AST_H

#include <stddef.h>

#include <libxml/xmlstring.h>

/*
 * Node kinds. The comment says which fields a kind uses. "name" means
 * the fields prefix, uri and text (the local name): at most one of
 * prefix and uri is set.
 */
typedef enum {
    XP3_AST_NONE = 0,

    /* Literals and simple expressions */
    XP3_AST_INTEGER,            /* text: digits as written */
    XP3_AST_DECIMAL,            /* text: as written */
    XP3_AST_DOUBLE,             /* text: as written */
    XP3_AST_STRING,             /* text: the value, quotes removed and
                                 * doubled quotes decoded */
    XP3_AST_VARREF,             /* $name: name */
    XP3_AST_CONTEXT_ITEM,       /* . */
    XP3_AST_SEQUENCE,           /* a, b, c: children */
    XP3_AST_EMPTY_SEQUENCE,     /* () */

    /* Control */
    XP3_AST_FOR,                /* children: BINDING..., return expression */
    XP3_AST_LET,                /* children: BINDING..., return expression */
    XP3_AST_SOME,               /* children: BINDING..., satisfies expr */
    XP3_AST_EVERY,              /* children: BINDING..., satisfies expr */
    XP3_AST_BINDING,            /* $name in/:= expr: name, child: expr */
    XP3_AST_IF,                 /* children: condition, then, else */

    /* Operators */
    XP3_AST_BINARY,             /* op: xp3AstOp, children: left, right */
    XP3_AST_UNARY,              /* op: XP3_OP_ADD or XP3_OP_SUB, child */
    XP3_AST_INSTANCE_OF,        /* children: expr, SEQUENCE_TYPE */
    XP3_AST_TREAT_AS,           /* children: expr, SEQUENCE_TYPE */
    XP3_AST_CASTABLE_AS,        /* children: expr, SEQUENCE_TYPE */
    XP3_AST_CAST_AS,            /* children: expr, SEQUENCE_TYPE */
    XP3_AST_ARROW,              /* op: xp3ArrowKind. XP3_ARROW_NAME: name,
                                 * children: expr, args...
                                 * XP3_ARROW_EXPR: children: expr,
                                 * function expr, args... */

    /* Paths */
    XP3_AST_PATH,               /* op: xp3PathKind, children: steps.
                                 * "//" is stored as an explicit
                                 * descendant-or-self::node() step */
    XP3_AST_STEP,               /* op: xp3Axis, children: node test,
                                 * PREDICATE... */
    XP3_AST_NAME_TEST,          /* op: xp3NameTestKind, name */
    XP3_AST_KIND_TEST,          /* op: xp3KindTestKind, see there */
    XP3_AST_PREDICATE,          /* child: expr */
    XP3_AST_FILTER,             /* children: primary expr, then PREDICATE,
                                 * DYNAMIC_CALL and LOOKUP in order */

    /* Functions */
    XP3_AST_FUNCTION_CALL,      /* name, children: arguments */
    XP3_AST_ARG_PLACEHOLDER,    /* ? as an argument */
    XP3_AST_DYNAMIC_CALL,       /* postfix (args): children: arguments */
    XP3_AST_NAMED_FUNCTION_REF, /* name#arity: name, op: arity or
                                 * XP3_ARITY_OVERFLOW */
    XP3_AST_INLINE_FUNCTION,    /* children: PARAM..., optional
                                 * SEQUENCE_TYPE (return type), body */
    XP3_AST_PARAM,              /* $name: name, optional child:
                                 * SEQUENCE_TYPE */

    /* Lookups */
    XP3_AST_LOOKUP,             /* postfix ?key: op: xp3LookupKind,
                                 * text (NAME, INTEGER) or child (EXPR) */
    XP3_AST_UNARY_LOOKUP,       /* ?key without a left side, as LOOKUP */

    /* Constructors */
    XP3_AST_MAP,                /* children: MAP_ENTRY... */
    XP3_AST_MAP_ENTRY,          /* children: key, value */
    XP3_AST_ARRAY_SQUARE,       /* [a, b]: children: members */
    XP3_AST_ARRAY_CURLY,        /* array { expr }: optional child */

    /* Types */
    XP3_AST_SEQUENCE_TYPE,      /* op: xp3Occurrence, child: item type */
    XP3_AST_EMPTY_SEQUENCE_TYPE,/* empty-sequence() */
    XP3_AST_ANY_ITEM_TYPE,      /* item() */
    XP3_AST_ATOMIC_TYPE,        /* name */
    XP3_AST_FUNCTION_TEST,      /* op: 0 for function(*), 1 typed:
                                 * children: parameter types, return type */
    XP3_AST_MAP_TEST,           /* op: 0 for map(*), 1 typed:
                                 * children: key type, value type */
    XP3_AST_ARRAY_TEST          /* op: 0 for array(*), 1 typed:
                                 * child: member type */
} xp3AstKind;

/*
 * Arity of a NAMED_FUNCTION_REF that does not fit in an int, as in
 * concat#340282366920938463463374607431768211456. Variadic functions
 * such as fn:concat accept any arity, so this is not a syntax error:
 * static analysis raises FOAR0002 for it, as QT3 expects.
 */
#define XP3_ARITY_OVERFLOW (-1)

/* Operators of BINARY and UNARY nodes */
typedef enum {
    XP3_OP_OR = 0,
    XP3_OP_AND,
    XP3_OP_GEN_EQ,      /* = */
    XP3_OP_GEN_NE,      /* != */
    XP3_OP_GEN_LT,      /* < */
    XP3_OP_GEN_LE,      /* <= */
    XP3_OP_GEN_GT,      /* > */
    XP3_OP_GEN_GE,      /* >= */
    XP3_OP_VAL_EQ,      /* eq */
    XP3_OP_VAL_NE,      /* ne */
    XP3_OP_VAL_LT,      /* lt */
    XP3_OP_VAL_LE,      /* le */
    XP3_OP_VAL_GT,      /* gt */
    XP3_OP_VAL_GE,      /* ge */
    XP3_OP_IS,          /* is */
    XP3_OP_PRECEDES,    /* << */
    XP3_OP_FOLLOWS,     /* >> */
    XP3_OP_CONCAT,      /* || */
    XP3_OP_RANGE,       /* to */
    XP3_OP_ADD,         /* + */
    XP3_OP_SUB,         /* - */
    XP3_OP_MUL,         /* * */
    XP3_OP_DIV,         /* div */
    XP3_OP_IDIV,        /* idiv */
    XP3_OP_MOD,         /* mod */
    XP3_OP_UNION,       /* union, | */
    XP3_OP_INTERSECT,   /* intersect */
    XP3_OP_EXCEPT,      /* except */
    XP3_OP_SIMPLE_MAP   /* ! */
} xp3AstOp;

/* Axes of STEP nodes */
typedef enum {
    XP3_AXIS_CHILD = 0,
    XP3_AXIS_DESCENDANT,
    XP3_AXIS_ATTRIBUTE,
    XP3_AXIS_SELF,
    XP3_AXIS_DESCENDANT_OR_SELF,
    XP3_AXIS_FOLLOWING_SIBLING,
    XP3_AXIS_FOLLOWING,
    XP3_AXIS_NAMESPACE,
    XP3_AXIS_PARENT,
    XP3_AXIS_ANCESTOR,
    XP3_AXIS_PRECEDING_SIBLING,
    XP3_AXIS_PRECEDING,
    XP3_AXIS_ANCESTOR_OR_SELF
} xp3Axis;

/* PATH nodes */
typedef enum {
    XP3_PATH_RELATIVE = 0,      /* a/b */
    XP3_PATH_ROOT               /* /a/b, //a/b, / */
} xp3PathKind;

/* NAME_TEST nodes */
typedef enum {
    XP3_NAMETEST_NAME = 0,      /* local, p:local, Q{uri}local */
    XP3_NAMETEST_ANY,           /* * */
    XP3_NAMETEST_ANY_LOCAL,     /* p:*, Q{uri}*: prefix or uri set */
    XP3_NAMETEST_ANY_NS         /* *:local: text set */
} xp3NameTestKind;

/*
 * KIND_TEST nodes. Optional children:
 *   DOCUMENT          an ELEMENT or SCHEMA_ELEMENT kind test
 *   ELEMENT/ATTRIBUTE a NAME_TEST, then a SEQUENCE_TYPE with the type
 *                     annotation (XP3_OCC_OPTIONAL for "type?")
 *   SCHEMA_ELEMENT/   a NAME_TEST
 *   SCHEMA_ATTRIBUTE
 *   PI                the target in text
 */
typedef enum {
    XP3_KIND_ANY = 0,           /* node() */
    XP3_KIND_TEXT,              /* text() */
    XP3_KIND_COMMENT,           /* comment() */
    XP3_KIND_NAMESPACE_NODE,    /* namespace-node() */
    XP3_KIND_PI,                /* processing-instruction() */
    XP3_KIND_DOCUMENT,          /* document-node() */
    XP3_KIND_ELEMENT,           /* element() */
    XP3_KIND_ATTRIBUTE,         /* attribute() */
    XP3_KIND_SCHEMA_ELEMENT,    /* schema-element() */
    XP3_KIND_SCHEMA_ATTRIBUTE   /* schema-attribute() */
} xp3KindTestKind;

/* LOOKUP and UNARY_LOOKUP nodes */
typedef enum {
    XP3_LOOKUP_NAME = 0,        /* ?name: text */
    XP3_LOOKUP_INTEGER,         /* ?3: text */
    XP3_LOOKUP_EXPR,            /* ?(expr): child */
    XP3_LOOKUP_WILDCARD         /* ?* */
} xp3LookupKind;

/* ARROW nodes */
typedef enum {
    XP3_ARROW_NAME = 0,         /* => f(...) */
    XP3_ARROW_EXPR              /* => $f(...), => (expr)(...) */
} xp3ArrowKind;

/* SEQUENCE_TYPE nodes */
typedef enum {
    XP3_OCC_ONE = 0,            /* exactly one */
    XP3_OCC_OPTIONAL,           /* ? */
    XP3_OCC_ZERO_OR_MORE,       /* * */
    XP3_OCC_ONE_OR_MORE         /* + */
} xp3Occurrence;

typedef struct _xp3AstNode xp3AstNode;
typedef struct _xp3AstBlock xp3AstBlock;
typedef struct _xp3Ast xp3Ast;

/*
 * A node. Nodes and their strings live in the tree's arena and are
 * freed together with xp3AstFree().
 */
struct _xp3AstNode {
    xp3AstKind kind;
    int line;               /* 1-based, from the first token */
    int column;             /* 1-based, in characters */
    int op;                 /* meaning depends on the kind */
    const xmlChar *text;    /* literal text or local name, or NULL */
    const xmlChar *prefix;  /* QName prefix, or NULL */
    const xmlChar *uri;     /* URI of a Q{uri} name, or NULL */
    xp3AstNode *first;      /* first child */
    xp3AstNode *last;       /* last child */
    xp3AstNode *next;       /* next sibling */
};

/*
 * A tree: its root and the arena that holds all its memory.
 */
struct _xp3Ast {
    xp3AstNode *root;
    xp3AstBlock *blocks;    /* most recent block first */
};

xp3Ast *
xp3AstNew(void);

void
xp3AstFree(xp3Ast *ast);

xp3AstNode *
xp3AstNewNode(xp3Ast *ast, xp3AstKind kind, int line, int column);

const xmlChar *
xp3AstStrndup(xp3Ast *ast, const xmlChar *str, size_t len);

void
xp3AstAppend(xp3AstNode *parent, xp3AstNode *child);

size_t
xp3AstChildCount(const xp3AstNode *node);

xmlChar *
xp3AstToString(const xp3AstNode *node);

const char *
xp3AstKindName(xp3AstKind kind);

#endif /* XP3_XP3AST_H */
