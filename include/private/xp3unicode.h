/**
 * @file
 *
 * @brief Unicode character properties and normalization (internal)
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XP3UNICODE_H
#define XP3_XP3UNICODE_H

#include <stddef.h>

#include <libxml/xmlstring.h>

/*
 * General categories (The Unicode Standard, 4.5), in an order where
 * each one-letter group is a run: L, M, N, P, S, Z, then C
 */
typedef enum {
    XP3_UNICODE_LU = 0,         /* letter, uppercase */
    XP3_UNICODE_LL,             /* letter, lowercase */
    XP3_UNICODE_LT,             /* letter, titlecase */
    XP3_UNICODE_LM,             /* letter, modifier */
    XP3_UNICODE_LO,             /* letter, other */
    XP3_UNICODE_MN,             /* mark, nonspacing */
    XP3_UNICODE_MC,             /* mark, spacing combining */
    XP3_UNICODE_ME,             /* mark, enclosing */
    XP3_UNICODE_ND,             /* number, decimal digit */
    XP3_UNICODE_NL,             /* number, letter */
    XP3_UNICODE_NO,             /* number, other */
    XP3_UNICODE_PC,             /* punctuation, connector */
    XP3_UNICODE_PD,             /* punctuation, dash */
    XP3_UNICODE_PS,             /* punctuation, open */
    XP3_UNICODE_PE,             /* punctuation, close */
    XP3_UNICODE_PI,             /* punctuation, initial quote */
    XP3_UNICODE_PF,             /* punctuation, final quote */
    XP3_UNICODE_PO,             /* punctuation, other */
    XP3_UNICODE_SM,             /* symbol, math */
    XP3_UNICODE_SC,             /* symbol, currency */
    XP3_UNICODE_SK,             /* symbol, modifier */
    XP3_UNICODE_SO,             /* symbol, other */
    XP3_UNICODE_ZS,             /* separator, space */
    XP3_UNICODE_ZL,             /* separator, line */
    XP3_UNICODE_ZP,             /* separator, paragraph */
    XP3_UNICODE_CC,             /* other, control */
    XP3_UNICODE_CF,             /* other, format */
    XP3_UNICODE_CS,             /* other, surrogate */
    XP3_UNICODE_CO,             /* other, private use */
    XP3_UNICODE_CN              /* other, not assigned */
} xp3UnicodeCategory;

/* The most code points a full case mapping of one code point has */
#define XP3_UNICODE_CASE_MAX 3

/* The most case variants one code point has */
#define XP3_UNICODE_VARIANTS_MAX 3

/* The most code points a full decomposition of one code point has */
#define XP3_UNICODE_DECOMPOSITION_MAX 18

/* The normalization forms of UAX #15 */
typedef enum {
    XP3_UNICODE_NFC = 0,
    XP3_UNICODE_NFD,
    XP3_UNICODE_NFKC,
    XP3_UNICODE_NFKD
} xp3UnicodeForm;

/*
 * Where xp3UnicodeNormalize() writes the normalized text, in pieces.
 * Returns 0, or -1 to stop.
 */
typedef int (*xp3UnicodeOutput)(void *data, const xmlChar *bytes,
                                size_t len);

/* What xp3UnicodeNormalize() returns when it fails */
#define XP3_UNICODE_NO_MEMORY (-1)  /* memory allocation failed */
#define XP3_UNICODE_STOPPED (-2)    /* the output returned -1 */

xp3UnicodeCategory
xp3UnicodeCategoryOf(int c);

const char *
xp3UnicodeCategoryName(xp3UnicodeCategory category);

int
xp3UnicodeIsInGroup(int c, char group);

int
xp3UnicodeBlock(const xmlChar *name, size_t len, int *first, int *last);

int
xp3UnicodeSimpleUpper(int c);

int
xp3UnicodeSimpleLower(int c);

int
xp3UnicodeUpper(int c, int *mapped);

int
xp3UnicodeLower(int c, int *mapped);

int
xp3UnicodeCaseVariants(int c, int *variants);

int
xp3UnicodeCombiningClass(int c);

int
xp3UnicodeDecompose(int c, int compatibility, int *decomposition);

int
xp3UnicodeCompose(int first, int second);

int
xp3UnicodeNormalize(const xmlChar *str, size_t len, xp3UnicodeForm form,
                    xp3UnicodeOutput output, void *data);

#endif /* XP3_XP3UNICODE_H */
