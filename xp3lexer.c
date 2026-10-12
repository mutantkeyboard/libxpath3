/*
 * xp3lexer.c: tokenizer for XPath 3.1 expressions
 *
 * The lexer never allocates. Tokens point into the expression, and
 * the only allocation is the error object when something is wrong.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <string.h>

#include <libxml/xmlstring.h>

#include "private/xp3chars.h"
#include "private/xp3context.h"
#include "private/xp3error.h"
#include "private/xp3lexer.h"

/* Returned by lexCharAt() */
#define LEX_END     (-2)    /* end of the expression */
#define LEX_INVALID (-1)    /* invalid UTF-8 or not an XML character */

/* Names of the token kinds, indexed by xp3TokenKind. */
static const char *const xp3TokenKindNames[] = {
    [XP3_TOK_EOF]             = "EOF",
    [XP3_TOK_INTEGER]         = "INTEGER",
    [XP3_TOK_DECIMAL]         = "DECIMAL",
    [XP3_TOK_DOUBLE]          = "DOUBLE",
    [XP3_TOK_STRING]          = "STRING",
    [XP3_TOK_NCNAME]          = "NCNAME",
    [XP3_TOK_QNAME]           = "QNAME",
    [XP3_TOK_EQNAME]          = "EQNAME",
    [XP3_TOK_STAR]            = "STAR",
    [XP3_TOK_NCNAME_STAR]     = "NCNAME_STAR",
    [XP3_TOK_STAR_NCNAME]     = "STAR_NCNAME",
    [XP3_TOK_BRACED_URI_STAR] = "BRACED_URI_STAR",
    [XP3_TOK_LPAREN]          = "LPAREN",
    [XP3_TOK_RPAREN]          = "RPAREN",
    [XP3_TOK_LBRACKET]        = "LBRACKET",
    [XP3_TOK_RBRACKET]        = "RBRACKET",
    [XP3_TOK_LBRACE]          = "LBRACE",
    [XP3_TOK_RBRACE]          = "RBRACE",
    [XP3_TOK_COMMA]           = "COMMA",
    [XP3_TOK_DOT]             = "DOT",
    [XP3_TOK_DOTDOT]          = "DOTDOT",
    [XP3_TOK_SLASH]           = "SLASH",
    [XP3_TOK_SLASHSLASH]      = "SLASHSLASH",
    [XP3_TOK_AT]              = "AT",
    [XP3_TOK_COLONCOLON]      = "COLONCOLON",
    [XP3_TOK_COLON]           = "COLON",
    [XP3_TOK_ASSIGN]          = "ASSIGN",
    [XP3_TOK_QUESTION]        = "QUESTION",
    [XP3_TOK_BANG]            = "BANG",
    [XP3_TOK_PIPE]            = "PIPE",
    [XP3_TOK_PIPEPIPE]        = "PIPEPIPE",
    [XP3_TOK_EQ]              = "EQ",
    [XP3_TOK_NE]              = "NE",
    [XP3_TOK_LT]              = "LT",
    [XP3_TOK_LE]              = "LE",
    [XP3_TOK_GT]              = "GT",
    [XP3_TOK_GE]              = "GE",
    [XP3_TOK_LTLT]            = "LTLT",
    [XP3_TOK_GTGT]            = "GTGT",
    [XP3_TOK_HASH]            = "HASH",
    [XP3_TOK_DOLLAR]          = "DOLLAR",
    [XP3_TOK_PLUS]            = "PLUS",
    [XP3_TOK_MINUS]           = "MINUS",
    [XP3_TOK_ARROW]           = "ARROW"
};

/**
 * Get the name of a token kind, for messages and tests.
 *
 * @param kind  the token kind
 * @returns the name, for example "QNAME", or NULL for unknown kinds
 */
const char *
xp3TokenKindName(xp3TokenKind kind) {
    if ((unsigned) kind >=
        sizeof(xp3TokenKindNames) / sizeof(xp3TokenKindNames[0]))
        return(NULL);
    return(xp3TokenKindNames[kind]);
}

/* The byte at offset off from the current position, or -1 at the end. */
static int
lexByte(const xp3Lexer *lex, size_t off) {
    size_t p = lex->pos + off;

    return((p < lex->length) ? lex->input[p] : -1);
}

static int
lexIsDigit(int b) {
    return((b >= '0') && (b <= '9'));
}

/*
 * Decode the character at byte offset pos and store its length in
 * *len. Returns the code point, LEX_END at the end of the expression,
 * or LEX_INVALID for invalid UTF-8 or a character not allowed in XML.
 */
static int
lexCharAt(const xp3Lexer *lex, size_t pos, int *len) {
    int c;
    int n;

    *len = 0;
    if (pos >= lex->length)
        return(LEX_END);

    if (lex->input[pos] < 0x80) {
        c = lex->input[pos];
        n = 1;
    } else {
        size_t avail = lex->length - pos;

        n = (avail > 4) ? 4 : (int) avail;
        c = xmlGetUTF8Char(lex->input + pos, &n);
        if (c < 0)
            return(LEX_INVALID);
    }

    if (!xp3IsXmlChar(c))
        return(LEX_INVALID);
    *len = n;
    return(c);
}

/* Advance over bytes that contain no line end, chars characters long. */
static void
lexSkip(xp3Lexer *lex, size_t bytes, int chars) {
    lex->pos += bytes;
    lex->column += chars;
}

/* Advance over one character of len bytes, counting line ends.
 * CR LF counts as one line end. */
static void
lexSkipChar(xp3Lexer *lex, int c, int len) {
    lex->pos += (size_t) len;
    if (c == '\n') {
        lex->line++;
        lex->column = 1;
    } else if (c == '\r') {
        if ((lex->pos < lex->length) && (lex->input[lex->pos] == '\n'))
            lex->pos++;
        lex->line++;
        lex->column = 1;
    } else {
        lex->column++;
    }
}

static int
lexError(xp3ErrCode code, int line, int column, const char *msg,
         xp3Error **err) {
    if (err != NULL)
        *err = xp3ErrorNew(code, line, column, "%s", msg);
    return(-1);
}

/*
 * Scan an NCName starting at byte offset p. Returns the offset after
 * it and adds its length in characters to *chars. Returns p unchanged
 * if no NCName starts there.
 */
static size_t
lexScanNCName(const xp3Lexer *lex, size_t p, int *chars) {
    int c;
    int len;

    c = lexCharAt(lex, p, &len);
    if ((c < 0) || (!xp3IsNameStartChar(c)))
        return(p);
    p += (size_t) len;
    (*chars)++;

    for (;;) {
        c = lexCharAt(lex, p, &len);
        if ((c < 0) || (!xp3IsNameChar(c)))
            break;
        p += (size_t) len;
        (*chars)++;
    }

    return(p);
}

/* Skip a comment, which may contain nested comments. */
static int
lexComment(xp3Lexer *lex, xp3Error **err) {
    int line = lex->line;
    int column = lex->column;
    int depth = 0;

    for (;;) {
        int b0 = lexByte(lex, 0);
        int b1 = lexByte(lex, 1);
        int c;
        int len;

        if ((b0 == '(') && (b1 == ':')) {
            if (++depth > lex->maxCommentDepth)
                return(lexError(XP3_ERR_LIMIT, lex->line, lex->column,
                                "comments nested too deeply", err));
            lexSkip(lex, 2, 2);
            continue;
        }
        if ((b0 == ':') && (b1 == ')')) {
            lexSkip(lex, 2, 2);
            if (--depth == 0)
                return(0);
            continue;
        }

        c = lexCharAt(lex, lex->pos, &len);
        if (c == LEX_END)
            return(lexError(XP3_ERR_XPST0003, line, column,
                            "unterminated comment", err));
        if (c == LEX_INVALID)
            return(lexError(XP3_ERR_XPST0003, lex->line, lex->column,
                            "invalid character", err));
        lexSkipChar(lex, c, len);
    }
}

/* Skip whitespace and comments. */
static int
lexSkipIgnorable(xp3Lexer *lex, xp3Error **err) {
    for (;;) {
        int b = lexByte(lex, 0);

        if ((b == ' ') || (b == '\t') || (b == '\n') || (b == '\r')) {
            lexSkipChar(lex, b, 1);
        } else if ((b == '(') && (lexByte(lex, 1) == ':')) {
            if (lexComment(lex, err) < 0)
                return(-1);
        } else {
            return(0);
        }
    }
}

/*
 * Numeric literals:
 *
 *   IntegerLiteral ::= Digits
 *   DecimalLiteral ::= ("." Digits) | (Digits "." [0-9]*)
 *   DoubleLiteral  ::= (("." Digits) | (Digits ("." [0-9]*)?))
 *                      [eE] [+-]? Digits
 *
 * A numeric literal immediately followed by "." or a name start
 * character is an error (XPath 3.1, A.2.2), for example "10div 3".
 */
static int
lexNumber(xp3Lexer *lex, xp3Token *tok, xp3Error **err) {
    const xmlChar *in = lex->input;
    size_t p = lex->pos;
    int c;
    int len;

    tok->kind = XP3_TOK_INTEGER;
    while ((p < lex->length) && lexIsDigit(in[p]))
        p++;
    if ((p < lex->length) && (in[p] == '.')) {
        tok->kind = XP3_TOK_DECIMAL;
        p++;
        while ((p < lex->length) && lexIsDigit(in[p]))
            p++;
    }
    if ((p < lex->length) && ((in[p] == 'e') || (in[p] == 'E'))) {
        size_t q = p + 1;

        if ((q < lex->length) && ((in[q] == '+') || (in[q] == '-')))
            q++;
        if ((q < lex->length) && lexIsDigit(in[q])) {
            tok->kind = XP3_TOK_DOUBLE;
            p = q;
            while ((p < lex->length) && lexIsDigit(in[p]))
                p++;
        }
    }

    c = lexCharAt(lex, p, &len);
    if ((c == '.') || ((c >= 0) && xp3IsNameStartChar(c)))
        return(lexError(XP3_ERR_XPST0003, tok->line, tok->column,
                        "numeric literal must be followed by a "
                        "delimiter", err));

    tok->length = p - lex->pos;
    lexSkip(lex, tok->length, (int) tok->length);
    return(0);
}

/* String literal. A doubled quote stands for one quote character. */
static int
lexString(xp3Lexer *lex, xp3Token *tok, xp3Error **err) {
    int quote = lexByte(lex, 0);

    lexSkip(lex, 1, 1);
    for (;;) {
        int len;
        int c = lexCharAt(lex, lex->pos, &len);

        if (c == LEX_END)
            return(lexError(XP3_ERR_XPST0003, tok->line, tok->column,
                            "unterminated string literal", err));
        if (c == LEX_INVALID)
            return(lexError(XP3_ERR_XPST0003, lex->line, lex->column,
                            "invalid character", err));
        if (c == quote) {
            if (lexByte(lex, 1) == quote) {
                lexSkip(lex, 2, 2);
                continue;
            }
            lexSkip(lex, 1, 1);
            break;
        }
        lexSkipChar(lex, c, len);
    }

    tok->kind = XP3_TOK_STRING;
    tok->length = lex->pos - tok->start;
    return(0);
}

/*
 * URIQualifiedName or wildcard after a braced URI:
 *
 *   BracedURILiteral ::= "Q" "{" [^{}]* "}"
 *   URIQualifiedName ::= BracedURILiteral NCName
 *   Wildcard         ::= BracedURILiteral "*"
 */
static int
lexBracedUri(xp3Lexer *lex, xp3Token *tok, xp3Error **err) {
    int chars = 0;
    size_t end;
    int c;
    int len;

    lexSkip(lex, 2, 2);
    for (;;) {
        c = lexCharAt(lex, lex->pos, &len);
        if (c == LEX_END)
            return(lexError(XP3_ERR_XPST0003, tok->line, tok->column,
                            "unterminated braced URI literal", err));
        if (c == LEX_INVALID)
            return(lexError(XP3_ERR_XPST0003, lex->line, lex->column,
                            "invalid character", err));
        if (c == '{')
            return(lexError(XP3_ERR_XPST0003, lex->line, lex->column,
                            "'{' is not allowed in a braced URI literal",
                            err));
        if (c == '}')
            break;
        lexSkipChar(lex, c, len);
    }
    tok->sep = lex->pos - tok->start;
    lexSkip(lex, 1, 1);

    if (lexByte(lex, 0) == '*') {
        tok->kind = XP3_TOK_BRACED_URI_STAR;
        lexSkip(lex, 1, 1);
    } else {
        end = lexScanNCName(lex, lex->pos, &chars);
        if (end == lex->pos)
            return(lexError(XP3_ERR_XPST0003, lex->line, lex->column,
                            "expected a local name or '*' after a braced "
                            "URI literal", err));
        tok->kind = XP3_TOK_EQNAME;
        lexSkip(lex, end - lex->pos, chars);
    }

    tok->length = lex->pos - tok->start;
    return(0);
}

/* NCName, QName or prefix:* */
static void
lexName(xp3Lexer *lex, xp3Token *tok) {
    int chars = 0;
    size_t end = lexScanNCName(lex, lex->pos, &chars);

    tok->kind = XP3_TOK_NCNAME;
    if ((end < lex->length) && (lex->input[end] == ':')) {
        int len;
        int c = lexCharAt(lex, end + 1, &len);

        if ((c >= 0) && xp3IsNameStartChar(c)) {
            int localChars = 0;

            tok->kind = XP3_TOK_QNAME;
            tok->sep = end - lex->pos;
            end = lexScanNCName(lex, end + 1, &localChars);
            chars += 1 + localChars;
        } else if (c == '*') {
            tok->kind = XP3_TOK_NCNAME_STAR;
            tok->sep = end - lex->pos;
            end += 2;
            chars += 2;
        }
    }

    tok->length = end - lex->pos;
    lexSkip(lex, tok->length, chars);
}

/* "*" or "*:local" */
static void
lexStar(xp3Lexer *lex, xp3Token *tok) {
    tok->kind = XP3_TOK_STAR;
    if (lexByte(lex, 1) == ':') {
        int chars = 0;
        size_t end = lexScanNCName(lex, lex->pos + 2, &chars);

        if (end > lex->pos + 2) {
            tok->kind = XP3_TOK_STAR_NCNAME;
            tok->sep = 1;
            tok->length = end - lex->pos;
            lexSkip(lex, tok->length, 2 + chars);
            return;
        }
    }
    tok->length = 1;
    lexSkip(lex, 1, 1);
}

/* Punctuation and operators, longest match first. */
static int
lexPunct(xp3Lexer *lex, xp3Token *tok, xp3Error **err) {
    int b = lexByte(lex, 0);
    int next = lexByte(lex, 1);
    size_t n = 1;

    switch (b) {
        case '(': tok->kind = XP3_TOK_LPAREN; break;
        case ')': tok->kind = XP3_TOK_RPAREN; break;
        case '[': tok->kind = XP3_TOK_LBRACKET; break;
        case ']': tok->kind = XP3_TOK_RBRACKET; break;
        case '{': tok->kind = XP3_TOK_LBRACE; break;
        case '}': tok->kind = XP3_TOK_RBRACE; break;
        case ',': tok->kind = XP3_TOK_COMMA; break;
        case '@': tok->kind = XP3_TOK_AT; break;
        case '?': tok->kind = XP3_TOK_QUESTION; break;
        case '#': tok->kind = XP3_TOK_HASH; break;
        case '$': tok->kind = XP3_TOK_DOLLAR; break;
        case '+': tok->kind = XP3_TOK_PLUS; break;
        case '-': tok->kind = XP3_TOK_MINUS; break;
        case '.':
            if (next == '.') { tok->kind = XP3_TOK_DOTDOT; n = 2; }
            else tok->kind = XP3_TOK_DOT;
            break;
        case '/':
            if (next == '/') { tok->kind = XP3_TOK_SLASHSLASH; n = 2; }
            else tok->kind = XP3_TOK_SLASH;
            break;
        case ':':
            if (next == ':') { tok->kind = XP3_TOK_COLONCOLON; n = 2; }
            else if (next == '=') { tok->kind = XP3_TOK_ASSIGN; n = 2; }
            else tok->kind = XP3_TOK_COLON;
            break;
        case '!':
            if (next == '=') { tok->kind = XP3_TOK_NE; n = 2; }
            else tok->kind = XP3_TOK_BANG;
            break;
        case '|':
            if (next == '|') { tok->kind = XP3_TOK_PIPEPIPE; n = 2; }
            else tok->kind = XP3_TOK_PIPE;
            break;
        case '=':
            if (next == '>') { tok->kind = XP3_TOK_ARROW; n = 2; }
            else tok->kind = XP3_TOK_EQ;
            break;
        case '<':
            if (next == '=') { tok->kind = XP3_TOK_LE; n = 2; }
            else if (next == '<') { tok->kind = XP3_TOK_LTLT; n = 2; }
            else tok->kind = XP3_TOK_LT;
            break;
        case '>':
            if (next == '=') { tok->kind = XP3_TOK_GE; n = 2; }
            else if (next == '>') { tok->kind = XP3_TOK_GTGT; n = 2; }
            else tok->kind = XP3_TOK_GT;
            break;
        default:
            return(lexError(XP3_ERR_XPST0003, tok->line, tok->column,
                            "unexpected character", err));
    }

    tok->length = n;
    lexSkip(lex, n, (int) n);
    return(0);
}

/**
 * Start tokenizing an expression.
 *
 * The expression is not copied and must stay valid while the lexer
 * is used.
 *
 * @param lex     the lexer to initialize
 * @param ctx     the static context with the limits to apply, or NULL
 *                for the default limits
 * @param expr    the expression in UTF-8, or NULL for an empty one
 * @param length  length of the expression in bytes
 * @param err     where to store an error, or NULL
 * @returns 0 on success, -1 if the expression is longer than the
 *          limit (error code LIMIT)
 */
int
xp3LexerInit(xp3Lexer *lex, const xp3StaticContext *ctx,
             const xmlChar *expr, size_t length, xp3Error **err) {
    size_t maxLength = (ctx != NULL) ? ctx->maxExprLength :
                                       XP3_DEFAULT_MAX_EXPR_LENGTH;

    if (err != NULL)
        *err = NULL;
    memset(lex, 0, sizeof(*lex));
    if (expr == NULL) {
        expr = BAD_CAST "";
        length = 0;
    }
    if (length > maxLength) {
        if (err != NULL)
            *err = xp3ErrorNew(XP3_ERR_LIMIT, 0, 0,
                               "expression is longer than %zu bytes",
                               maxLength);
        return(-1);
    }

    lex->input = expr;
    lex->length = length;
    lex->line = 1;
    lex->column = 1;
    lex->maxCommentDepth = (ctx != NULL) ? ctx->maxParseDepth :
                                           XP3_DEFAULT_MAX_PARSE_DEPTH;
    return(0);
}

/**
 * Read the next token.
 *
 * Whitespace and comments are skipped. At the end of the expression
 * the token kind is XP3_TOK_EOF, and further calls return it again.
 *
 * @param lex  the lexer
 * @param tok  where to store the token
 * @param err  where to store an error, or NULL
 * @returns 0 on success, -1 on a syntax error (XPST0003) or when
 *          comments are nested deeper than the limit (LIMIT)
 */
int
xp3LexerNext(xp3Lexer *lex, xp3Token *tok, xp3Error **err) {
    int b;
    int c;
    int len;

    memset(tok, 0, sizeof(*tok));
    if (err != NULL)
        *err = NULL;
    if (lexSkipIgnorable(lex, err) < 0)
        return(-1);

    tok->start = lex->pos;
    tok->line = lex->line;
    tok->column = lex->column;

    b = lexByte(lex, 0);
    if (b < 0) {
        tok->kind = XP3_TOK_EOF;
        return(0);
    }

    if (lexIsDigit(b) || ((b == '.') && lexIsDigit(lexByte(lex, 1))))
        return(lexNumber(lex, tok, err));
    if ((b == '"') || (b == '\''))
        return(lexString(lex, tok, err));
    if ((b == 'Q') && (lexByte(lex, 1) == '{'))
        return(lexBracedUri(lex, tok, err));
    if (b == '*') {
        lexStar(lex, tok);
        return(0);
    }

    c = lexCharAt(lex, lex->pos, &len);
    if (c == LEX_INVALID)
        return(lexError(XP3_ERR_XPST0003, tok->line, tok->column,
                        "invalid character", err));
    if (xp3IsNameStartChar(c)) {
        lexName(lex, tok);
        return(0);
    }

    return(lexPunct(lex, tok, err));
}

/**
 * Read the next token without consuming it.
 *
 * @param lex  the lexer
 * @param tok  where to store the token
 * @param err  where to store an error, or NULL
 * @returns 0 on success, -1 on error, as xp3LexerNext()
 */
int
xp3LexerPeek(const xp3Lexer *lex, xp3Token *tok, xp3Error **err) {
    xp3Lexer copy = *lex;

    return(xp3LexerNext(&copy, tok, err));
}
