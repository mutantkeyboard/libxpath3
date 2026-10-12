/**
 * @file
 *
 * @brief Tokenizer for XPath 3.1 expressions (internal)
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XP3LEXER_H
#define XP3_XP3LEXER_H

#include <stddef.h>

#include <libxml/xmlstring.h>

#include <libxpath3/xpath3.h>

/*
 * Token kinds. Keywords such as "if", "for" or "div" are not reserved
 * in XPath, so the lexer returns them as XP3_TOK_NCNAME and the parser
 * decides from the context.
 */
typedef enum {
    XP3_TOK_EOF = 0,

    /* Literals */
    XP3_TOK_INTEGER,            /* 42 */
    XP3_TOK_DECIMAL,            /* .5  5.  1.5 */
    XP3_TOK_DOUBLE,             /* 1e3  1.5E-2 */
    XP3_TOK_STRING,             /* "a""b"  'it''s', quotes included */

    /* Names */
    XP3_TOK_NCNAME,             /* local */
    XP3_TOK_QNAME,              /* prefix:local */
    XP3_TOK_EQNAME,             /* Q{uri}local */

    /* Wildcards */
    XP3_TOK_STAR,               /* *, also multiplication */
    XP3_TOK_NCNAME_STAR,        /* prefix:* */
    XP3_TOK_STAR_NCNAME,        /* *:local */
    XP3_TOK_BRACED_URI_STAR,    /* Q{uri}* */

    /* Punctuation and operators */
    XP3_TOK_LPAREN,             /* ( */
    XP3_TOK_RPAREN,             /* ) */
    XP3_TOK_LBRACKET,           /* [ */
    XP3_TOK_RBRACKET,           /* ] */
    XP3_TOK_LBRACE,             /* { */
    XP3_TOK_RBRACE,             /* } */
    XP3_TOK_COMMA,              /* , */
    XP3_TOK_DOT,                /* . */
    XP3_TOK_DOTDOT,             /* .. */
    XP3_TOK_SLASH,              /* / */
    XP3_TOK_SLASHSLASH,         /* // */
    XP3_TOK_AT,                 /* @ */
    XP3_TOK_COLONCOLON,         /* :: */
    XP3_TOK_COLON,              /* : */
    XP3_TOK_ASSIGN,             /* := */
    XP3_TOK_QUESTION,           /* ? */
    XP3_TOK_BANG,               /* ! */
    XP3_TOK_PIPE,               /* | */
    XP3_TOK_PIPEPIPE,           /* || */
    XP3_TOK_EQ,                 /* = */
    XP3_TOK_NE,                 /* != */
    XP3_TOK_LT,                 /* < */
    XP3_TOK_LE,                 /* <= */
    XP3_TOK_GT,                 /* > */
    XP3_TOK_GE,                 /* >= */
    XP3_TOK_LTLT,               /* << */
    XP3_TOK_GTGT,               /* >> */
    XP3_TOK_HASH,               /* # */
    XP3_TOK_DOLLAR,             /* $ */
    XP3_TOK_PLUS,               /* + */
    XP3_TOK_MINUS,              /* - */
    XP3_TOK_ARROW               /* => */
} xp3TokenKind;

/*
 * A token. It points into the expression and owns no memory.
 */
typedef struct {
    xp3TokenKind kind;
    size_t start;       /* byte offset in the expression */
    size_t length;      /* length in bytes */
    int line;           /* 1-based */
    int column;         /* 1-based, in characters */
    size_t sep;         /* offset from start of the ':' (QNAME,
                         * NCNAME_STAR) or the '}' (EQNAME,
                         * BRACED_URI_STAR), otherwise 0 */
} xp3Token;

/*
 * Lexer state. It holds no memory either, so the parser saves and
 * restores a position with a plain struct copy:
 *
 *     xp3Lexer saved = *lex;
 *     ...
 *     *lex = saved;
 */
typedef struct {
    const xmlChar *input;
    size_t length;
    size_t pos;
    int line;
    int column;
    int maxCommentDepth;
} xp3Lexer;

int
xp3LexerInit(xp3Lexer *lex, const xp3StaticContext *ctx,
             const xmlChar *expr, size_t length, xp3Error **err);

int
xp3LexerNext(xp3Lexer *lex, xp3Token *tok, xp3Error **err);

int
xp3LexerPeek(const xp3Lexer *lex, xp3Token *tok, xp3Error **err);

const char *
xp3TokenKindName(xp3TokenKind kind);

#endif /* XP3_XP3LEXER_H */
