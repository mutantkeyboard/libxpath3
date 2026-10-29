/*
 * testlexer.c: tests for the XPath tokenizer
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdio.h>
#include <string.h>

#include <libxml/parser.h>

#include <libxpath3/xpath3.h>
#include "private/xp3chars.h"
#include "private/xp3context.h"
#include "private/xp3error.h"
#include "private/xp3lexer.h"

static int failures = 0;

static void
check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

/* Token kinds whose text is shown in the description */
static int
hasText(xp3TokenKind kind) {
    return((kind >= XP3_TOK_INTEGER) && (kind <= XP3_TOK_BRACED_URI_STAR) &&
           (kind != XP3_TOK_STAR));
}

/*
 * Tokenize expr and describe the tokens as "KIND KIND(text) ...",
 * without the final EOF. Returns 0, or -1 with *err set on error.
 */
static int
describe(const xp3StaticContext *ctx, const char *expr, size_t length,
         char *out, size_t outSize, xp3Error **err) {
    xp3Lexer lex;
    xp3Token tok;
    size_t used = 0;

    out[0] = 0;
    if (xp3LexerInit(&lex, ctx, BAD_CAST expr, length, err) < 0)
        return(-1);

    for (;;) {
        int n;

        if (xp3LexerNext(&lex, &tok, err) < 0)
            return(-1);
        if (tok.kind == XP3_TOK_EOF)
            return(0);

        if (hasText(tok.kind))
            n = snprintf(out + used, outSize - used, "%s%s(%.*s)",
                         used ? " " : "", xp3TokenKindName(tok.kind),
                         (int) tok.length, expr + tok.start);
        else
            n = snprintf(out + used, outSize - used, "%s%s",
                         used ? " " : "", xp3TokenKindName(tok.kind));
        if ((n < 0) || ((size_t) n >= outSize - used))
            return(0);  /* truncated, the comparison will fail */
        used += (size_t) n;
    }
}

static const struct {
    const char *expr;
    const char *tokens;
} tokenCases[] = {
    /* Literals */
    { "42", "INTEGER(42)" },
    { ".5 5. 1.5", "DECIMAL(.5) DECIMAL(5.) DECIMAL(1.5)" },
    { "1e3 1.5E-2 .5e+1 1.e5",
      "DOUBLE(1e3) DOUBLE(1.5E-2) DOUBLE(.5e+1) DOUBLE(1.e5)" },
    { "'it''s' \"a\"\"b\" ''",
      "STRING('it''s') STRING(\"a\"\"b\") STRING('')" },
    { "'(: not a comment :)'", "STRING('(: not a comment :)')" },

    /* Names: '-' and '.' are name characters */
    { "a-b a.b _x", "NCNAME(a-b) NCNAME(a.b) NCNAME(_x)" },
    { "a - b", "NCNAME(a) MINUS NCNAME(b)" },
    { "p:local", "QNAME(p:local)" },
    { "p : local", "NCNAME(p) COLON NCNAME(local)" },
    { "Q{urn:x}local Q{}y", "EQNAME(Q{urn:x}local) EQNAME(Q{}y)" },
    { "Q", "NCNAME(Q)" },
    { "Straße/größe", "NCNAME(Straße) SLASH NCNAME(größe)" },

    /* Wildcards */
    { "* p:* *:local Q{urn:x}*",
      "STAR NCNAME_STAR(p:*) STAR_NCNAME(*:local) "
      "BRACED_URI_STAR(Q{urn:x}*)" },
    { "a*b", "NCNAME(a) STAR NCNAME(b)" },

    /* Keywords are names; the parser decides */
    { "if(a)", "NCNAME(if) LPAREN NCNAME(a) RPAREN" },
    { "10 div 3", "INTEGER(10) NCNAME(div) INTEGER(3)" },
    { "child::x", "NCNAME(child) COLONCOLON NCNAME(x)" },
    { "$x", "DOLLAR NCNAME(x)" },
    { "f#2", "NCNAME(f) HASH INTEGER(2)" },
    { "map{a:b}", "NCNAME(map) LBRACE QNAME(a:b) RBRACE" },
    { "map{a : b}", "NCNAME(map) LBRACE NCNAME(a) COLON NCNAME(b) RBRACE" },

    /* Punctuation, longest match first */
    { "( ) [ ] { } , . .. / // @ :: : := ? ! | || = != < <= > >= << >> "
      "# $ + - * =>",
      "LPAREN RPAREN LBRACKET RBRACKET LBRACE RBRACE COMMA DOT DOTDOT "
      "SLASH SLASHSLASH AT COLONCOLON COLON ASSIGN QUESTION BANG PIPE "
      "PIPEPIPE EQ NE LT LE GT GE LTLT GTGT HASH DOLLAR PLUS MINUS STAR "
      "ARROW" },
    { "a//b", "NCNAME(a) SLASHSLASH NCNAME(b)" },
    { "..", "DOTDOT" },

    /* Whitespace and comments */
    { "", "" },
    { " \t\r\n ", "" },
    { "a(: c (: nested :) :)b", "NCNAME(a) NCNAME(b)" },
    { "(::)", "" }
};

static void
testTokens(const xp3StaticContext *ctx) {
    size_t i;

    for (i = 0; i < sizeof(tokenCases) / sizeof(tokenCases[0]); i++) {
        char actual[512];
        char what[640];
        xp3Error *err = NULL;
        int ret;

        ret = describe(ctx, tokenCases[i].expr, strlen(tokenCases[i].expr),
                       actual, sizeof(actual), &err);
        if (ret < 0) {
            snprintf(what, sizeof(what), "'%s': unexpected error %s: %s",
                     tokenCases[i].expr, (const char *) xp3ErrorCode(err),
                     (const char *) xp3ErrorMessage(err));
            check(0, what);
            xp3ErrorFree(err);
            continue;
        }
        snprintf(what, sizeof(what), "'%s': got [%s], expected [%s]",
                 tokenCases[i].expr, actual, tokenCases[i].tokens);
        check(strcmp(actual, tokenCases[i].tokens) == 0, what);
    }
}

static const struct {
    const char *expr;
    size_t length;          /* 0 = strlen(expr) */
    const char *code;
    int line;
    int column;
} errorCases[] = {
    /* A numeric literal must be followed by a delimiter (A.2.2) */
    { "10div 3",  0, "XPST0003", 1, 1 },
    { "x 1.5.3",  0, "XPST0003", 1, 3 },
    { "1e",       0, "XPST0003", 1, 1 },
    { "1..2",     0, "XPST0003", 1, 1 },

    /* Unterminated constructs are reported where they start */
    { "a 'abc",          0, "XPST0003", 1, 3 },
    { "a \n \"abc\ndef", 0, "XPST0003", 2, 2 },
    { "a (: b",          0, "XPST0003", 1, 3 },
    { "(: a (: b :)",    0, "XPST0003", 1, 1 },
    { "x Q{abc",         0, "XPST0003", 1, 3 },

    /* Other errors are reported where they occur */
    { "Q{a{b}c",  0, "XPST0003", 1, 4 },
    { "Q{a\xff}c", 0, "XPST0003", 1, 4 },
    { "Q{x} 1",   0, "XPST0003", 1, 5 },
    { "a % b",    0, "XPST0003", 1, 3 },
    { "a\n  ^",   0, "XPST0003", 2, 3 },
    { "a \xff",   0, "XPST0003", 1, 3 },
    /* Lead bytes 0xF8 to 0xFF are not UTF-8, even with continuation
     * bytes after them */
    { "'\xf8\x90\x80\x80'", 0, "XPST0003", 1, 2 },
    { "a \xf9\x80\x80\x80",  0, "XPST0003", 1, 3 },
    { "'\xfc\x80\x80\x80'", 0, "XPST0003", 1, 2 },
    { "a\0b",     3, "XPST0003", 1, 2 },
    { "'a\x01'",  0, "XPST0003", 1, 3 }
};

static void
testErrors(const xp3StaticContext *ctx) {
    size_t i;

    for (i = 0; i < sizeof(errorCases) / sizeof(errorCases[0]); i++) {
        char actual[512];
        char what[640];
        xp3Error *err = NULL;
        size_t length = errorCases[i].length ? errorCases[i].length :
                                               strlen(errorCases[i].expr);
        int ret;

        ret = describe(ctx, errorCases[i].expr, length, actual,
                       sizeof(actual), &err);
        snprintf(what, sizeof(what),
                 "error case %zu: expected %s at %d:%d, got %s at %d:%d",
                 i, errorCases[i].code, errorCases[i].line,
                 errorCases[i].column,
                 err ? (const char *) xp3ErrorCode(err) : "no error",
                 xp3ErrorLine(err), xp3ErrorColumn(err));
        check((ret < 0) && (err != NULL) &&
              (strcmp((const char *) xp3ErrorCode(err),
                      errorCases[i].code) == 0) &&
              (xp3ErrorLine(err) == errorCases[i].line) &&
              (xp3ErrorColumn(err) == errorCases[i].column),
              what);
        xp3ErrorFree(err);
    }
}

static void
testPositions(const xp3StaticContext *ctx) {
    static const char expr[] = "a\n  bb\r\nc größe x";
    static const struct { int line; int column; } expected[] = {
        { 1, 1 },   /* a */
        { 2, 3 },   /* bb */
        { 3, 1 },   /* c, after CR LF */
        { 3, 3 },   /* größe */
        { 3, 9 }    /* x: columns count characters, not bytes */
    };
    xp3Lexer lex;
    xp3Token tok;
    size_t i;

    xp3LexerInit(&lex, ctx, BAD_CAST expr, strlen(expr), NULL);
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        char what[96];

        check(xp3LexerNext(&lex, &tok, NULL) == 0, "token read");
        snprintf(what, sizeof(what), "token %zu at %d:%d, got %d:%d", i,
                 expected[i].line, expected[i].column, tok.line,
                 tok.column);
        check((tok.line == expected[i].line) &&
              (tok.column == expected[i].column), what);
    }
    check(xp3LexerNext(&lex, &tok, NULL) == 0 && tok.kind == XP3_TOK_EOF,
          "EOF after the last token");
    check(xp3LexerNext(&lex, &tok, NULL) == 0 && tok.kind == XP3_TOK_EOF,
          "EOF again");
}

static void
testSeparators(const xp3StaticContext *ctx) {
    static const char expr[] = "pre:local *:any Q{urn:x}name";
    xp3Lexer lex;
    xp3Token tok;

    xp3LexerInit(&lex, ctx, BAD_CAST expr, strlen(expr), NULL);

    xp3LexerNext(&lex, &tok, NULL);
    check(tok.kind == XP3_TOK_QNAME && tok.sep == 3 &&
          expr[tok.start + tok.sep] == ':', "QName separator");

    xp3LexerNext(&lex, &tok, NULL);
    check(tok.kind == XP3_TOK_STAR_NCNAME && tok.sep == 1,
          "*:local separator");

    xp3LexerNext(&lex, &tok, NULL);
    check(tok.kind == XP3_TOK_EQNAME && expr[tok.start + tok.sep] == '}' &&
          tok.sep == strlen("Q{urn:x"), "EQName separator");
}

static void
testPeekAndRestore(const xp3StaticContext *ctx) {
    static const char expr[] = "if (x)";
    xp3Lexer lex;
    xp3Lexer saved;
    xp3Token peeked;
    xp3Token tok;

    xp3LexerInit(&lex, ctx, BAD_CAST expr, strlen(expr), NULL);
    check(xp3LexerPeek(&lex, &peeked, NULL) == 0, "peek");
    check(xp3LexerNext(&lex, &tok, NULL) == 0, "next after peek");
    check(peeked.kind == tok.kind && peeked.start == tok.start,
          "peek does not consume");

    saved = lex;
    xp3LexerNext(&lex, &tok, NULL);
    check(tok.kind == XP3_TOK_LPAREN, "LPAREN after if");
    lex = saved;
    xp3LexerNext(&lex, &tok, NULL);
    check(tok.kind == XP3_TOK_LPAREN, "restored lexer reads LPAREN again");
}

static void
testLimits(void) {
    xp3StaticContext *ctx = xp3NewStaticContext();
    xp3Lexer lex;
    xp3Token tok;
    xp3Error *err = NULL;
    char out[64];

    ctx->maxExprLength = 5;
    check(xp3LexerInit(&lex, ctx, BAD_CAST "123456", 6, &err) < 0 &&
          err != NULL &&
          strcmp((const char *) xp3ErrorCode(err), "LIMIT") == 0,
          "expression length limit");
    xp3ErrorFree(err);
    err = NULL;
    check(xp3LexerInit(&lex, ctx, BAD_CAST "12345", 5, &err) == 0,
          "expression at the length limit");

    ctx->maxExprLength = XP3_DEFAULT_MAX_EXPR_LENGTH;
    ctx->maxParseDepth = 3;
    check(describe(ctx, "(:(:(: x :):):) 1", 17, out, sizeof(out), &err)
              == 0 && strcmp(out, "INTEGER(1)") == 0,
          "comments at the nesting limit");
    check(describe(ctx, "(:(:(:(: x :):):):)", 19, out, sizeof(out), &err)
              < 0 && err != NULL &&
          strcmp((const char *) xp3ErrorCode(err), "LIMIT") == 0,
          "comments nested too deeply");
    xp3ErrorFree(err);

    check(xp3LexerInit(&lex, NULL, NULL, 0, NULL) == 0 &&
          xp3LexerNext(&lex, &tok, NULL) == 0 && tok.kind == XP3_TOK_EOF,
          "NULL context and expression");

    xp3FreeStaticContext(ctx);
}

static void
testKindNames(void) {
    check(strcmp(xp3TokenKindName(XP3_TOK_ARROW), "ARROW") == 0,
          "last kind has a name");
    check(xp3TokenKindName((xp3TokenKind) 9999) == NULL,
          "unknown kind has no name");
}

/*
 * Every range of NameStartChar and NameChar (XML 1.0 Fifth Edition,
 * 2.3), checked at both ends and just outside them.
 */
static void
testNameChars(void) {
    static const struct {
        int low;
        int high;
    } startRanges[] = {
        { 'A', 'Z' }, { '_', '_' }, { 'a', 'z' },
        { 0xC0, 0xD6 }, { 0xD8, 0xF6 }, { 0xF8, 0x2FF },
        { 0x370, 0x37D }, { 0x37F, 0x1FFF }, { 0x200C, 0x200D },
        { 0x2070, 0x218F }, { 0x2C00, 0x2FEF }, { 0x3001, 0xD7FF },
        { 0xF900, 0xFDCF }, { 0xFDF0, 0xFFFD }, { 0x10000, 0xEFFFF }
    };
    static const struct {
        int low;
        int high;
    } nameRanges[] = {
        { '-', '.' }, { '0', '9' }, { 0xB7, 0xB7 },
        { 0x300, 0x36F }, { 0x203F, 0x2040 }
    };
    /* Just outside a range, and in neither production */
    static const int neither[] = {
        0, ' ', ',', '/', ':', '@', '[', '^', '`', '{', 0x7F, 0xBF,
        0xD7, 0xF7, 0x37E, 0x2000, 0x200B, 0x200E, 0x203E, 0x2041,
        0x206F, 0x2190, 0x2BFF, 0x2FF0, 0x3000, 0xD800, 0xF8FF, 0xFDD0,
        0xFFFE, 0xF0000, 0x10FFFF
    };
    size_t i;
    char what[64];

    for (i = 0; i < sizeof(startRanges) / sizeof(startRanges[0]); i++) {
        int low = startRanges[i].low;
        int high = startRanges[i].high;

        snprintf(what, sizeof(what), "U+%04X starts a name", low);
        check(xp3IsNameStartChar(low) && xp3IsNameChar(low), what);
        snprintf(what, sizeof(what), "U+%04X starts a name", high);
        check(xp3IsNameStartChar(high) && xp3IsNameChar(high), what);
    }
    for (i = 0; i < sizeof(nameRanges) / sizeof(nameRanges[0]); i++) {
        int low = nameRanges[i].low;
        int high = nameRanges[i].high;

        snprintf(what, sizeof(what), "U+%04X only continues a name", low);
        check(!xp3IsNameStartChar(low) && xp3IsNameChar(low), what);
        snprintf(what, sizeof(what), "U+%04X only continues a name",
                 high);
        check(!xp3IsNameStartChar(high) && xp3IsNameChar(high), what);
    }
    for (i = 0; i < sizeof(neither) / sizeof(neither[0]); i++) {
        snprintf(what, sizeof(what), "U+%04X is not a name character",
                 neither[i]);
        check(!xp3IsNameStartChar(neither[i]) && !xp3IsNameChar(neither[i]),
              what);
    }
}

int
main(void) {
    xp3StaticContext *ctx;

    xmlInitParser();
    ctx = xp3NewStaticContext();

    testTokens(ctx);
    testErrors(ctx);
    testPositions(ctx);
    testSeparators(ctx);
    testPeekAndRestore(ctx);
    testLimits();
    testKindNames();
    testNameChars();

    xp3FreeStaticContext(ctx);

    if (failures == 0)
        printf("testlexer: OK\n");
    return(failures == 0 ? 0 : 1);
}
