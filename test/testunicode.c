/*
 * testunicode.c: tests for the Unicode tables and normalization
 *
 * Known characters of each general category, including the inside of
 * the ranges UnicodeData.txt gives by their first and last code point,
 * blocks old and new, case mappings that change the length of a
 * string, case variants, and normalization examples from UAX #15. The
 * whole of NormalizationTest.txt is too large to keep here; the
 * normalization code passes all of it for the version of the tables.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/xmlmemory.h>

#include "private/xp3chars.h"
#include "private/xp3unicode.h"
#include "testalloc.h"

static int failures = 0;

static void
check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

/*
 * General categories
 */

static const struct {
    int c;
    const char *category;
} categoryCases[] = {
    { 'A', "Lu" }, { 'z', "Ll" }, { 0x01C5, "Lt" }, { 0x02B0, "Lm" },
    { 0x05D0, "Lo" }, { 0x0300, "Mn" }, { 0x0903, "Mc" }, { 0x20DD, "Me" },
    { '7', "Nd" }, { 0x2160, "Nl" }, { 0x00B2, "No" }, { '_', "Pc" },
    { '-', "Pd" }, { '(', "Ps" }, { ')', "Pe" }, { 0x00AB, "Pi" },
    { 0x00BB, "Pf" }, { '!', "Po" }, { '+', "Sm" }, { '$', "Sc" },
    { '^', "Sk" }, { 0x00A9, "So" }, { ' ', "Zs" }, { 0x2028, "Zl" },
    { 0x2029, "Zp" }, { 0x0000, "Cc" }, { 0x00AD, "Cf" }, { 0xD800, "Cs" },
    { 0xE000, "Co" },
    /* The first, an inner and the last code point of ranges that
     * UnicodeData.txt gives by their ends only */
    { 0x4E00, "Lo" }, { 0x4E01, "Lo" }, { 0x9FFF, "Lo" },
    { 0xAC00, "Lo" }, { 0xAC01, "Lo" }, { 0xD7A3, "Lo" },
    { 0xE001, "Co" }, { 0xF8FF, "Co" }, { 0x20001, "Lo" },
    { 0x3D001, "Lo" }, { 0xF0001, "Co" }, { 0x10FFFD, "Co" },
    /* Unassigned, noncharacters, and no code point at all */
    { 0x0378, "Cn" }, { 0xD7A4, "Cn" }, { 0xFFFF, "Cn" }, { 0x10FFFF, "Cn" },
    { 0x110000, "Cn" }, { -1, "Cn" }
};

static void
testCategories(void) {
    size_t i;

    for (i = 0; i < sizeof(categoryCases) / sizeof(categoryCases[0]); i++) {
        const char *name = xp3UnicodeCategoryName(
            xp3UnicodeCategoryOf(categoryCases[i].c));
        char what[100];

        snprintf(what, sizeof(what), "U+%04X is %s, not %s",
                 (unsigned) categoryCases[i].c, categoryCases[i].category,
                 name);
        check((name != NULL) && (strcmp(name, categoryCases[i].category) == 0),
              what);
    }

    check(xp3UnicodeIsInGroup('A', 'L') && xp3UnicodeIsInGroup(0x01C5, 'L'),
          "letters are in L");
    check(xp3UnicodeIsInGroup(0x0300, 'M') && xp3UnicodeIsInGroup('7', 'N') &&
          xp3UnicodeIsInGroup('!', 'P') && xp3UnicodeIsInGroup('+', 'S') &&
          xp3UnicodeIsInGroup(' ', 'Z') && xp3UnicodeIsInGroup(0x0378, 'C'),
          "the other groups");
    check(!xp3UnicodeIsInGroup('A', 'N') && !xp3UnicodeIsInGroup('7', 'L') &&
          !xp3UnicodeIsInGroup(0x0378, 'L'), "not in another group");
    check(xp3UnicodeCategoryName((xp3UnicodeCategory) 30) == NULL,
          "no name for an invalid category");
}

/*
 * Blocks
 */

static const struct {
    const char *name;
    int first;
    int last;
} blockCases[] = {
    { "BasicLatin", 0x0000, 0x007F },
    { "Latin-1Supplement", 0x0080, 0x00FF },
    { "CJKUnifiedIdeographs", 0x4E00, 0x9FFF },
    { "HangulSyllables", 0xAC00, 0xD7AF },
    { "SupplementaryPrivateUseArea-B", 0x100000, 0x10FFFF },
    /* Added after Unicode 4: in 9.0 and in 18.0 */
    { "Adlam", 0x1E900, 0x1E95F },
    { "Seal", 0x3D000, 0x3FC3F },
    { "Jurchen", 0x18E00, 0x1919F },
    { "MiscellaneousSymbolsandArrowsExtended", 0x1DB00, 0x1DBFF }
};

static const char *const unknownBlocks[] = {
    "IsBasicLatin", "basiclatin", "Basic Latin", "BasicLatin ", "Basic",
    "", "Latin-1", "Latin1Supplement", "Zzz"
};

static void
testBlocks(void) {
    size_t i;
    int first;
    int last;

    for (i = 0; i < sizeof(blockCases) / sizeof(blockCases[0]); i++) {
        const char *name = blockCases[i].name;
        char what[100];

        first = -1;
        last = -1;
        snprintf(what, sizeof(what), "block %s", name);
        check((xp3UnicodeBlock(BAD_CAST name, strlen(name), &first,
                               &last) == 0) &&
              (first == blockCases[i].first) && (last == blockCases[i].last),
              what);
    }
    for (i = 0; i < sizeof(unknownBlocks) / sizeof(unknownBlocks[0]); i++) {
        const char *name = unknownBlocks[i];
        char what[100];

        snprintf(what, sizeof(what), "no block \"%s\"", name);
        check(xp3UnicodeBlock(BAD_CAST name, strlen(name), &first,
                              &last) == -1, what);
    }
    /* The name has a length, as in a pattern */
    check((xp3UnicodeBlock(BAD_CAST "BasicLatinX", 10, &first, &last) == 0) &&
          (last == 0x7F), "a name that isn't terminated");
}

/*
 * Case mappings
 */

/* Check a full case mapping */
static void
checkCase(int upper, int c, int count, int c1, int c2, int c3) {
    int mapped[XP3_UNICODE_CASE_MAX];
    const char *kind = "lower";
    int got;
    char what[100];

    if (upper) {
        kind = "upper";
        got = xp3UnicodeUpper(c, mapped);
    } else {
        got = xp3UnicodeLower(c, mapped);
    }
    snprintf(what, sizeof(what), "%s-case mapping of U+%04X", kind,
             (unsigned) c);
    check((got == count) && (mapped[0] == c1) &&
          ((count < 2) || (mapped[1] == c2)) &&
          ((count < 3) || (mapped[2] == c3)), what);
}

static void
testCaseMappings(void) {
    /* Simple mappings, by ranges of every and of every second code
     * point */
    check(xp3UnicodeSimpleUpper('a') == 'A', "simple upper a");
    check(xp3UnicodeSimpleUpper('A') == 'A', "simple upper A");
    check(xp3UnicodeSimpleUpper('1') == '1', "simple upper 1");
    check(xp3UnicodeSimpleUpper(0x00B5) == 0x039C, "simple upper micro");
    check(xp3UnicodeSimpleUpper(0x00FF) == 0x0178, "simple upper y diaeresis");
    check(xp3UnicodeSimpleUpper(0x0101) == 0x0100, "simple upper a macron");
    check(xp3UnicodeSimpleUpper(0x0100) == 0x0100, "simple upper A macron");
    check(xp3UnicodeSimpleUpper(0x00DF) == 0x00DF, "no simple upper sharp s");
    check(xp3UnicodeSimpleLower(0x0100) == 0x0101, "simple lower A macron");
    check(xp3UnicodeSimpleLower(0x0101) == 0x0101, "simple lower a macron");
    check(xp3UnicodeSimpleLower(0x0130) == 'i', "simple lower I dot");
    check(xp3UnicodeSimpleLower(0x212A) == 'k', "simple lower Kelvin");
    check(xp3UnicodeSimpleLower(0x1E9E) == 0x00DF, "simple lower sharp S");
    check(xp3UnicodeSimpleLower(0x10400) == 0x10428, "simple lower Deseret");
    check(xp3UnicodeSimpleLower(-1) == -1, "no mapping for -1");

    /* Full mappings, which may be longer */
    checkCase(1, 'a', 1, 'A', 0, 0);
    checkCase(1, 0x00DF, 2, 'S', 'S', 0);
    checkCase(1, 0xFB03, 3, 'F', 'F', 'I');
    checkCase(1, 0x0149, 2, 0x02BC, 'N', 0);
    checkCase(1, 0x1F80, 2, 0x1F08, 0x0399, 0);
    checkCase(1, 0x0131, 1, 'I', 0, 0);
    checkCase(1, 0x0378, 1, 0x0378, 0, 0);
    checkCase(0, 0x0130, 2, 'i', 0x0307, 0);
    checkCase(0, 'A', 1, 'a', 0, 0);
    checkCase(0, 0x1E9E, 1, 0x00DF, 0, 0);
    /* No tailoring, and no conditions: final sigma isn't applied */
    checkCase(0, 0x03A3, 1, 0x03C3, 0, 0);
    checkCase(0, 'I', 1, 'i', 0, 0);
}

/*
 * Case variants
 */

/* Whether v is one of the case variants of c */
static int
hasVariant(int c, int v) {
    int variants[XP3_UNICODE_VARIANTS_MAX];
    int count = xp3UnicodeCaseVariants(c, variants);
    int i;

    for (i = 0; i < count; i++) {
        if (variants[i] == v)
            return(1);
    }
    return(0);
}

static int
variantCount(int c) {
    int variants[XP3_UNICODE_VARIANTS_MAX];

    return(xp3UnicodeCaseVariants(c, variants));
}

/* Whether v is a case variant of c by the definition of F&O 3.1, 5.6.2,
 * with the full mappings */
static int
isVariantByDefinition(int c, int v) {
    int mapped1[XP3_UNICODE_CASE_MAX];
    int mapped2[XP3_UNICODE_CASE_MAX];
    int count1 = xp3UnicodeLower(c, mapped1);
    int count2 = xp3UnicodeLower(v, mapped2);

    if ((count1 == count2) &&
        (memcmp(mapped1, mapped2, count1 * sizeof(int)) == 0))
        return(1);
    count1 = xp3UnicodeUpper(c, mapped1);
    count2 = xp3UnicodeUpper(v, mapped2);
    return((count1 == count2) &&
           (memcmp(mapped1, mapped2, count1 * sizeof(int)) == 0));
}

static void
testCaseVariants(void) {
    int c;
    int unsound = 0;
    int asymmetric = 0;

    check((variantCount('k') == 2) && hasVariant('k', 'K') &&
          hasVariant('k', 0x212A), "k: K and the Kelvin sign");
    check((variantCount('K') == 2) && hasVariant('K', 'k') &&
          hasVariant('K', 0x212A), "K: k and the Kelvin sign");
    check((variantCount(0x212A) == 2) && hasVariant(0x212A, 'k') &&
          hasVariant(0x212A, 'K'), "the Kelvin sign: k and K");
    check(hasVariant(0x00DF, 0x1E9E) && hasVariant(0x1E9E, 0x00DF) &&
          !hasVariant(0x00DF, 'S'), "sharp s and capital sharp S");
    check((variantCount('a') == 1) && hasVariant('a', 'A'), "a: A");
    check((variantCount(0x03C3) == 2) && hasVariant(0x03C3, 0x03A3) &&
          hasVariant(0x03C3, 0x03C2), "sigma: capital and final sigma");
    check(variantCount(0x0130) == 0,
          "capital I with dot: none, as its lower case has two characters");
    check((variantCount('1') == 0) && (variantCount(0x0378) == 0) &&
          (variantCount(0x4E01) == 0), "none for caseless characters");

    /* Every variant fits the definition, and the relation is symmetric */
    for (c = 0; c <= 0x10FFFF; c++) {
        int variants[XP3_UNICODE_VARIANTS_MAX];
        int count = xp3UnicodeCaseVariants(c, variants);
        int i;

        for (i = 0; i < count; i++) {
            if (!isVariantByDefinition(c, variants[i]))
                unsound++;
            if (!hasVariant(variants[i], c))
                asymmetric++;
        }
    }
    check(unsound == 0, "every case variant fits the definition");
    check(asymmetric == 0, "case variants are symmetric");
}

/*
 * Normalization
 */

/* Room for the results of the tests */
typedef struct {
    xmlChar data[512];
    size_t len;
    int writes;
    int stopAfter;              /* stop at this write, or -1 */
} xp3TestOutput;

static int
collect(void *data, const xmlChar *bytes, size_t len) {
    xp3TestOutput *out = data;

    if (out->writes++ == out->stopAfter)
        return(-1);
    if (out->len + len > sizeof(out->data))
        return(-1);
    memcpy(out->data + out->len, bytes, len);
    out->len += len;
    return(0);
}

static const char *const formNames[] = { "NFC", "NFD", "NFKC", "NFKD" };

static void
checkNormalized(const char *in, xp3UnicodeForm form, const char *expected) {
    xp3TestOutput out;
    char what[200];
    int ret;

    out.len = 0;
    out.writes = 0;
    out.stopAfter = -1;
    ret = xp3UnicodeNormalize(BAD_CAST in, strlen(in), form, collect, &out);
    snprintf(what, sizeof(what), "%s of \"%.40s\"", formNames[form], in);
    check((ret == 0) && (out.len == strlen(expected)) &&
          (memcmp(out.data, expected, out.len) == 0), what);
}

/* A run of combining marks longer than both the segment kept on the
 * stack and the runs sorted by insertion: a with dot below and acute,
 * 40 times each, alternating */
static char longRun[1 + 40 * 4 + 1];
static char longRunNfd[1 + 40 * 4 + 1];
static char longRunNfc[3 + 39 * 2 + 40 * 2 + 1];

static void
makeLongRun(void) {
    size_t used = 0;
    int i;

    longRun[used++] = 'a';
    for (i = 0; i < 40; i++) {
        memcpy(longRun + used, "\xcc\x81\xcc\xa3", 4);
        used += 4;
    }
    longRun[used] = 0;

    used = 0;
    longRunNfd[used++] = 'a';
    for (i = 0; i < 40; i++) {
        memcpy(longRunNfd + used, "\xcc\xa3", 2);
        used += 2;
    }
    for (i = 0; i < 40; i++) {
        memcpy(longRunNfd + used, "\xcc\x81", 2);
        used += 2;
    }
    longRunNfd[used] = 0;

    /* a and the first dot below compose to U+1EA1; the other dots
     * below block each other, and no character has U+1EA1 with an
     * acute */
    memcpy(longRunNfc, "\xe1\xba\xa1", 3);
    used = 3;
    for (i = 0; i < 39; i++) {
        memcpy(longRunNfc + used, "\xcc\xa3", 2);
        used += 2;
    }
    for (i = 0; i < 40; i++) {
        memcpy(longRunNfc + used, "\xcc\x81", 2);
        used += 2;
    }
    longRunNfc[used] = 0;
}

static void
testNormalization(void) {
    int parts[XP3_UNICODE_DECOMPOSITION_MAX];

    /* The lookups */
    check(xp3UnicodeCombiningClass('a') == 0, "ccc of a");
    check(xp3UnicodeCombiningClass(0x0301) == 230, "ccc of acute");
    check(xp3UnicodeCombiningClass(0x0323) == 220, "ccc of dot below");
    check(xp3UnicodeCombiningClass(0x05B0) == 10, "ccc of sheva");
    check(xp3UnicodeCombiningClass(0x1D165) == 216, "ccc of a stem");
    check((xp3UnicodeDecompose(0x00C5, 0, parts) == 2) &&
          (parts[0] == 'A') && (parts[1] == 0x030A),
          "decomposition of A ring");
    check((xp3UnicodeDecompose(0x212B, 0, parts) == 2) &&
          (parts[0] == 'A') && (parts[1] == 0x030A),
          "full decomposition of the Angstrom sign");
    check((xp3UnicodeDecompose(0xFB01, 0, parts) == 1) &&
          (parts[0] == 0xFB01), "no canonical decomposition of fi");
    check((xp3UnicodeDecompose(0xFB01, 1, parts) == 2) &&
          (parts[0] == 'f') && (parts[1] == 'i'),
          "compatibility decomposition of fi");
    check(xp3UnicodeDecompose(0xFDFA, 1, parts) ==
          XP3_UNICODE_DECOMPOSITION_MAX, "the longest decomposition");
    check((xp3UnicodeDecompose(0xAC01, 0, parts) == 3) &&
          (parts[0] == 0x1100) && (parts[1] == 0x1161) &&
          (parts[2] == 0x11A8), "Hangul LVT syllable");
    check((xp3UnicodeDecompose(0xAC00, 1, parts) == 2) &&
          (parts[0] == 0x1100) && (parts[1] == 0x1161),
          "Hangul LV syllable");
    check(xp3UnicodeCompose('A', 0x030A) == 0x00C5, "compose A ring");
    check(xp3UnicodeCompose(0x0915, 0x093C) == -1,
          "Devanagari QA is excluded from composition");
    check(xp3UnicodeCompose(0x1100, 0x1161) == 0xAC00, "compose Hangul LV");
    check(xp3UnicodeCompose(0xAC00, 0x11A8) == 0xAC01, "compose Hangul LVT");
    check(xp3UnicodeCompose(0xAC01, 0x11A8) == -1, "no Hangul LVT + T");
    check(xp3UnicodeCompose('a', 'b') == -1, "no composite of a and b");

    /* A with ring, the Angstrom sign and their decomposed form */
    checkNormalized("\xe2\x84\xab", XP3_UNICODE_NFC, "\xc3\x85");
    checkNormalized("A\xcc\x8a", XP3_UNICODE_NFC, "\xc3\x85");
    checkNormalized("\xc3\x85", XP3_UNICODE_NFD, "A\xcc\x8a");
    checkNormalized("\xe2\x84\xab", XP3_UNICODE_NFKD, "A\xcc\x8a");
    /* Ohm sign, a singleton */
    checkNormalized("\xe2\x84\xa6", XP3_UNICODE_NFC, "\xce\xa9");
    /* Hangul */
    checkNormalized("\xea\xb0\x81", XP3_UNICODE_NFD,
                    "\xe1\x84\x80\xe1\x85\xa1\xe1\x86\xa8");
    checkNormalized("\xe1\x84\x80\xe1\x85\xa1\xe1\x86\xa8", XP3_UNICODE_NFC,
                    "\xea\xb0\x81");
    checkNormalized("\xea\xb0\x80\xe1\x86\xa8", XP3_UNICODE_NFC,
                    "\xea\xb0\x81");
    /* Reordering: dot below (220) goes before circumflex (230) */
    checkNormalized("a\xcc\x82\xcc\xa3", XP3_UNICODE_NFD,
                    "a\xcc\xa3\xcc\x82");
    checkNormalized("a\xcc\x82\xcc\xa3", XP3_UNICODE_NFC, "\xe1\xba\xad");
    /* UAX #15, figure 6: d with dot above and dot below */
    checkNormalized("\xe1\xb8\x8b\xcc\xa3", XP3_UNICODE_NFC,
                    "\xe1\xb8\x8d\xcc\x87");
    /* UAX #15, table 6: long s with dot above and dot below */
    checkNormalized("\xe1\xba\x9b\xcc\xa3", XP3_UNICODE_NFC,
                    "\xe1\xba\x9b\xcc\xa3");
    checkNormalized("\xe1\xba\x9b\xcc\xa3", XP3_UNICODE_NFD,
                    "\xc5\xbf\xcc\xa3\xcc\x87");
    checkNormalized("\xe1\xba\x9b\xcc\xa3", XP3_UNICODE_NFKC, "\xe1\xb9\xa9");
    checkNormalized("\xe1\xba\x9b\xcc\xa3", XP3_UNICODE_NFKD,
                    "s\xcc\xa3\xcc\x87");
    /* Compatibility forms */
    checkNormalized("\xef\xac\x81", XP3_UNICODE_NFKC, "fi");
    checkNormalized("\xef\xac\x81", XP3_UNICODE_NFC, "\xef\xac\x81");
    checkNormalized("x\xc2\xb2", XP3_UNICODE_NFKC, "x2");
    /* A composition exclusion stays decomposed */
    checkNormalized("\xe0\xa5\x98", XP3_UNICODE_NFC,
                    "\xe0\xa4\x95\xe0\xa4\xbc");
    /* A starter that composes with the starter before it */
    checkNormalized("\xe0\xa7\x87\xe0\xa6\xbe", XP3_UNICODE_NFC,
                    "\xe0\xa7\x8b");
    /* Unassigned code points and text without marks stay */
    checkNormalized("a\xcd\xb8" "b", XP3_UNICODE_NFKD, "a\xcd\xb8" "b");
    checkNormalized("", XP3_UNICODE_NFC, "");
    checkNormalized("plain text", XP3_UNICODE_NFKC, "plain text");
    /* A combining mark at the start has nothing to compose with */
    checkNormalized("\xcc\x8a" "A", XP3_UNICODE_NFC, "\xcc\x8a" "A");
    /* A byte that isn't UTF-8 is U+FFFD, copied as it is, and the
     * accent after it doesn't reach the a before it */
    checkNormalized("a\x82\xcc\x81", XP3_UNICODE_NFC, "a\x82\xcc\x81");
    checkNormalized("\xc3\x85\xff", XP3_UNICODE_NFD, "A\xcc\x8a\xff");
    /* A long run of combining marks, sorted by counting */
    checkNormalized(longRun, XP3_UNICODE_NFD, longRunNfd);
    checkNormalized(longRun, XP3_UNICODE_NFC, longRunNfc);
}

/* The output can stop normalization, as a limit does */
static void
testNormalizationStops(void) {
    xmlChar in[1000];
    xp3TestOutput out;
    int ret;

    memset(in, 'a', sizeof(in));
    out.len = 0;
    out.writes = 0;
    out.stopAfter = 1;
    ret = xp3UnicodeNormalize(in, sizeof(in), XP3_UNICODE_NFC, collect, &out);
    check((ret == XP3_UNICODE_STOPPED) && (out.writes == 2),
          "the output stops normalization");
}

/* Each allocation failing in turn: the segment on the heap and the
 * buffer for sorting by counting */
static void
testNormalizationAllocationFailures(void) {
    xp3TestOutput out;
    int ret;
    int i;

    if (!xp3TestAllocInstall()) {
        printf("testunicode: allocation-failure tests skipped: this "
               "libxml2 ignores xmlMemSetup()\n");
        xp3TestAllocRestore();
        return;
    }
    for (i = 0; i < 100; i++) {
        int injected;

        out.len = 0;
        out.writes = 0;
        out.stopAfter = -1;
        xp3TestAllocFailAt(i);
        ret = xp3UnicodeNormalize(BAD_CAST longRun, strlen(longRun),
                                  XP3_UNICODE_NFC, collect, &out);
        injected = xp3TestAllocFailed();
        xp3TestAllocFailAt(-1);
        if (!injected) {
            check((ret == 0) && (out.len == strlen(longRunNfc)) &&
                  (memcmp(out.data, longRunNfc, out.len) == 0),
                  "normalization complete after allocation failures");
            break;
        }
        check(ret == XP3_UNICODE_NO_MEMORY,
              "a failed allocation reports XP3_UNICODE_NO_MEMORY");
    }
    check((i > 1) && (i < 100), "allocation failure points exercised");
    xp3TestAllocRestore();
}

int
main(void) {
    xmlInitParser();
    makeLongRun();

    testCategories();
    testBlocks();
    testCaseMappings();
    testCaseVariants();
    testNormalization();
    testNormalizationStops();
    testNormalizationAllocationFailures();

    if (failures != 0)
        return(1);
    printf("testunicode: OK\n");
    return(0);
}
