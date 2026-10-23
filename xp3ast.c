/*
 * xp3ast.c: abstract syntax tree and its arena
 *
 * All nodes and strings of a tree are allocated from a chain of
 * blocks owned by the tree, so a tree is freed with one call and a
 * partially built tree never leaks.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

#include "private/xp3ast.h"
#include "private/xp3types.h"
#include "private/xp3value.h"

/* Size of the first block, and the limit for doubling */
#define XP3_AST_BLOCK_MIN 4096
#define XP3_AST_BLOCK_MAX (1024 * 1024)

/* Arena allocations are aligned for any of these types. */
typedef union {
    long double ld;
    long long ll;
    double d;
    void *p;
    void (*fp)(void);
} xp3AstAlign;

#define XP3_AST_ALIGN sizeof(xp3AstAlign)

struct _xp3AstBlock {
    xp3AstBlock *next;
    size_t size;            /* usable bytes in data */
    size_t used;
    xp3AstAlign data[];
};

/* Names of the node kinds, indexed by xp3AstKind. */
static const char *const xp3AstKindNames[] = {
    [XP3_AST_NONE]                = "none",
    [XP3_AST_INTEGER]             = "integer",
    [XP3_AST_DECIMAL]             = "decimal",
    [XP3_AST_DOUBLE]              = "double",
    [XP3_AST_STRING]              = "string",
    [XP3_AST_VARREF]              = "var",
    [XP3_AST_CONTEXT_ITEM]        = "context",
    [XP3_AST_SEQUENCE]            = "sequence",
    [XP3_AST_EMPTY_SEQUENCE]      = "empty",
    [XP3_AST_FOR]                 = "for",
    [XP3_AST_LET]                 = "let",
    [XP3_AST_SOME]                = "some",
    [XP3_AST_EVERY]               = "every",
    [XP3_AST_BINDING]             = "bind",
    [XP3_AST_IF]                  = "if",
    [XP3_AST_BINARY]              = "binary",
    [XP3_AST_UNARY]               = "unary",
    [XP3_AST_INSTANCE_OF]         = "instance-of",
    [XP3_AST_TREAT_AS]            = "treat-as",
    [XP3_AST_CASTABLE_AS]         = "castable-as",
    [XP3_AST_CAST_AS]             = "cast-as",
    [XP3_AST_ARROW]               = "arrow",
    [XP3_AST_PATH]                = "path",
    [XP3_AST_STEP]                = "step",
    [XP3_AST_NAME_TEST]           = "name",
    [XP3_AST_KIND_TEST]           = "kind",
    [XP3_AST_PREDICATE]           = "predicate",
    [XP3_AST_FILTER]              = "filter",
    [XP3_AST_FUNCTION_CALL]       = "call",
    [XP3_AST_ARG_PLACEHOLDER]     = "placeholder",
    [XP3_AST_DYNAMIC_CALL]        = "dynamic-call",
    [XP3_AST_NAMED_FUNCTION_REF]  = "function-ref",
    [XP3_AST_INLINE_FUNCTION]     = "function",
    [XP3_AST_PARAM]               = "param",
    [XP3_AST_LOOKUP]              = "lookup",
    [XP3_AST_UNARY_LOOKUP]        = "unary-lookup",
    [XP3_AST_MAP]                 = "map",
    [XP3_AST_MAP_ENTRY]           = "entry",
    [XP3_AST_ARRAY_SQUARE]        = "array-square",
    [XP3_AST_ARRAY_CURLY]         = "array-curly",
    [XP3_AST_SEQUENCE_TYPE]       = "type",
    [XP3_AST_EMPTY_SEQUENCE_TYPE] = "empty-sequence",
    [XP3_AST_ANY_ITEM_TYPE]       = "item",
    [XP3_AST_ATOMIC_TYPE]         = "atomic",
    [XP3_AST_FUNCTION_TEST]       = "function-test",
    [XP3_AST_MAP_TEST]            = "map-test",
    [XP3_AST_ARRAY_TEST]          = "array-test",
    [XP3_AST_CONSTANT]            = "constant"
};

static const char *const xp3AstOpNames[] = {
    [XP3_OP_OR]         = "or",
    [XP3_OP_AND]        = "and",
    [XP3_OP_GEN_EQ]     = "=",
    [XP3_OP_GEN_NE]     = "!=",
    [XP3_OP_GEN_LT]     = "<",
    [XP3_OP_GEN_LE]     = "<=",
    [XP3_OP_GEN_GT]     = ">",
    [XP3_OP_GEN_GE]     = ">=",
    [XP3_OP_VAL_EQ]     = "eq",
    [XP3_OP_VAL_NE]     = "ne",
    [XP3_OP_VAL_LT]     = "lt",
    [XP3_OP_VAL_LE]     = "le",
    [XP3_OP_VAL_GT]     = "gt",
    [XP3_OP_VAL_GE]     = "ge",
    [XP3_OP_IS]         = "is",
    [XP3_OP_PRECEDES]   = "<<",
    [XP3_OP_FOLLOWS]    = ">>",
    [XP3_OP_CONCAT]     = "||",
    [XP3_OP_RANGE]      = "to",
    [XP3_OP_ADD]        = "+",
    [XP3_OP_SUB]        = "-",
    [XP3_OP_MUL]        = "*",
    [XP3_OP_DIV]        = "div",
    [XP3_OP_IDIV]       = "idiv",
    [XP3_OP_MOD]        = "mod",
    [XP3_OP_UNION]      = "union",
    [XP3_OP_INTERSECT]  = "intersect",
    [XP3_OP_EXCEPT]     = "except",
    [XP3_OP_SIMPLE_MAP] = "!"
};

static const char *const xp3AxisNames[] = {
    [XP3_AXIS_CHILD]              = "child",
    [XP3_AXIS_DESCENDANT]         = "descendant",
    [XP3_AXIS_ATTRIBUTE]          = "attribute",
    [XP3_AXIS_SELF]               = "self",
    [XP3_AXIS_DESCENDANT_OR_SELF] = "descendant-or-self",
    [XP3_AXIS_FOLLOWING_SIBLING]  = "following-sibling",
    [XP3_AXIS_FOLLOWING]          = "following",
    [XP3_AXIS_NAMESPACE]          = "namespace",
    [XP3_AXIS_PARENT]             = "parent",
    [XP3_AXIS_ANCESTOR]           = "ancestor",
    [XP3_AXIS_PRECEDING_SIBLING]  = "preceding-sibling",
    [XP3_AXIS_PRECEDING]          = "preceding",
    [XP3_AXIS_ANCESTOR_OR_SELF]   = "ancestor-or-self"
};

static const char *const xp3KindTestNames[] = {
    [XP3_KIND_ANY]              = "node",
    [XP3_KIND_TEXT]             = "text",
    [XP3_KIND_COMMENT]          = "comment",
    [XP3_KIND_NAMESPACE_NODE]   = "namespace-node",
    [XP3_KIND_PI]               = "processing-instruction",
    [XP3_KIND_DOCUMENT]         = "document-node",
    [XP3_KIND_ELEMENT]          = "element",
    [XP3_KIND_ATTRIBUTE]        = "attribute",
    [XP3_KIND_SCHEMA_ELEMENT]   = "schema-element",
    [XP3_KIND_SCHEMA_ATTRIBUTE] = "schema-attribute"
};

static const char *const xp3OccurrenceNames[] = {
    [XP3_OCC_ONE]          = "",
    [XP3_OCC_OPTIONAL]     = "?",
    [XP3_OCC_ZERO_OR_MORE] = "*",
    [XP3_OCC_ONE_OR_MORE]  = "+"
};

#define XP3_TABLE_SIZE(t) (sizeof(t) / sizeof((t)[0]))

/* Look up a name in a table, "?" if out of range. */
static const char *
xp3TableName(const char *const *table, size_t size, int index) {
    if ((index < 0) || ((size_t) index >= size) || (table[index] == NULL))
        return("?");
    return(table[index]);
}

/**
 * Get the name of a node kind, as used by xp3AstToString().
 *
 * @param kind  the node kind
 * @returns the name, for example "binary", or NULL for unknown kinds
 */
const char *
xp3AstKindName(xp3AstKind kind) {
    if ((unsigned) kind >= XP3_TABLE_SIZE(xp3AstKindNames))
        return(NULL);
    return(xp3AstKindNames[kind]);
}

/**
 * Allocate memory from the arena of a tree, aligned for any type. It is
 * freed with the tree.
 *
 * @param ast   the tree
 * @param size  number of bytes
 * @returns the memory, or NULL if memory allocation failed
 */
void *
xp3AstAlloc(xp3Ast *ast, size_t size) {
    xp3AstBlock *block = ast->blocks;
    void *ret;

    if (size > SIZE_MAX / 2)
        return(NULL);
    size = (size + XP3_AST_ALIGN - 1) / XP3_AST_ALIGN * XP3_AST_ALIGN;
    if (size == 0)
        size = XP3_AST_ALIGN;

    if ((block == NULL) || (block->size - block->used < size)) {
        size_t blockSize;

        blockSize = (block == NULL) ? XP3_AST_BLOCK_MIN : block->size * 2;
        if (blockSize > XP3_AST_BLOCK_MAX)
            blockSize = XP3_AST_BLOCK_MAX;
        if (blockSize < size)
            blockSize = size;

        block = xmlMalloc(offsetof(xp3AstBlock, data) + blockSize);
        if (block == NULL)
            return(NULL);
        block->size = blockSize;
        block->used = 0;
        block->next = ast->blocks;
        ast->blocks = block;
    }

    ret = (char *) block->data + block->used;
    block->used += size;
    return(ret);
}

/**
 * Create an empty tree.
 *
 * @returns the tree, or NULL if memory allocation failed
 */
xp3Ast *
xp3AstNew(void) {
    xp3Ast *ast;

    ast = xmlMalloc(sizeof(*ast));
    if (ast == NULL)
        return(NULL);
    memset(ast, 0, sizeof(*ast));
    return(ast);
}

/**
 * Free a tree with all its nodes and strings. NULL is ignored.
 *
 * @param ast  the tree
 */
void
xp3AstFree(xp3Ast *ast) {
    xp3AstBlock *block;

    if (ast == NULL)
        return;
    block = ast->blocks;
    while (block != NULL) {
        xp3AstBlock *next = block->next;

        xmlFree(block);
        block = next;
    }
    xmlFree(ast);
}

/**
 * Create a node in the tree's arena. All fields other than kind,
 * line and column are zero.
 *
 * @param ast     the tree
 * @param kind    the node kind
 * @param line    1-based line of the node's first token
 * @param column  1-based column of the node's first token
 * @returns the node, or NULL if memory allocation failed
 */
xp3AstNode *
xp3AstNewNode(xp3Ast *ast, xp3AstKind kind, int line, int column) {
    xp3AstNode *node;

    if (ast == NULL)
        return(NULL);
    node = xp3AstAlloc(ast, sizeof(*node));
    if (node == NULL)
        return(NULL);
    memset(node, 0, sizeof(*node));
    node->kind = kind;
    node->line = line;
    node->column = column;
    return(node);
}

/**
 * Copy a string into the tree's arena and terminate it.
 *
 * @param ast  the tree
 * @param str  the string, may contain no terminator
 * @param len  number of bytes to copy
 * @returns the copy, or NULL if memory allocation failed
 */
const xmlChar *
xp3AstStrndup(xp3Ast *ast, const xmlChar *str, size_t len) {
    xmlChar *copy;

    if ((ast == NULL) || ((str == NULL) && (len > 0)))
        return(NULL);
    copy = xp3AstAlloc(ast, len + 1);
    if (copy == NULL)
        return(NULL);
    if (len > 0)
        memcpy(copy, str, len);
    copy[len] = 0;
    return(copy);
}

/**
 * Append a child to a node. NULL arguments are ignored.
 *
 * @param parent  the parent
 * @param child   the new last child
 */
void
xp3AstAppend(xp3AstNode *parent, xp3AstNode *child) {
    if ((parent == NULL) || (child == NULL))
        return;
    child->next = NULL;
    if (parent->last != NULL)
        parent->last->next = child;
    else
        parent->first = child;
    parent->last = child;
}

/**
 * Count the children of a node.
 *
 * @param node  the node
 * @returns the number of children, 0 for NULL
 */
size_t
xp3AstChildCount(const xp3AstNode *node) {
    const xp3AstNode *child;
    size_t count = 0;

    if (node == NULL)
        return(0);
    for (child = node->first; child != NULL; child = child->next)
        count++;
    return(count);
}

/*
 * Growing string buffer for the printer. After a failed allocation
 * it ignores further writes, and xp3AstToString() returns NULL.
 */
typedef struct {
    xmlChar *data;
    size_t len;
    size_t cap;
    int failed;
} xp3AstBuf;

static void
bufAdd(xp3AstBuf *buf, const char *str, size_t len) {
    if (buf->failed)
        return;
    if (len >= buf->cap - buf->len) {
        size_t cap = (buf->cap == 0) ? 64 : buf->cap;
        xmlChar *data;

        while (len >= cap - buf->len) {
            if (cap > SIZE_MAX / 2) {
                buf->failed = 1;
                return;
            }
            cap *= 2;
        }
        data = xmlRealloc(buf->data, cap);
        if (data == NULL) {
            buf->failed = 1;
            return;
        }
        buf->data = data;
        buf->cap = cap;
    }
    memcpy(buf->data + buf->len, str, len);
    buf->len += len;
    buf->data[buf->len] = 0;
}

static void
bufStr(xp3AstBuf *buf, const char *str) {
    bufAdd(buf, str, strlen(str));
}

static void
bufXmlStr(xp3AstBuf *buf, const xmlChar *str) {
    if (str != NULL)
        bufStr(buf, (const char *) str);
}

/* Q{uri}local, prefix:local or local */
static void
bufName(xp3AstBuf *buf, const xp3AstNode *node, const char *local) {
    if (node->uri != NULL) {
        bufStr(buf, "Q{");
        bufXmlStr(buf, node->uri);
        bufStr(buf, "}");
    } else if (node->prefix != NULL) {
        bufXmlStr(buf, node->prefix);
        bufStr(buf, ":");
    }
    if (local != NULL)
        bufStr(buf, local);
    else
        bufXmlStr(buf, node->text);
}

/* A string literal value, quoted, with " and \ escaped */
static void
bufQuoted(xp3AstBuf *buf, const xmlChar *str) {
    bufStr(buf, "\"");
    if (str != NULL) {
        const xmlChar *cur;

        for (cur = str; *cur != 0; cur++) {
            if ((*cur == '"') || (*cur == '\\'))
                bufStr(buf, "\\");
            bufAdd(buf, (const char *) cur, 1);
        }
    }
    bufStr(buf, "\"");
}

/* The items of a constant as type:value, a range as integer:a..b */
static void
bufValue(xp3AstBuf *buf, const xp3Value *value) {
    char num[64];
    size_t i;

    if (value == NULL)
        return;
    if (value->shape == XP3_SEQ_RANGE) {
        snprintf(num, sizeof(num), " integer:%lld..%lld",
                 (long long) value->u.range.first,
                 (long long) value->u.range.last);
        bufStr(buf, num);
        return;
    }
    for (i = 0; i < xp3ValueCount(value); i++) {
        xp3Item item;
        xmlChar *str;

        if ((xp3ValueItemAt(value, i, &item) <= 0) ||
            (item.kind != XP3_ITEM_ATOMIC)) {
            buf->failed = 1;
            return;
        }
        str = xp3AtomicToString(item.u.atomic);
        bufStr(buf, " ");
        bufXmlStr(buf, xp3TypeLocalName(item.u.atomic->type));
        bufStr(buf, ":");
        if (str == NULL)
            buf->failed = 1;
        bufXmlStr(buf, str);
        xmlFree(str);
        xp3ItemUnref(item);
    }
}

static void
bufNode(xp3AstBuf *buf, const xp3AstNode *node) {
    const xp3AstNode *child;
    char num[32];

    bufStr(buf, "(");
    bufStr(buf, xp3TableName(xp3AstKindNames,
                             XP3_TABLE_SIZE(xp3AstKindNames), node->kind));

    switch (node->kind) {
        case XP3_AST_INTEGER:
        case XP3_AST_DECIMAL:
        case XP3_AST_DOUBLE:
            bufStr(buf, " ");
            bufXmlStr(buf, node->text);
            break;
        case XP3_AST_STRING:
            bufStr(buf, " ");
            bufQuoted(buf, node->text);
            break;
        case XP3_AST_VARREF:
        case XP3_AST_BINDING:
        case XP3_AST_PARAM:
        case XP3_AST_FUNCTION_CALL:
        case XP3_AST_ATOMIC_TYPE:
            bufStr(buf, " ");
            bufName(buf, node, NULL);
            break;
        case XP3_AST_NAMED_FUNCTION_REF:
            bufStr(buf, " ");
            bufName(buf, node, NULL);
            if (node->op == XP3_ARITY_OVERFLOW) {
                bufStr(buf, "#overflow");
            } else {
                snprintf(num, sizeof(num), "#%d", node->op);
                bufStr(buf, num);
            }
            break;
        case XP3_AST_NAME_TEST:
            bufStr(buf, " ");
            switch (node->op) {
                case XP3_NAMETEST_ANY:
                    bufStr(buf, "*");
                    break;
                case XP3_NAMETEST_ANY_LOCAL:
                    bufName(buf, node, "*");
                    break;
                case XP3_NAMETEST_ANY_NS:
                    bufStr(buf, "*:");
                    bufXmlStr(buf, node->text);
                    break;
                default:
                    bufName(buf, node, NULL);
                    break;
            }
            break;
        case XP3_AST_BINARY:
        case XP3_AST_UNARY:
            bufStr(buf, " ");
            bufStr(buf, xp3TableName(xp3AstOpNames,
                                     XP3_TABLE_SIZE(xp3AstOpNames),
                                     node->op));
            break;
        case XP3_AST_STEP:
            bufStr(buf, " ");
            bufStr(buf, xp3TableName(xp3AxisNames,
                                     XP3_TABLE_SIZE(xp3AxisNames),
                                     node->op));
            break;
        case XP3_AST_PATH:
            if (node->op == XP3_PATH_ROOT)
                bufStr(buf, " root");
            break;
        case XP3_AST_KIND_TEST:
            bufStr(buf, " ");
            bufStr(buf, xp3TableName(xp3KindTestNames,
                                     XP3_TABLE_SIZE(xp3KindTestNames),
                                     node->op));
            if (node->text != NULL) {
                bufStr(buf, " ");
                bufXmlStr(buf, node->text);
            }
            break;
        case XP3_AST_LOOKUP:
        case XP3_AST_UNARY_LOOKUP:
            if ((node->op == XP3_LOOKUP_NAME) ||
                (node->op == XP3_LOOKUP_INTEGER)) {
                bufStr(buf, " ");
                bufXmlStr(buf, node->text);
            } else if (node->op == XP3_LOOKUP_WILDCARD) {
                bufStr(buf, " *");
            }
            break;
        case XP3_AST_ARROW:
            if (node->op == XP3_ARROW_NAME) {
                bufStr(buf, " ");
                bufName(buf, node, NULL);
            }
            break;
        case XP3_AST_SEQUENCE_TYPE:
            if (node->op != XP3_OCC_ONE) {
                bufStr(buf, " ");
                bufStr(buf, xp3TableName(xp3OccurrenceNames,
                                         XP3_TABLE_SIZE(xp3OccurrenceNames),
                                         node->op));
            }
            break;
        case XP3_AST_FUNCTION_TEST:
        case XP3_AST_MAP_TEST:
        case XP3_AST_ARRAY_TEST:
            if (node->op == 0)
                bufStr(buf, " *");
            break;
        case XP3_AST_CONSTANT:
            bufValue(buf, node->value);
            break;
        default:
            break;
    }
    if (node->flags & XP3_AST_SORTED)
        bufStr(buf, " sorted");

    for (child = node->first; child != NULL; child = child->next) {
        bufStr(buf, " ");
        bufNode(buf, child);
    }
    bufStr(buf, ")");
}

/**
 * Write a node and its children as an S-expression, for tests and
 * debugging. For example "a[1] + 2" gives
 * (binary + (path (step child (name a) (predicate (integer 1))))
 * (integer 2)).
 *
 * @param node  the node
 * @returns the string, to be freed with xmlFree(), or NULL if node is
 *          NULL or memory allocation failed
 */
xmlChar *
xp3AstToString(const xp3AstNode *node) {
    xp3AstBuf buf;

    if (node == NULL)
        return(NULL);
    memset(&buf, 0, sizeof(buf));
    bufNode(&buf, node);
    if (buf.failed) {
        xmlFree(buf.data);
        return(NULL);
    }
    return(buf.data);
}
