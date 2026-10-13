/*
 * xp3parser.c: recursive descent parser for XPath expressions
 *
 * There is one function per grammar level, from Expr down to
 * PrimaryExpr, in the order of the precedence table in XPath 3.1
 * (A.4). Each function returns the node it built, or NULL after the
 * first error has been stored in the parser. All nodes live in the
 * tree's arena, so nothing has to be freed on the way out.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <libxml/xmlstring.h>

#include "private/xp3ast.h"
#include "private/xp3context.h"
#include "private/xp3error.h"
#include "private/xp3lexer.h"
#include "private/xp3parser.h"

/*
 * Nesting cost of one ExprSingle. A parenthesized expression or a
 * predicate goes through about 20 functions before it recurses, so it
 * counts for more than one level of the limit, as in libxml2. Unary
 * signs and chained binary operators cost 1 each, because they make the
 * tree deeper without recursing here.
 */
#define XP3_PARSE_EXPR_COST 10

/* Longest token text quoted in an error message, in bytes */
#define XP3_PARSE_MAX_QUOTE 32

typedef struct {
    xp3Lexer lex;               /* positioned after tok */
    xp3Token tok;               /* the current token */
    xp3Ast *ast;
    xp3LanguageLevel level;
    int depth;                  /* nesting so far, see parserEnter() */
    int maxDepth;
    xp3Error *err;              /* the first error */
} xp3Parser;

typedef xp3AstNode *(*xp3ParseFunc)(xp3Parser *p);

/* Operator of a binary level at the current token, or -1 */
typedef int (*xp3MatchFunc)(const xp3Parser *p);

static const struct {
    const char *name;
    xp3Axis axis;
} xp3Axes[] = {
    { "ancestor",           XP3_AXIS_ANCESTOR },
    { "ancestor-or-self",   XP3_AXIS_ANCESTOR_OR_SELF },
    { "attribute",          XP3_AXIS_ATTRIBUTE },
    { "child",              XP3_AXIS_CHILD },
    { "descendant",         XP3_AXIS_DESCENDANT },
    { "descendant-or-self", XP3_AXIS_DESCENDANT_OR_SELF },
    { "following",          XP3_AXIS_FOLLOWING },
    { "following-sibling",  XP3_AXIS_FOLLOWING_SIBLING },
    { "namespace",          XP3_AXIS_NAMESPACE },
    { "parent",             XP3_AXIS_PARENT },
    { "preceding",          XP3_AXIS_PRECEDING },
    { "preceding-sibling",  XP3_AXIS_PRECEDING_SIBLING },
    { "self",               XP3_AXIS_SELF }
};

/* Kind tests, and the language level that introduced them */
static const struct {
    const char *name;
    xp3KindTestKind kind;
    xp3LanguageLevel level;
} xp3KindTests[] = {
    { "attribute",              XP3_KIND_ATTRIBUTE,        XP3_XPATH_20 },
    { "comment",                XP3_KIND_COMMENT,          XP3_XPATH_20 },
    { "document-node",          XP3_KIND_DOCUMENT,         XP3_XPATH_20 },
    { "element",                XP3_KIND_ELEMENT,          XP3_XPATH_20 },
    { "namespace-node",         XP3_KIND_NAMESPACE_NODE,   XP3_XPATH_30 },
    { "node",                   XP3_KIND_ANY,              XP3_XPATH_20 },
    { "processing-instruction", XP3_KIND_PI,               XP3_XPATH_20 },
    { "schema-attribute",       XP3_KIND_SCHEMA_ATTRIBUTE, XP3_XPATH_20 },
    { "schema-element",         XP3_KIND_SCHEMA_ELEMENT,   XP3_XPATH_20 },
    { "text",                   XP3_KIND_TEXT,             XP3_XPATH_20 }
};

/*
 * Names that cannot be unprefixed function names, because the grammar
 * uses them before "(" (XPath 3.1 A.3), and the language level that
 * reserved them.
 */
static const struct {
    const char *name;
    xp3LanguageLevel level;
} xp3ReservedNames[] = {
    { "array",                  XP3_XPATH_31 },
    { "attribute",              XP3_XPATH_20 },
    { "comment",                XP3_XPATH_20 },
    { "document-node",          XP3_XPATH_20 },
    { "element",                XP3_XPATH_20 },
    { "empty-sequence",         XP3_XPATH_20 },
    { "function",               XP3_XPATH_30 },
    { "if",                     XP3_XPATH_20 },
    { "item",                   XP3_XPATH_20 },
    { "map",                    XP3_XPATH_31 },
    { "namespace-node",         XP3_XPATH_30 },
    { "node",                   XP3_XPATH_20 },
    { "processing-instruction", XP3_XPATH_20 },
    { "schema-attribute",       XP3_XPATH_20 },
    { "schema-element",         XP3_XPATH_20 },
    { "switch",                 XP3_XPATH_30 },
    { "text",                   XP3_XPATH_20 },
    { "typeswitch",             XP3_XPATH_20 }
};

#define XP3_TABLE_SIZE(t) (sizeof(t) / sizeof((t)[0]))

static xp3AstNode *parseExpr(xp3Parser *p);
static xp3AstNode *parseExprSingle(xp3Parser *p);

/*
 * Tokens and errors
 */

/* Whether tok is the NCName name. */
static int
tokIsName(const xp3Parser *p, const xp3Token *tok, const char *name) {
    size_t len = strlen(name);

    return((tok->kind == XP3_TOK_NCNAME) && (tok->length == len) &&
           (memcmp(p->lex.input + tok->start, name, len) == 0));
}

/* Whether tok is an EQName: NCName, prefix:local or Q{uri}local. */
static int
tokIsEQName(const xp3Token *tok) {
    return((tok->kind == XP3_TOK_NCNAME) || (tok->kind == XP3_TOK_QNAME) ||
           (tok->kind == XP3_TOK_EQNAME));
}

/*
 * Quote a token for an error message: 'text', shortened at a character
 * boundary if it is long, or "end of expression".
 */
static void
parserQuote(const xp3Parser *p, const xp3Token *tok, char *buf,
            size_t size) {
    const xmlChar *text = p->lex.input + tok->start;
    size_t len = tok->length;
    const char *more = "";

    if (tok->kind == XP3_TOK_EOF) {
        snprintf(buf, size, "end of expression");
        return;
    }
    if (len > XP3_PARSE_MAX_QUOTE) {
        len = XP3_PARSE_MAX_QUOTE;
        while ((len > 0) && ((text[len] & 0xC0) == 0x80))
            len--;
        more = "...";
    }
    snprintf(buf, size, "'%.*s%s'", (int) len, (const char *) text, more);
}

/*
 * Store an error at a token, unless an error is stored already.
 * Returns NULL, so callers can return its result.
 */
static xp3AstNode *
parserError(xp3Parser *p, xp3ErrCode code, const xp3Token *tok,
            const char *format, ...) {
    va_list args;

    if (p->err == NULL) {
        va_start(args, format);
        p->err = xp3ErrorNewV(code, tok->line, tok->column, format, args);
        va_end(args);
    }
    return(NULL);
}

/* Syntax error at the current token: "expected X, found Y". */
static xp3AstNode *
parserExpected(xp3Parser *p, const char *what) {
    char found[48];

    parserQuote(p, &p->tok, found, sizeof(found));
    return(parserError(p, XP3_ERR_XPST0003, &p->tok,
                       "expected %s, found %s", what, found));
}

static xp3AstNode *
parserNoMemory(xp3Parser *p) {
    if (p->err == NULL)
        p->err = xp3ErrorNoMemory();
    return(NULL);
}

/* Keep a lexer error, or free it if an error is stored already. */
static void
parserKeepError(xp3Parser *p, xp3Error *err) {
    if (p->err == NULL)
        p->err = err;
    else
        xp3ErrorFree(err);
}

/* Move to the next token. */
static int
parserAdvance(xp3Parser *p) {
    xp3Error *err = NULL;

    if (xp3LexerNext(&p->lex, &p->tok, &err) < 0) {
        parserKeepError(p, err);
        return(-1);
    }
    return(0);
}

/* Kind of the token after the current one, or -1 on a lexical error. */
static int
parserPeekKind(xp3Parser *p) {
    xp3Error *err = NULL;
    xp3Token next;

    if (xp3LexerPeek(&p->lex, &next, &err) < 0) {
        parserKeepError(p, err);
        return(-1);
    }
    return((int) next.kind);
}

/* Check that the current token has the given kind and move past it. */
static int
parserExpect(xp3Parser *p, xp3TokenKind kind, const char *what) {
    if (p->tok.kind != kind) {
        parserExpected(p, what);
        return(-1);
    }
    return(parserAdvance(p));
}

/* Check that a construct is available at the language level. */
static int
parserRequire(xp3Parser *p, const xp3Token *tok, xp3LanguageLevel level,
              const char *what) {
    if (p->level >= level)
        return(0);
    parserError(p, XP3_ERR_XPST0003, tok, "%s require XPath %d.%d", what,
                (int) level / 10, (int) level % 10);
    return(-1);
}

/*
 * Count one more level of nesting. Returns -1 with a LIMIT error if
 * the limit is reached. The caller subtracts the cost when done.
 */
static int
parserEnter(xp3Parser *p, int cost) {
    if (p->depth >= p->maxDepth) {
        parserError(p, XP3_ERR_LIMIT, &p->tok,
                    "expression nested too deeply");
        return(-1);
    }
    p->depth += cost;
    return(0);
}

/*
 * Nodes
 */

static xp3AstNode *
parserNode(xp3Parser *p, xp3AstKind kind, int op, int line, int column) {
    xp3AstNode *node = xp3AstNewNode(p->ast, kind, line, column);

    if (node == NULL)
        return(parserNoMemory(p));
    node->op = op;
    return(node);
}

/* A node at the position of a token */
static xp3AstNode *
parserTokNode(xp3Parser *p, xp3AstKind kind, int op, const xp3Token *tok) {
    return(parserNode(p, kind, op, tok->line, tok->column));
}

/* A node at the position of its first child, which is appended. */
static xp3AstNode *
parserWrap(xp3Parser *p, xp3AstKind kind, int op, xp3AstNode *first) {
    xp3AstNode *node = parserNode(p, kind, op, first->line, first->column);

    if (node != NULL)
        xp3AstAppend(node, first);
    return(node);
}

static const xmlChar *
parserStrndup(xp3Parser *p, const xmlChar *str, size_t len) {
    const xmlChar *copy = xp3AstStrndup(p->ast, str, len);

    if (copy == NULL)
        parserNoMemory(p);
    return(copy);
}

/* Value of a string literal token: quotes removed, doubled quotes
 * decoded. */
static const xmlChar *
parserStringValue(xp3Parser *p, const xp3Token *tok) {
    const xmlChar *text = p->lex.input + tok->start;
    xmlChar quote = text[0];
    xmlChar *value;
    size_t i;
    size_t j;

    /* The copy is in the arena and not shared yet, so it is decoded in
     * place. The lexer guarantees that quotes inside come in pairs. */
    value = (xmlChar *) parserStrndup(p, text + 1, tok->length - 2);
    if (value == NULL)
        return(NULL);
    for (i = 0, j = 0; value[i] != 0; i++, j++) {
        value[j] = value[i];
        if (value[i] == quote)
            i++;
    }
    value[j] = 0;
    return(value);
}

/*
 * Store the name of an NCNAME, QNAME or EQNAME token in a node: the
 * local name in text, and the prefix or the URI.
 */
static int
parserSetName(xp3Parser *p, xp3AstNode *node, const xp3Token *tok) {
    const xmlChar *text = p->lex.input + tok->start;
    size_t local = 0;

    if (tok->kind == XP3_TOK_QNAME) {
        node->prefix = parserStrndup(p, text, tok->sep);
        if (node->prefix == NULL)
            return(-1);
        local = tok->sep + 1;
    } else if (tok->kind == XP3_TOK_EQNAME) {
        if (parserRequire(p, tok, XP3_XPATH_30, "Q{} names") < 0)
            return(-1);
        node->uri = parserStrndup(p, text + 2, tok->sep - 2);
        if (node->uri == NULL)
            return(-1);
        local = tok->sep + 1;
    }
    node->text = parserStrndup(p, text + local, tok->length - local);
    return((node->text == NULL) ? -1 : 0);
}

/*
 * Name lookups
 */

static int
lookupAxis(const xp3Parser *p, const xp3Token *tok) {
    size_t i;

    for (i = 0; i < XP3_TABLE_SIZE(xp3Axes); i++) {
        if (tokIsName(p, tok, xp3Axes[i].name))
            return((int) xp3Axes[i].axis);
    }
    return(-1);
}

/* The kind test that tok names at the parser's level, or -1. */
static int
lookupKindTest(const xp3Parser *p, const xp3Token *tok) {
    size_t i;

    for (i = 0; i < XP3_TABLE_SIZE(xp3KindTests); i++) {
        if ((p->level >= xp3KindTests[i].level) &&
            (tokIsName(p, tok, xp3KindTests[i].name)))
            return((int) xp3KindTests[i].kind);
    }
    return(-1);
}

static int
isReservedName(const xp3Parser *p, const xp3Token *tok) {
    size_t i;

    for (i = 0; i < XP3_TABLE_SIZE(xp3ReservedNames); i++) {
        if ((p->level >= xp3ReservedNames[i].level) &&
            (tokIsName(p, tok, xp3ReservedNames[i].name)))
            return(1);
    }
    return(0);
}

/*
 * Sequence types
 */

/*
 * NameTest ::= EQName | Wildcard
 * Wildcard ::= "*" | (NCName ":*") | ("*:" NCName) | (BracedURILiteral "*")
 */
static xp3AstNode *
parseNameTest(xp3Parser *p) {
    const xp3Token *tok = &p->tok;
    const xmlChar *text = p->lex.input + tok->start;
    xp3AstNode *test;

    test = parserTokNode(p, XP3_AST_NAME_TEST, XP3_NAMETEST_NAME, tok);
    if (test == NULL)
        return(NULL);

    switch (tok->kind) {
        case XP3_TOK_NCNAME:
        case XP3_TOK_QNAME:
        case XP3_TOK_EQNAME:
            if (parserSetName(p, test, tok) < 0)
                return(NULL);
            break;
        case XP3_TOK_STAR:
            test->op = XP3_NAMETEST_ANY;
            break;
        case XP3_TOK_NCNAME_STAR:
            test->op = XP3_NAMETEST_ANY_LOCAL;
            test->prefix = parserStrndup(p, text, tok->sep);
            if (test->prefix == NULL)
                return(NULL);
            break;
        case XP3_TOK_STAR_NCNAME:
            test->op = XP3_NAMETEST_ANY_NS;
            test->text = parserStrndup(p, text + 2, tok->length - 2);
            if (test->text == NULL)
                return(NULL);
            break;
        case XP3_TOK_BRACED_URI_STAR:
            if (parserRequire(p, tok, XP3_XPATH_30, "Q{} names") < 0)
                return(NULL);
            test->op = XP3_NAMETEST_ANY_LOCAL;
            test->uri = parserStrndup(p, text + 2, tok->sep - 2);
            if (test->uri == NULL)
                return(NULL);
            break;
        default:
            return(parserExpected(p, "a name test"));
    }

    if (parserAdvance(p) < 0)
        return(NULL);
    return(test);
}

/*
 * The type name in element(N, T?) and attribute(N, T), as a
 * SEQUENCE_TYPE holding an ATOMIC_TYPE.
 */
static xp3AstNode *
parseTypeAnnotation(xp3Parser *p, int allowOptional) {
    xp3AstNode *name;
    xp3AstNode *type;

    if (!tokIsEQName(&p->tok))
        return(parserExpected(p, "a type name"));
    name = parserTokNode(p, XP3_AST_ATOMIC_TYPE, 0, &p->tok);
    if ((name == NULL) || (parserSetName(p, name, &p->tok) < 0) ||
        (parserAdvance(p) < 0))
        return(NULL);
    type = parserWrap(p, XP3_AST_SEQUENCE_TYPE, XP3_OCC_ONE, name);
    if (type == NULL)
        return(NULL);
    if (allowOptional && (p->tok.kind == XP3_TOK_QUESTION)) {
        type->op = XP3_OCC_OPTIONAL;
        if (parserAdvance(p) < 0)
            return(NULL);
    }
    return(type);
}

/*
 * KindTest, with the current token on its name:
 *
 *   DocumentTest     ::= "document-node" "(" (ElementTest |
 *                        SchemaElementTest)? ")"
 *   ElementTest      ::= "element" "(" (ElementNameOrWildcard
 *                        ("," TypeName "?"?)?)? ")"
 *   AttributeTest    ::= "attribute" "(" (AttribNameOrWildcard
 *                        ("," TypeName)?)? ")"
 *   SchemaElementTest::= "schema-element" "(" EQName ")"
 *   PITest           ::= "processing-instruction" "(" (NCName |
 *                        StringLiteral)? ")"
 *
 * and node(), text(), comment(), namespace-node().
 */
static xp3AstNode *
parseKindTest(xp3Parser *p, xp3KindTestKind kind) {
    xp3AstNode *test;
    xp3AstNode *child;

    test = parserTokNode(p, XP3_AST_KIND_TEST, kind, &p->tok);
    if ((test == NULL) || (parserAdvance(p) < 0) ||
        (parserExpect(p, XP3_TOK_LPAREN, "'('") < 0))
        return(NULL);

    switch (kind) {
        case XP3_KIND_PI:
            if (p->tok.kind == XP3_TOK_NCNAME)
                test->text = parserStrndup(p, p->lex.input + p->tok.start,
                                           p->tok.length);
            else if (p->tok.kind == XP3_TOK_STRING)
                test->text = parserStringValue(p, &p->tok);
            else
                break;
            if ((test->text == NULL) || (parserAdvance(p) < 0))
                return(NULL);
            break;

        case XP3_KIND_DOCUMENT:
            if (tokIsName(p, &p->tok, "element"))
                child = parseKindTest(p, XP3_KIND_ELEMENT);
            else if (tokIsName(p, &p->tok, "schema-element"))
                child = parseKindTest(p, XP3_KIND_SCHEMA_ELEMENT);
            else
                break;
            if (child == NULL)
                return(NULL);
            xp3AstAppend(test, child);
            break;

        case XP3_KIND_ELEMENT:
        case XP3_KIND_ATTRIBUTE:
            if (p->tok.kind == XP3_TOK_RPAREN)
                break;
            if ((p->tok.kind != XP3_TOK_STAR) && (!tokIsEQName(&p->tok)))
                return(parserExpected(p, "a name or '*'"));
            child = parseNameTest(p);
            if (child == NULL)
                return(NULL);
            xp3AstAppend(test, child);
            if (p->tok.kind != XP3_TOK_COMMA)
                break;
            if (parserAdvance(p) < 0)
                return(NULL);
            child = parseTypeAnnotation(p, kind == XP3_KIND_ELEMENT);
            if (child == NULL)
                return(NULL);
            xp3AstAppend(test, child);
            break;

        case XP3_KIND_SCHEMA_ELEMENT:
        case XP3_KIND_SCHEMA_ATTRIBUTE:
            if (!tokIsEQName(&p->tok))
                return(parserExpected(p, "a name"));
            child = parseNameTest(p);
            if (child == NULL)
                return(NULL);
            xp3AstAppend(test, child);
            break;

        default:
            break;
    }

    if (parserExpect(p, XP3_TOK_RPAREN, "')'") < 0)
        return(NULL);
    return(test);
}

/*
 * ItemType ::= KindTest | ("item" "(" ")") | AtomicOrUnionType
 */
static xp3AstNode *
parseItemType(xp3Parser *p) {
    xp3AstNode *type;

    if (p->tok.kind == XP3_TOK_NCNAME) {
        int next = parserPeekKind(p);
        int kind;

        if (next < 0)
            return(NULL);
        if (next == XP3_TOK_LPAREN) {
            if (tokIsName(p, &p->tok, "item")) {
                type = parserTokNode(p, XP3_AST_ANY_ITEM_TYPE, 0, &p->tok);
                if ((type == NULL) || (parserAdvance(p) < 0) ||
                    (parserAdvance(p) < 0) ||
                    (parserExpect(p, XP3_TOK_RPAREN, "')'") < 0))
                    return(NULL);
                return(type);
            }
            kind = lookupKindTest(p, &p->tok);
            if (kind >= 0)
                return(parseKindTest(p, (xp3KindTestKind) kind));
        }
    }

    if (!tokIsEQName(&p->tok))
        return(parserExpected(p, "a sequence type"));
    type = parserTokNode(p, XP3_AST_ATOMIC_TYPE, 0, &p->tok);
    if ((type == NULL) || (parserSetName(p, type, &p->tok) < 0) ||
        (parserAdvance(p) < 0))
        return(NULL);
    return(type);
}

/*
 * SequenceType ::= ("empty-sequence" "(" ")")
 *                | (ItemType OccurrenceIndicator?)
 *
 * A "?", "*" or "+" right after an item type is always an occurrence
 * indicator (A.2.1.3, occurrence-indicators), so in
 * "4 treat as item() + - 5" the "+" belongs to the type.
 */
static xp3AstNode *
parseSequenceType(xp3Parser *p) {
    xp3AstNode *item;
    xp3AstNode *type;

    if (tokIsName(p, &p->tok, "empty-sequence")) {
        int next = parserPeekKind(p);

        if (next < 0)
            return(NULL);
        if (next == XP3_TOK_LPAREN) {
            type = parserTokNode(p, XP3_AST_EMPTY_SEQUENCE_TYPE, 0,
                                 &p->tok);
            if ((type == NULL) || (parserAdvance(p) < 0) ||
                (parserAdvance(p) < 0) ||
                (parserExpect(p, XP3_TOK_RPAREN, "')'") < 0))
                return(NULL);
            return(type);
        }
    }

    item = parseItemType(p);
    if (item == NULL)
        return(NULL);
    type = parserWrap(p, XP3_AST_SEQUENCE_TYPE, XP3_OCC_ONE, item);
    if (type == NULL)
        return(NULL);

    switch (p->tok.kind) {
        case XP3_TOK_QUESTION:
            type->op = XP3_OCC_OPTIONAL;
            break;
        case XP3_TOK_STAR:
            type->op = XP3_OCC_ZERO_OR_MORE;
            break;
        case XP3_TOK_PLUS:
            type->op = XP3_OCC_ONE_OR_MORE;
            break;
        default:
            return(type);
    }
    if (parserAdvance(p) < 0)
        return(NULL);
    return(type);
}

/*
 * SingleType ::= SimpleTypeName "?"?
 */
static xp3AstNode *
parseSingleType(xp3Parser *p) {
    if (!tokIsEQName(&p->tok))
        return(parserExpected(p, "an atomic type name"));
    return(parseTypeAnnotation(p, 1));
}

/*
 * Primary expressions
 */

/* NumericLiteral */
static xp3AstNode *
parseNumber(xp3Parser *p, xp3AstKind kind) {
    xp3AstNode *node = parserTokNode(p, kind, 0, &p->tok);

    if (node == NULL)
        return(NULL);
    node->text = parserStrndup(p, p->lex.input + p->tok.start,
                               p->tok.length);
    if ((node->text == NULL) || (parserAdvance(p) < 0))
        return(NULL);
    return(node);
}

/* StringLiteral */
static xp3AstNode *
parseString(xp3Parser *p) {
    xp3AstNode *node = parserTokNode(p, XP3_AST_STRING, 0, &p->tok);

    if (node == NULL)
        return(NULL);
    node->text = parserStringValue(p, &p->tok);
    if ((node->text == NULL) || (parserAdvance(p) < 0))
        return(NULL);
    return(node);
}

/* VarRef ::= "$" EQName */
static xp3AstNode *
parseVarRef(xp3Parser *p) {
    xp3AstNode *node = parserTokNode(p, XP3_AST_VARREF, 0, &p->tok);

    if ((node == NULL) || (parserAdvance(p) < 0))
        return(NULL);
    if (!tokIsEQName(&p->tok))
        return(parserExpected(p, "a variable name"));
    if ((parserSetName(p, node, &p->tok) < 0) || (parserAdvance(p) < 0))
        return(NULL);
    return(node);
}

/* ParenthesizedExpr ::= "(" Expr? ")" */
static xp3AstNode *
parseParenthesized(xp3Parser *p) {
    xp3Token start = p->tok;
    xp3AstNode *expr;

    if (parserAdvance(p) < 0)
        return(NULL);
    if (p->tok.kind == XP3_TOK_RPAREN) {
        expr = parserTokNode(p, XP3_AST_EMPTY_SEQUENCE, 0, &start);
        if ((expr == NULL) || (parserAdvance(p) < 0))
            return(NULL);
        return(expr);
    }
    expr = parseExpr(p);
    if ((expr == NULL) || (parserExpect(p, XP3_TOK_RPAREN, "')'") < 0))
        return(NULL);
    return(expr);
}

/*
 * FunctionCall ::= EQName ArgumentList
 * ArgumentList ::= "(" (ExprSingle ("," ExprSingle)*)? ")"
 *
 * Unprefixed reserved names such as "if" or "item" are not function
 * names (A.3).
 */
static xp3AstNode *
parseFunctionCall(xp3Parser *p) {
    xp3AstNode *call;
    xp3AstNode *arg;

    if (isReservedName(p, &p->tok)) {
        char name[48];

        parserQuote(p, &p->tok, name, sizeof(name));
        return(parserError(p, XP3_ERR_XPST0003, &p->tok,
                           "%s is reserved and cannot be a function name",
                           name));
    }

    call = parserTokNode(p, XP3_AST_FUNCTION_CALL, 0, &p->tok);
    if ((call == NULL) || (parserSetName(p, call, &p->tok) < 0) ||
        (parserAdvance(p) < 0) ||
        (parserExpect(p, XP3_TOK_LPAREN, "'('") < 0))
        return(NULL);

    if (p->tok.kind == XP3_TOK_RPAREN) {
        if (parserAdvance(p) < 0)
            return(NULL);
        return(call);
    }
    for (;;) {
        arg = parseExprSingle(p);
        if (arg == NULL)
            return(NULL);
        xp3AstAppend(call, arg);
        if (p->tok.kind != XP3_TOK_COMMA)
            break;
        if (parserAdvance(p) < 0)
            return(NULL);
    }
    if (parserExpect(p, XP3_TOK_RPAREN, "',' or ')'") < 0)
        return(NULL);
    return(call);
}

/*
 * PrimaryExpr ::= Literal | VarRef | ParenthesizedExpr
 *               | ContextItemExpr | FunctionCall
 *
 * parseStep() only comes here for a name when it is followed by "(".
 */
static xp3AstNode *
parsePrimary(xp3Parser *p) {
    xp3AstNode *node;

    switch (p->tok.kind) {
        case XP3_TOK_INTEGER:
            return(parseNumber(p, XP3_AST_INTEGER));
        case XP3_TOK_DECIMAL:
            return(parseNumber(p, XP3_AST_DECIMAL));
        case XP3_TOK_DOUBLE:
            return(parseNumber(p, XP3_AST_DOUBLE));
        case XP3_TOK_STRING:
            return(parseString(p));
        case XP3_TOK_DOLLAR:
            return(parseVarRef(p));
        case XP3_TOK_LPAREN:
            return(parseParenthesized(p));
        case XP3_TOK_DOT:
            node = parserTokNode(p, XP3_AST_CONTEXT_ITEM, 0, &p->tok);
            if ((node == NULL) || (parserAdvance(p) < 0))
                return(NULL);
            return(node);
        case XP3_TOK_NCNAME:
        case XP3_TOK_QNAME:
        case XP3_TOK_EQNAME:
            return(parseFunctionCall(p));
        default:
            return(parserExpected(p, "an expression"));
    }
}

/*
 * Predicate ::= "[" Expr "]"
 *
 * Appends predicates as long as there are any, and returns node.
 */
static xp3AstNode *
parsePredicates(xp3Parser *p, xp3AstNode *node) {
    while (p->tok.kind == XP3_TOK_LBRACKET) {
        xp3AstNode *pred;
        xp3AstNode *expr;

        pred = parserTokNode(p, XP3_AST_PREDICATE, 0, &p->tok);
        if ((pred == NULL) || (parserAdvance(p) < 0))
            return(NULL);
        expr = parseExpr(p);
        if ((expr == NULL) || (parserExpect(p, XP3_TOK_RBRACKET, "']'") < 0))
            return(NULL);
        xp3AstAppend(pred, expr);
        xp3AstAppend(node, pred);
    }
    return(node);
}

/*
 * PostfixExpr ::= PrimaryExpr Predicate*
 *
 * A primary without predicates is returned as it is, otherwise it is
 * wrapped in a FILTER node.
 */
static xp3AstNode *
parsePostfix(xp3Parser *p) {
    xp3AstNode *primary;
    xp3AstNode *filter;

    primary = parsePrimary(p);
    if ((primary == NULL) || (p->tok.kind != XP3_TOK_LBRACKET))
        return(primary);
    filter = parserWrap(p, XP3_AST_FILTER, 0, primary);
    if (filter == NULL)
        return(NULL);
    return(parsePredicates(p, filter));
}

/*
 * Paths
 */

/* The descendant-or-self::node() step that "//" stands for */
static xp3AstNode *
parserDescendantStep(xp3Parser *p, const xp3Token *tok) {
    xp3AstNode *step;
    xp3AstNode *test;

    step = parserTokNode(p, XP3_AST_STEP, XP3_AXIS_DESCENDANT_OR_SELF, tok);
    test = parserTokNode(p, XP3_AST_KIND_TEST, XP3_KIND_ANY, tok);
    if ((step == NULL) || (test == NULL))
        return(NULL);
    xp3AstAppend(step, test);
    return(step);
}

/* NodeTest ::= KindTest | NameTest */
static xp3AstNode *
parseNodeTest(xp3Parser *p) {
    if (p->tok.kind == XP3_TOK_NCNAME) {
        int next = parserPeekKind(p);
        int kind;

        if (next < 0)
            return(NULL);
        if (next == XP3_TOK_LPAREN) {
            kind = lookupKindTest(p, &p->tok);
            if (kind >= 0)
                return(parseKindTest(p, (xp3KindTestKind) kind));
        }
    }
    return(parseNameTest(p));
}

/*
 * StepExpr ::= PostfixExpr | AxisStep
 * AxisStep ::= (ForwardAxis NodeTest | "@"? NodeTest
 *               | ReverseAxis NodeTest | "..") Predicate*
 *
 * A name is a function call if "(" follows and it is not a kind test,
 * otherwise a name test. Without an axis, the axis is child, except
 * for attribute() and schema-attribute(), which use the attribute
 * axis, and namespace-node(), which uses the namespace axis.
 */
static xp3AstNode *
parseStep(xp3Parser *p) {
    xp3Token start = p->tok;
    xp3AstNode *step;
    xp3AstNode *test;
    int axis = -1;
    int next;

    switch (p->tok.kind) {
        case XP3_TOK_AT:
            axis = XP3_AXIS_ATTRIBUTE;
            if (parserAdvance(p) < 0)
                return(NULL);
            break;

        case XP3_TOK_DOTDOT:
            step = parserTokNode(p, XP3_AST_STEP, XP3_AXIS_PARENT, &start);
            test = parserTokNode(p, XP3_AST_KIND_TEST, XP3_KIND_ANY, &start);
            if ((step == NULL) || (test == NULL) || (parserAdvance(p) < 0))
                return(NULL);
            xp3AstAppend(step, test);
            return(parsePredicates(p, step));

        case XP3_TOK_NCNAME:
            next = parserPeekKind(p);
            if (next < 0)
                return(NULL);
            if (next == XP3_TOK_COLONCOLON) {
                axis = lookupAxis(p, &p->tok);
                if (axis < 0) {
                    char name[48];

                    parserQuote(p, &p->tok, name, sizeof(name));
                    return(parserError(p, XP3_ERR_XPST0003, &p->tok,
                                       "unknown axis %s", name));
                }
                if ((parserAdvance(p) < 0) || (parserAdvance(p) < 0))
                    return(NULL);
            } else if ((next == XP3_TOK_LPAREN) &&
                       (lookupKindTest(p, &p->tok) < 0)) {
                return(parsePostfix(p));
            }
            break;

        case XP3_TOK_QNAME:
        case XP3_TOK_EQNAME:
            next = parserPeekKind(p);
            if (next < 0)
                return(NULL);
            if (next == XP3_TOK_LPAREN)
                return(parsePostfix(p));
            break;

        case XP3_TOK_STAR:
        case XP3_TOK_NCNAME_STAR:
        case XP3_TOK_STAR_NCNAME:
        case XP3_TOK_BRACED_URI_STAR:
            break;

        default:
            return(parsePostfix(p));
    }

    test = parseNodeTest(p);
    if (test == NULL)
        return(NULL);
    if (axis < 0) {
        axis = XP3_AXIS_CHILD;
        if (test->kind == XP3_AST_KIND_TEST) {
            if ((test->op == XP3_KIND_ATTRIBUTE) ||
                (test->op == XP3_KIND_SCHEMA_ATTRIBUTE))
                axis = XP3_AXIS_ATTRIBUTE;
            else if (test->op == XP3_KIND_NAMESPACE_NODE)
                axis = XP3_AXIS_NAMESPACE;
        }
    }
    step = parserTokNode(p, XP3_AST_STEP, axis, &start);
    if (step == NULL)
        return(NULL);
    xp3AstAppend(step, test);
    return(parsePredicates(p, step));
}

/* Whether the current token can start a StepExpr. */
static int
parserStartsStep(const xp3Parser *p) {
    switch (p->tok.kind) {
        case XP3_TOK_INTEGER:
        case XP3_TOK_DECIMAL:
        case XP3_TOK_DOUBLE:
        case XP3_TOK_STRING:
        case XP3_TOK_NCNAME:
        case XP3_TOK_QNAME:
        case XP3_TOK_EQNAME:
        case XP3_TOK_STAR:
        case XP3_TOK_NCNAME_STAR:
        case XP3_TOK_STAR_NCNAME:
        case XP3_TOK_BRACED_URI_STAR:
        case XP3_TOK_LPAREN:
        case XP3_TOK_DOT:
        case XP3_TOK_DOTDOT:
        case XP3_TOK_AT:
        case XP3_TOK_DOLLAR:
            return(1);
        default:
            return(0);
    }
}

/*
 * PathExpr         ::= ("/" RelativePathExpr?)
 *                    | ("//" RelativePathExpr)
 *                    | RelativePathExpr
 * RelativePathExpr ::= StepExpr (("/" | "//") StepExpr)*
 *
 * A "/" followed by a token that can start a step is the start of a
 * path, not a complete one (A.2.1.2, leading-lone-slash), so "/ * 5"
 * is a syntax error and "(/) * 5" is a multiplication.
 *
 * A single step that is not an axis step, such as "$x" or "f()", is
 * returned without a PATH node.
 */
static xp3AstNode *
parsePath(xp3Parser *p) {
    xp3Token start = p->tok;
    xp3AstNode *path;
    xp3AstNode *step;

    if ((start.kind == XP3_TOK_SLASH) || (start.kind == XP3_TOK_SLASHSLASH)) {
        path = parserTokNode(p, XP3_AST_PATH, XP3_PATH_ROOT, &start);
        if (path == NULL)
            return(NULL);
        if (start.kind == XP3_TOK_SLASHSLASH) {
            step = parserDescendantStep(p, &start);
            if (step == NULL)
                return(NULL);
            xp3AstAppend(path, step);
        }
        if (parserAdvance(p) < 0)
            return(NULL);
        if (!parserStartsStep(p)) {
            if (start.kind == XP3_TOK_SLASHSLASH)
                return(parserExpected(p, "a step after '//'"));
            return(path);
        }
        step = parseStep(p);
        if (step == NULL)
            return(NULL);
    } else {
        step = parseStep(p);
        if (step == NULL)
            return(NULL);
        if ((step->kind != XP3_AST_STEP) &&
            (p->tok.kind != XP3_TOK_SLASH) &&
            (p->tok.kind != XP3_TOK_SLASHSLASH))
            return(step);
        path = parserNode(p, XP3_AST_PATH, XP3_PATH_RELATIVE, step->line,
                          step->column);
        if (path == NULL)
            return(NULL);
    }
    xp3AstAppend(path, step);

    while ((p->tok.kind == XP3_TOK_SLASH) ||
           (p->tok.kind == XP3_TOK_SLASHSLASH)) {
        if (p->tok.kind == XP3_TOK_SLASHSLASH) {
            step = parserDescendantStep(p, &p->tok);
            if (step == NULL)
                return(NULL);
            xp3AstAppend(path, step);
        }
        if (parserAdvance(p) < 0)
            return(NULL);
        step = parseStep(p);
        if (step == NULL)
            return(NULL);
        xp3AstAppend(path, step);
    }
    return(path);
}

/*
 * Operators
 */

/*
 * UnaryExpr ::= ("-" | "+")* ValueExpr
 * ValueExpr ::= PathExpr
 *
 * "-+-1" gives (unary - (unary + (unary - (integer 1)))).
 */
static xp3AstNode *
parseUnary(xp3Parser *p) {
    xp3AstNode *top = NULL;
    xp3AstNode *last = NULL;
    xp3AstNode *ret = NULL;
    xp3AstNode *operand;
    int signs = 0;

    while ((p->tok.kind == XP3_TOK_PLUS) || (p->tok.kind == XP3_TOK_MINUS)) {
        xp3AstNode *node;

        if (parserEnter(p, 1) < 0)
            goto done;
        signs++;
        node = parserTokNode(p, XP3_AST_UNARY,
                             (p->tok.kind == XP3_TOK_PLUS) ? XP3_OP_ADD :
                                                             XP3_OP_SUB,
                             &p->tok);
        if ((node == NULL) || (parserAdvance(p) < 0))
            goto done;
        if (last != NULL)
            xp3AstAppend(last, node);
        else
            top = node;
        last = node;
    }

    operand = parsePath(p);
    if (operand == NULL)
        goto done;
    if (last == NULL) {
        ret = operand;
    } else {
        xp3AstAppend(last, operand);
        ret = top;
    }

done:
    p->depth -= signs;
    return(ret);
}

/*
 * InstanceofExpr, TreatExpr, CastableExpr and CastExpr: an operand,
 * then optionally two keywords and a type. None of them repeats.
 */
static xp3AstNode *
parseTypeOperator(xp3Parser *p, xp3ParseFunc operand, const char *keyword,
                  const char *keyword2, xp3AstKind kind, int single) {
    xp3AstNode *left;
    xp3AstNode *node;
    xp3AstNode *type;

    left = operand(p);
    if ((left == NULL) || (!tokIsName(p, &p->tok, keyword)))
        return(left);
    node = parserWrap(p, kind, 0, left);
    if ((node == NULL) || (parserAdvance(p) < 0))
        return(NULL);
    if (!tokIsName(p, &p->tok, keyword2)) {
        char what[16];

        snprintf(what, sizeof(what), "'%s'", keyword2);
        return(parserExpected(p, what));
    }
    if (parserAdvance(p) < 0)
        return(NULL);
    type = single ? parseSingleType(p) : parseSequenceType(p);
    if (type == NULL)
        return(NULL);
    xp3AstAppend(node, type);
    return(node);
}

/* CastExpr ::= UnaryExpr ("cast" "as" SingleType)? */
static xp3AstNode *
parseCast(xp3Parser *p) {
    return(parseTypeOperator(p, parseUnary, "cast", "as", XP3_AST_CAST_AS,
                             1));
}

/* CastableExpr ::= CastExpr ("castable" "as" SingleType)? */
static xp3AstNode *
parseCastable(xp3Parser *p) {
    return(parseTypeOperator(p, parseCast, "castable", "as",
                             XP3_AST_CASTABLE_AS, 1));
}

/* TreatExpr ::= CastableExpr ("treat" "as" SequenceType)? */
static xp3AstNode *
parseTreat(xp3Parser *p) {
    return(parseTypeOperator(p, parseCastable, "treat", "as",
                             XP3_AST_TREAT_AS, 0));
}

/* InstanceofExpr ::= TreatExpr ("instance" "of" SequenceType)? */
static xp3AstNode *
parseInstanceOf(xp3Parser *p) {
    return(parseTypeOperator(p, parseTreat, "instance", "of",
                             XP3_AST_INSTANCE_OF, 0));
}

/*
 * A level of binary operators: operand (op operand)*, folded to the
 * left, or operand (op operand)? if chain is 0.
 *
 * Keyword operators are NCName tokens. In operator position a name
 * cannot start an operand, so "div div div" divides child::div by
 * child::div.
 */
static xp3AstNode *
parseBinary(xp3Parser *p, xp3ParseFunc operand, xp3MatchFunc match,
            int chain) {
    xp3AstNode *left;
    int folds = 0;
    int op;

    left = operand(p);
    while ((left != NULL) && ((op = match(p)) >= 0)) {
        xp3AstNode *node;
        xp3AstNode *right;

        if (parserEnter(p, 1) < 0) {
            left = NULL;
            break;
        }
        folds++;
        node = parserNode(p, XP3_AST_BINARY, op, left->line, left->column);
        if ((node == NULL) || (parserAdvance(p) < 0)) {
            left = NULL;
            break;
        }
        right = operand(p);
        if (right == NULL) {
            left = NULL;
            break;
        }
        xp3AstAppend(node, left);
        xp3AstAppend(node, right);
        left = node;
        if (!chain)
            break;
    }
    p->depth -= folds;
    return(left);
}

static int
matchIntersectExcept(const xp3Parser *p) {
    if (tokIsName(p, &p->tok, "intersect"))
        return(XP3_OP_INTERSECT);
    if (tokIsName(p, &p->tok, "except"))
        return(XP3_OP_EXCEPT);
    return(-1);
}

static int
matchUnion(const xp3Parser *p) {
    if ((p->tok.kind == XP3_TOK_PIPE) || (tokIsName(p, &p->tok, "union")))
        return(XP3_OP_UNION);
    return(-1);
}

static int
matchMultiplicative(const xp3Parser *p) {
    if (p->tok.kind == XP3_TOK_STAR)
        return(XP3_OP_MUL);
    if (tokIsName(p, &p->tok, "div"))
        return(XP3_OP_DIV);
    if (tokIsName(p, &p->tok, "idiv"))
        return(XP3_OP_IDIV);
    if (tokIsName(p, &p->tok, "mod"))
        return(XP3_OP_MOD);
    return(-1);
}

static int
matchAdditive(const xp3Parser *p) {
    if (p->tok.kind == XP3_TOK_PLUS)
        return(XP3_OP_ADD);
    if (p->tok.kind == XP3_TOK_MINUS)
        return(XP3_OP_SUB);
    return(-1);
}

static int
matchRange(const xp3Parser *p) {
    return(tokIsName(p, &p->tok, "to") ? XP3_OP_RANGE : -1);
}

static int
matchComparison(const xp3Parser *p) {
    switch (p->tok.kind) {
        case XP3_TOK_EQ:   return(XP3_OP_GEN_EQ);
        case XP3_TOK_NE:   return(XP3_OP_GEN_NE);
        case XP3_TOK_LT:   return(XP3_OP_GEN_LT);
        case XP3_TOK_LE:   return(XP3_OP_GEN_LE);
        case XP3_TOK_GT:   return(XP3_OP_GEN_GT);
        case XP3_TOK_GE:   return(XP3_OP_GEN_GE);
        case XP3_TOK_LTLT: return(XP3_OP_PRECEDES);
        case XP3_TOK_GTGT: return(XP3_OP_FOLLOWS);
        default:           break;
    }
    if (tokIsName(p, &p->tok, "eq"))
        return(XP3_OP_VAL_EQ);
    if (tokIsName(p, &p->tok, "ne"))
        return(XP3_OP_VAL_NE);
    if (tokIsName(p, &p->tok, "lt"))
        return(XP3_OP_VAL_LT);
    if (tokIsName(p, &p->tok, "le"))
        return(XP3_OP_VAL_LE);
    if (tokIsName(p, &p->tok, "gt"))
        return(XP3_OP_VAL_GT);
    if (tokIsName(p, &p->tok, "ge"))
        return(XP3_OP_VAL_GE);
    if (tokIsName(p, &p->tok, "is"))
        return(XP3_OP_IS);
    return(-1);
}

static int
matchAnd(const xp3Parser *p) {
    return(tokIsName(p, &p->tok, "and") ? XP3_OP_AND : -1);
}

static int
matchOr(const xp3Parser *p) {
    return(tokIsName(p, &p->tok, "or") ? XP3_OP_OR : -1);
}

/* IntersectExceptExpr ::= InstanceofExpr (("intersect" | "except")
 *                         InstanceofExpr)* */
static xp3AstNode *
parseIntersectExcept(xp3Parser *p) {
    return(parseBinary(p, parseInstanceOf, matchIntersectExcept, 1));
}

/* UnionExpr ::= IntersectExceptExpr (("union" | "|")
 *               IntersectExceptExpr)* */
static xp3AstNode *
parseUnion(xp3Parser *p) {
    return(parseBinary(p, parseIntersectExcept, matchUnion, 1));
}

/* MultiplicativeExpr ::= UnionExpr (("*" | "div" | "idiv" | "mod")
 *                        UnionExpr)* */
static xp3AstNode *
parseMultiplicative(xp3Parser *p) {
    return(parseBinary(p, parseUnion, matchMultiplicative, 1));
}

/* AdditiveExpr ::= MultiplicativeExpr (("+" | "-") MultiplicativeExpr)* */
static xp3AstNode *
parseAdditive(xp3Parser *p) {
    return(parseBinary(p, parseMultiplicative, matchAdditive, 1));
}

/* RangeExpr ::= AdditiveExpr ("to" AdditiveExpr)? */
static xp3AstNode *
parseRange(xp3Parser *p) {
    return(parseBinary(p, parseAdditive, matchRange, 0));
}

/*
 * ComparisonExpr ::= RangeExpr ((ValueComp | GeneralComp | NodeComp)
 *                    RangeExpr)?
 *
 * Comparisons do not chain: "1 = 2 = 3" is a syntax error.
 */
static xp3AstNode *
parseComparison(xp3Parser *p) {
    return(parseBinary(p, parseRange, matchComparison, 0));
}

/* AndExpr ::= ComparisonExpr ("and" ComparisonExpr)* */
static xp3AstNode *
parseAnd(xp3Parser *p) {
    return(parseBinary(p, parseComparison, matchAnd, 1));
}

/* OrExpr ::= AndExpr ("or" AndExpr)* */
static xp3AstNode *
parseOr(xp3Parser *p) {
    return(parseBinary(p, parseAnd, matchOr, 1));
}

/* ExprSingle ::= OrExpr */
static xp3AstNode *
parseExprSingle(xp3Parser *p) {
    xp3AstNode *ret;

    if (parserEnter(p, XP3_PARSE_EXPR_COST) < 0)
        return(NULL);
    ret = parseOr(p);
    p->depth -= XP3_PARSE_EXPR_COST;
    return(ret);
}

/*
 * Expr ::= ExprSingle ("," ExprSingle)*
 *
 * A single ExprSingle is returned as it is, otherwise the items are
 * the children of a SEQUENCE node.
 */
static xp3AstNode *
parseExpr(xp3Parser *p) {
    xp3AstNode *first;
    xp3AstNode *seq;

    first = parseExprSingle(p);
    if ((first == NULL) || (p->tok.kind != XP3_TOK_COMMA))
        return(first);
    seq = parserWrap(p, XP3_AST_SEQUENCE, 0, first);
    if (seq == NULL)
        return(NULL);
    while (p->tok.kind == XP3_TOK_COMMA) {
        xp3AstNode *item;

        if (parserAdvance(p) < 0)
            return(NULL);
        item = parseExprSingle(p);
        if (item == NULL)
            return(NULL);
        xp3AstAppend(seq, item);
    }
    return(seq);
}

/**
 * Parse an expression into a syntax tree.
 *
 * Prefixes, variables and function names are not resolved here. That
 * is left to static analysis, so the tree keeps names as written.
 *
 * @param ctx     the static context with the language level and the
 *                limits, or NULL for the defaults
 * @param expr    the expression in UTF-8, or NULL for an empty one
 * @param length  length of the expression in bytes
 * @param err     where to store an error, or NULL
 * @returns the tree, to be freed with xp3AstFree(), or NULL on a
 *          syntax error (XPST0003), when a limit is exceeded (LIMIT)
 *          or when memory allocation failed (NO_MEMORY)
 */
xp3Ast *
xp3Parse(const xp3StaticContext *ctx, const xmlChar *expr, size_t length,
         xp3Error **err) {
    xp3Parser p;
    xp3AstNode *root;

    if (err != NULL)
        *err = NULL;
    memset(&p, 0, sizeof(p));
    p.level = (ctx != NULL) ? ctx->level : XP3_DEFAULT_LANGUAGE_LEVEL;
    p.maxDepth = (ctx != NULL) ? ctx->maxParseDepth :
                                 XP3_DEFAULT_MAX_PARSE_DEPTH;

    if (xp3LexerInit(&p.lex, ctx, expr, length, &p.err) < 0)
        goto error;
    p.ast = xp3AstNew();
    if (p.ast == NULL) {
        parserNoMemory(&p);
        goto error;
    }
    if (parserAdvance(&p) < 0)
        goto error;

    root = parseExpr(&p);
    if (root == NULL)
        goto error;
    if (p.tok.kind != XP3_TOK_EOF) {
        char found[48];

        parserQuote(&p, &p.tok, found, sizeof(found));
        parserError(&p, XP3_ERR_XPST0003, &p.tok, "unexpected %s", found);
        goto error;
    }

    p.ast->root = root;
    return(p.ast);

error:
    xp3AstFree(p.ast);
    if (err != NULL)
        *err = p.err;
    else
        xp3ErrorFree(p.err);
    return(NULL);
}
