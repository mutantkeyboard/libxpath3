/*
 * xp3unicode.c: Unicode character properties, case mappings and
 * normalization
 *
 * The tables are codegen/unicode.inc, generated from the Unicode
 * Character Database 18.0.0 by codegen/genUnicode.py: the general
 * categories, the blocks, the simple and full case mappings, the case
 * variants of the i flag of regular expressions (F&O 3.1, 5.6.2), and
 * the canonical combining classes, decompositions and composition
 * pairs that normalization needs. Each is sorted and searched by
 * binary search. Hangul syllables are composed and decomposed by the
 * arithmetic of The Unicode Standard, 3.12, not by table.
 *
 * Normalization follows UAX #15: decompose each character fully, put
 * each run of combining marks in the order of their combining classes,
 * and for NFC and NFKC compose again. The text is processed in
 * segments, each up to a starter that composes with nothing before it
 * (combining class 0, and not the second of a composition pair), as
 * nothing reorders or composes across such a starter. A segment is
 * written out when the next one starts, so memory stays proportional
 * to the longest segment, not to the text. A byte that isn't UTF-8 is
 * U+FFFD, as for the string functions (xp3chars.c), which is such a
 * starter, and it is copied as it is.
 *
 * Code points are ints, as xp3NextChar() returns them.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdint.h>
#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

#include "private/xp3chars.h"
#include "private/xp3unicode.h"

/* A run of code points with one general category */
typedef struct {
    uint32_t first;
    uint32_t last;
    uint8_t category;           /* an xp3UnicodeCategory */
} xp3UnicodeCategoryRange;

/* A block, by its name without spaces */
typedef struct {
    const char *name;
    uint32_t first;
    uint32_t last;
} xp3UnicodeBlockRange;

/* Code points from first to last, every stride-th of them, whose
 * simple case mapping is the code point plus delta */
typedef struct {
    uint32_t first;
    uint32_t last;
    int32_t delta;
    uint8_t stride;             /* 1 or 2 */
} xp3UnicodeDeltaRange;

/* A full case mapping that differs from the simple one */
typedef struct {
    uint32_t code;
    uint8_t length;
    uint32_t mapped[XP3_UNICODE_CASE_MAX];
} xp3UnicodeFullCase;

/* A code point with length code points of a pool, from start */
typedef struct {
    uint32_t code;
    uint16_t start;
    uint16_t length;
} xp3UnicodePoolEntry;

/* A run of code points with one canonical combining class */
typedef struct {
    uint32_t first;
    uint32_t last;
    uint8_t combining;
} xp3UnicodeCombiningRange;

/* A canonical composition pair and its composite */
typedef struct {
    uint32_t first;
    uint32_t second;
    uint32_t composite;
} xp3UnicodeComposition;

#include "codegen/unicode.inc"

#if (XP3_UNICODE_TABLES_CASE_MAX > XP3_UNICODE_CASE_MAX) || \
    (XP3_UNICODE_TABLES_VARIANTS_MAX > XP3_UNICODE_VARIANTS_MAX) || \
    (XP3_UNICODE_TABLES_DECOMPOSITION_MAX > XP3_UNICODE_DECOMPOSITION_MAX)
  #error "the Unicode tables need larger buffers than xp3unicode.h gives"
#endif

#define XP3_TABLE_SIZE(t) (sizeof(t) / sizeof((t)[0]))

/* Hangul syllables and jamo, The Unicode Standard, 3.12 */
#define XP3_HANGUL_S_BASE 0xAC00
#define XP3_HANGUL_L_BASE 0x1100
#define XP3_HANGUL_V_BASE 0x1161
#define XP3_HANGUL_T_BASE 0x11A7
#define XP3_HANGUL_L_COUNT 19
#define XP3_HANGUL_V_COUNT 21
#define XP3_HANGUL_T_COUNT 28
#define XP3_HANGUL_N_COUNT (XP3_HANGUL_V_COUNT * XP3_HANGUL_T_COUNT)
#define XP3_HANGUL_S_COUNT (XP3_HANGUL_L_COUNT * XP3_HANGUL_N_COUNT)

/* Indexed by xp3UnicodeCategory */
static const char *const xp3UnicodeCategoryNames[] = {
    "Lu", "Ll", "Lt", "Lm", "Lo", "Mn", "Mc", "Me", "Nd", "Nl",
    "No", "Pc", "Pd", "Ps", "Pe", "Pi", "Pf", "Po", "Sm", "Sc",
    "Sk", "So", "Zs", "Zl", "Zp", "Cc", "Cf", "Cs", "Co", "Cn"
};

/*
 * Lookups
 */

/**
 * Get the general category of a code point.
 *
 * @param c  the code point
 * @returns the category; Cn for an unassigned code point, and for
 *          anything that isn't a code point
 */
xp3UnicodeCategory
xp3UnicodeCategoryOf(int c) {
    size_t low = 0;
    size_t high = XP3_TABLE_SIZE(xp3UnicodeCategories);

    if (c < 0)
        return(XP3_UNICODE_CN);
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        const xp3UnicodeCategoryRange *range = &xp3UnicodeCategories[mid];

        if ((uint32_t) c < range->first)
            high = mid;
        else if ((uint32_t) c > range->last)
            low = mid + 1;
        else
            return((xp3UnicodeCategory) range->category);
    }
    return(XP3_UNICODE_CN);
}

/**
 * Get the two-letter name of a general category, such as "Lu".
 *
 * @param category  the category
 * @returns the name, or NULL for an invalid category
 */
const char *
xp3UnicodeCategoryName(xp3UnicodeCategory category) {
    if ((unsigned) category >= XP3_TABLE_SIZE(xp3UnicodeCategoryNames))
        return(NULL);
    return(xp3UnicodeCategoryNames[category]);
}

/**
 * Check whether a code point is in a one-letter group of general
 * categories, as \p{L} tests it: its category name starts with the
 * letter.
 *
 * @param c      the code point
 * @param group  'L', 'M', 'N', 'P', 'S', 'Z' or 'C'
 * @returns 1 if it is, 0 otherwise
 */
int
xp3UnicodeIsInGroup(int c, char group) {
    const char *name = xp3UnicodeCategoryNames[xp3UnicodeCategoryOf(c)];

    return(name[0] == group);
}

/* Compare a block name of the table with name, len bytes, as strcmp()
 * would compare them */
static int
compareBlockName(const char *entry, const xmlChar *name, size_t len) {
    size_t entryLen = strlen(entry);
    size_t common = len;
    int cmp;

    if (entryLen < common)
        common = entryLen;
    cmp = memcmp(entry, name, common);
    if (cmp != 0)
        return(cmp);
    if (entryLen < len)
        return(-1);
    if (entryLen > len)
        return(1);
    return(0);
}

/**
 * Find a block by its name as XSD regular expressions use it in
 * \p{IsX}: the name in Blocks.txt without spaces, such as
 * "BasicLatin" or "Latin-1Supplement". Names are case-sensitive.
 *
 * @param name   the name, without "Is"
 * @param len    its length in bytes
 * @param first  set to the first code point of the block
 * @param last   set to the last code point of the block
 * @returns 0 if found, -1 if there is no such block
 */
int
xp3UnicodeBlock(const xmlChar *name, size_t len, int *first, int *last) {
    size_t low = 0;
    size_t high = XP3_TABLE_SIZE(xp3UnicodeBlocks);

    while (low < high) {
        size_t mid = low + (high - low) / 2;
        const xp3UnicodeBlockRange *block = &xp3UnicodeBlocks[mid];
        int cmp = compareBlockName(block->name, name, len);

        if (cmp < 0) {
            low = mid + 1;
        } else if (cmp > 0) {
            high = mid;
        } else {
            *first = (int) block->first;
            *last = (int) block->last;
            return(0);
        }
    }
    return(-1);
}

/* The simple case mapping of a code point by a table of delta ranges:
 * the last range that starts at or before it holds it, or none does */
static int
applyDelta(const xp3UnicodeDeltaRange *table, size_t count, int c) {
    const xp3UnicodeDeltaRange *range;
    size_t low = 0;
    size_t high = count;

    if (c < 0)
        return(c);
    while (low < high) {
        size_t mid = low + (high - low) / 2;

        if (table[mid].first <= (uint32_t) c)
            low = mid + 1;
        else
            high = mid;
    }
    if (low == 0)
        return(c);
    range = &table[low - 1];
    if ((uint32_t) c > range->last)
        return(c);
    if (((uint32_t) c - range->first) % range->stride != 0)
        return(c);
    return(c + range->delta);
}

/**
 * Get the simple upper-case mapping of a code point (UnicodeData.txt).
 *
 * @param c  the code point
 * @returns the mapping, or c if it has none
 */
int
xp3UnicodeSimpleUpper(int c) {
    return(applyDelta(xp3UnicodeUpperDeltas,
                      XP3_TABLE_SIZE(xp3UnicodeUpperDeltas), c));
}

/**
 * Get the simple lower-case mapping of a code point (UnicodeData.txt).
 *
 * @param c  the code point
 * @returns the mapping, or c if it has none
 */
int
xp3UnicodeSimpleLower(int c) {
    return(applyDelta(xp3UnicodeLowerDeltas,
                      XP3_TABLE_SIZE(xp3UnicodeLowerDeltas), c));
}

/* The full case mapping of c in a table of those that differ from the
 * simple ones, or NULL if it isn't there */
static const xp3UnicodeFullCase *
findFullCase(const xp3UnicodeFullCase *table, size_t count, int c) {
    size_t low = 0;
    size_t high = count;

    while (low < high) {
        size_t mid = low + (high - low) / 2;

        if (table[mid].code == (uint32_t) c)
            return(&table[mid]);
        if (table[mid].code < (uint32_t) c)
            low = mid + 1;
        else
            high = mid;
    }
    return(NULL);
}

/* Copy a full case mapping, or the simple one if there is none.
 * Returns the number of code points. */
static int
fullCase(const xp3UnicodeFullCase *entry, int simple, int *mapped) {
    int i;

    if (entry == NULL) {
        mapped[0] = simple;
        return(1);
    }
    for (i = 0; i < entry->length; i++)
        mapped[i] = (int) entry->mapped[i];
    return(entry->length);
}

/**
 * Get the full upper-case mapping of a code point, without tailoring
 * (SpecialCasing.txt, unconditional mappings, then UnicodeData.txt), as
 * fn:upper-case uses it (F&O 3.1, 5.4.7): U+00DF becomes "SS".
 *
 * @param c       the code point
 * @param mapped  room for XP3_UNICODE_CASE_MAX code points; set to the
 *                mapping, c itself if it has none
 * @returns the number of code points of the mapping
 */
int
xp3UnicodeUpper(int c, int *mapped) {
    const xp3UnicodeFullCase *entry;

    entry = findFullCase(xp3UnicodeFullUpper,
                         XP3_TABLE_SIZE(xp3UnicodeFullUpper), c);
    return(fullCase(entry, xp3UnicodeSimpleUpper(c), mapped));
}

/**
 * Get the full lower-case mapping of a code point, without tailoring,
 * as fn:lower-case uses it (F&O 3.1, 5.4.8): U+0130 becomes "i" and
 * U+0307. Final sigma is a conditional mapping, so U+03A3 always
 * becomes U+03C3.
 *
 * @param c       the code point
 * @param mapped  room for XP3_UNICODE_CASE_MAX code points; set to the
 *                mapping, c itself if it has none
 * @returns the number of code points of the mapping
 */
int
xp3UnicodeLower(int c, int *mapped) {
    const xp3UnicodeFullCase *entry;

    entry = findFullCase(xp3UnicodeFullLower,
                         XP3_TABLE_SIZE(xp3UnicodeFullLower), c);
    return(fullCase(entry, xp3UnicodeSimpleLower(c), mapped));
}

/* The entry of c in a table of pool entries, or NULL */
static const xp3UnicodePoolEntry *
findPoolEntry(const xp3UnicodePoolEntry *table, size_t count, int c) {
    size_t low = 0;
    size_t high = count;

    while (low < high) {
        size_t mid = low + (high - low) / 2;

        if (table[mid].code == (uint32_t) c)
            return(&table[mid]);
        if (table[mid].code < (uint32_t) c)
            low = mid + 1;
        else
            high = mid;
    }
    return(NULL);
}

/* Copy the code points of a pool entry. Returns their number. */
static int
copyPoolEntry(const xp3UnicodePoolEntry *entry, const uint32_t *pool,
              int *out) {
    int i;

    for (i = 0; i < entry->length; i++)
        out[i] = (int) pool[entry->start + i];
    return(entry->length);
}

/**
 * Get the case variants of a code point, as the i flag of regular
 * expressions matches them (F&O 3.1, 5.6.2): the code points other
 * than c whose full lower-case mapping is that of c, or whose full
 * upper-case mapping is that of c. For k they are K and U+212A KELVIN
 * SIGN, for U+00DF U+1E9E and U+1DF95. For most code points they are
 * just the simple case mappings, so the table lists only the others.
 *
 * @param c         the code point
 * @param variants  room for XP3_UNICODE_VARIANTS_MAX code points; set
 *                  to the variants, in no particular order
 * @returns the number of variants, 0 if there are none
 */
int
xp3UnicodeCaseVariants(int c, int *variants) {
    const xp3UnicodePoolEntry *entry;
    int upper;
    int lower;
    int count = 0;

    entry = findPoolEntry(xp3UnicodeVariants,
                          XP3_TABLE_SIZE(xp3UnicodeVariants), c);
    if (entry != NULL)
        return(copyPoolEntry(entry, xp3UnicodeVariantPool, variants));
    upper = xp3UnicodeSimpleUpper(c);
    lower = xp3UnicodeSimpleLower(c);
    if (upper != c)
        variants[count++] = upper;
    if ((lower != c) && (lower != upper))
        variants[count++] = lower;
    return(count);
}

/**
 * Get the canonical combining class of a code point.
 *
 * @param c  the code point
 * @returns the class, 0 to 254; 0 for most characters
 */
int
xp3UnicodeCombiningClass(int c) {
    size_t low = 0;
    size_t high = XP3_TABLE_SIZE(xp3UnicodeCombiningClasses);

    /* The first combining mark is U+0300 */
    if (c < 0x300)
        return(0);
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        const xp3UnicodeCombiningRange *range =
            &xp3UnicodeCombiningClasses[mid];

        if ((uint32_t) c < range->first)
            high = mid;
        else if ((uint32_t) c > range->last)
            low = mid + 1;
        else
            return(range->combining);
    }
    return(0);
}

static int
isHangulSyllable(int c) {
    return((c >= XP3_HANGUL_S_BASE) &&
           (c < XP3_HANGUL_S_BASE + XP3_HANGUL_S_COUNT));
}

/**
 * Get the full decomposition of a code point: its decomposition
 * mapping applied until nothing changes, canonical only, or with the
 * compatibility mappings too, as NFD and NFKD use them. The code
 * points are not in canonical order yet.
 *
 * @param c              the code point
 * @param compatibility  1 for the compatibility decomposition, 0 for
 *                       the canonical one
 * @param decomposition  room for XP3_UNICODE_DECOMPOSITION_MAX code
 *                       points; set to the decomposition, c itself if
 *                       it has none
 * @returns the number of code points of the decomposition
 */
int
xp3UnicodeDecompose(int c, int compatibility, int *decomposition) {
    const xp3UnicodePoolEntry *entry = NULL;

    /* Nothing below U+00A0 decomposes */
    if (c < 0xA0) {
        decomposition[0] = c;
        return(1);
    }
    if (isHangulSyllable(c)) {
        int index = c - XP3_HANGUL_S_BASE;
        int trailing = index % XP3_HANGUL_T_COUNT;

        decomposition[0] = XP3_HANGUL_L_BASE + index / XP3_HANGUL_N_COUNT;
        decomposition[1] = XP3_HANGUL_V_BASE +
                           (index % XP3_HANGUL_N_COUNT) / XP3_HANGUL_T_COUNT;
        if (trailing == 0)
            return(2);
        decomposition[2] = XP3_HANGUL_T_BASE + trailing;
        return(3);
    }
    if (compatibility)
        entry = findPoolEntry(xp3UnicodeCompatibility,
                              XP3_TABLE_SIZE(xp3UnicodeCompatibility), c);
    if (entry == NULL)
        entry = findPoolEntry(xp3UnicodeCanonical,
                              XP3_TABLE_SIZE(xp3UnicodeCanonical), c);
    if (entry == NULL) {
        decomposition[0] = c;
        return(1);
    }
    return(copyPoolEntry(entry, xp3UnicodeDecompositionPool,
                         decomposition));
}

/* The Hangul syllable of a leading consonant and a vowel, or of an LV
 * syllable and a trailing consonant, or -1 */
static int
composeHangul(int first, int second) {
    int leading = first - XP3_HANGUL_L_BASE;
    int vowel = second - XP3_HANGUL_V_BASE;
    int trailing = second - XP3_HANGUL_T_BASE;

    if ((leading >= 0) && (leading < XP3_HANGUL_L_COUNT) &&
        (vowel >= 0) && (vowel < XP3_HANGUL_V_COUNT))
        return(XP3_HANGUL_S_BASE +
               (leading * XP3_HANGUL_V_COUNT + vowel) * XP3_HANGUL_T_COUNT);
    if (isHangulSyllable(first) &&
        ((first - XP3_HANGUL_S_BASE) % XP3_HANGUL_T_COUNT == 0) &&
        (trailing > 0) && (trailing < XP3_HANGUL_T_COUNT))
        return(first + trailing);
    return(-1);
}

/* Whether a composition pair sorts before the pair first, second */
static int
pairBefore(const xp3UnicodeComposition *pair, uint32_t first,
           uint32_t second) {
    if (pair->first != first)
        return(pair->first < first);
    return(pair->second < second);
}

/**
 * Get the primary composite of two code points: the character whose
 * canonical decomposition they are, unless it is excluded from
 * composition (Full_Composition_Exclusion), as NFC and NFKC compose.
 *
 * @param first   the first code point, a starter
 * @param second  the second code point
 * @returns the composite, or -1 if there is none
 */
int
xp3UnicodeCompose(int first, int second) {
    size_t low = 0;
    size_t high = XP3_TABLE_SIZE(xp3UnicodeCompositions);
    int hangul = composeHangul(first, second);

    if (hangul >= 0)
        return(hangul);
    if ((first < 0) || (second < 0))
        return(-1);
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        const xp3UnicodeComposition *pair = &xp3UnicodeCompositions[mid];

        if ((pair->first == (uint32_t) first) &&
            (pair->second == (uint32_t) second))
            return((int) pair->composite);
        if (pairBefore(pair, (uint32_t) first, (uint32_t) second))
            low = mid + 1;
        else
            high = mid;
    }
    return(-1);
}

/*
 * Normalization
 */

/* Code points of a segment kept on the stack, before it needs the
 * heap */
#define XP3_SEGMENT_STACK 32

/* Runs of combining marks up to this long are sorted by insertion;
 * longer ones by counting, so that no input takes quadratic time */
#define XP3_INSERTION_MAX 16

/* Bytes of output collected before they are written */
#define XP3_OUTPUT_CHUNK 256

typedef struct {
    int compose;                /* NFC or NFKC */
    int compatibility;          /* NFKC or NFKD */
    int fixed[XP3_SEGMENT_STACK];
    int *chars;                 /* the segment: fixed, or on the heap */
    size_t len;
    size_t cap;
    int *scratch;               /* for counting sort, or NULL */
    size_t scratchCap;
    xmlChar out[XP3_OUTPUT_CHUNK];
    size_t outLen;
    xp3UnicodeOutput output;
    void *data;
} xp3Normalizer;

/* Whether a starter composes with a starter before it: the Hangul
 * vowels and trailing consonants, and those of the table */
static int
composesBackward(int c) {
    size_t low = 0;
    size_t high = XP3_TABLE_SIZE(xp3UnicodeComposingStarters);

    if ((c >= XP3_HANGUL_V_BASE) &&
        (c < XP3_HANGUL_V_BASE + XP3_HANGUL_V_COUNT))
        return(1);
    if ((c > XP3_HANGUL_T_BASE) &&
        (c < XP3_HANGUL_T_BASE + XP3_HANGUL_T_COUNT))
        return(1);
    while (low < high) {
        size_t mid = low + (high - low) / 2;

        if (xp3UnicodeComposingStarters[mid] == (uint32_t) c)
            return(1);
        if (xp3UnicodeComposingStarters[mid] < (uint32_t) c)
            low = mid + 1;
        else
            high = mid;
    }
    return(0);
}

/* Whether a code point of the decomposed text starts a new segment:
 * nothing before it reorders or composes with it or anything after */
static int
startsSegment(int c) {
    return((xp3UnicodeCombiningClass(c) == 0) && !composesBackward(c));
}

/* Append a code point to the segment. Returns 0, or
 * XP3_UNICODE_NO_MEMORY. */
static int
segmentAppend(xp3Normalizer *norm, int c) {
    if (norm->len == norm->cap) {
        size_t cap = norm->cap * 2;
        int *chars;

        if (norm->cap > SIZE_MAX / 2 / sizeof(int))
            return(XP3_UNICODE_NO_MEMORY);
        if (norm->chars == norm->fixed) {
            chars = xmlMalloc(cap * sizeof(int));
            if (chars != NULL)
                memcpy(chars, norm->fixed, norm->len * sizeof(int));
        } else {
            chars = xmlRealloc(norm->chars, cap * sizeof(int));
        }
        if (chars == NULL)
            return(XP3_UNICODE_NO_MEMORY);
        norm->chars = chars;
        norm->cap = cap;
    }
    norm->chars[norm->len++] = c;
    return(0);
}

/* Write the collected output. Returns 0, or XP3_UNICODE_STOPPED. */
static int
flushOutput(xp3Normalizer *norm) {
    int ret = 0;

    if (norm->outLen > 0)
        ret = norm->output(norm->data, norm->out, norm->outLen);
    norm->outLen = 0;
    if (ret < 0)
        return(XP3_UNICODE_STOPPED);
    return(0);
}

/* Add up to 4 bytes to the output. Returns 0, or XP3_UNICODE_STOPPED. */
static int
outputBytes(xp3Normalizer *norm, const xmlChar *bytes, size_t len) {
    if (norm->outLen + len > XP3_OUTPUT_CHUNK) {
        if (flushOutput(norm) < 0)
            return(XP3_UNICODE_STOPPED);
    }
    memcpy(norm->out + norm->outLen, bytes, len);
    norm->outLen += len;
    return(0);
}

/* Sort a run of combining marks by their classes, keeping the order
 * of equal ones, by insertion */
static void
insertionSort(int *run, size_t len) {
    size_t i;

    for (i = 1; i < len; i++) {
        int c = run[i];
        int combining = xp3UnicodeCombiningClass(c);
        size_t to = i;

        while ((to > 0) &&
               (xp3UnicodeCombiningClass(run[to - 1]) > combining)) {
            run[to] = run[to - 1];
            to--;
        }
        run[to] = c;
    }
}

/* The same by counting, in linear time, with a scratch buffer kept for
 * the next long run. Returns 0, or XP3_UNICODE_NO_MEMORY. */
static int
countingSort(xp3Normalizer *norm, int *run, size_t len) {
    size_t starts[256];
    size_t total = 0;
    size_t i;

    if (len > norm->scratchCap) {
        int *scratch;

        if (len > SIZE_MAX / sizeof(int))
            return(XP3_UNICODE_NO_MEMORY);
        scratch = xmlRealloc(norm->scratch, len * sizeof(int));
        if (scratch == NULL)
            return(XP3_UNICODE_NO_MEMORY);
        norm->scratch = scratch;
        norm->scratchCap = len;
    }
    memset(starts, 0, sizeof(starts));
    for (i = 0; i < len; i++)
        starts[xp3UnicodeCombiningClass(run[i])]++;
    for (i = 0; i < 256; i++) {
        size_t count = starts[i];

        starts[i] = total;
        total += count;
    }
    for (i = 0; i < len; i++) {
        int combining = xp3UnicodeCombiningClass(run[i]);

        norm->scratch[starts[combining]++] = run[i];
    }
    memcpy(run, norm->scratch, len * sizeof(int));
    return(0);
}

/* Put each run of combining marks of the segment in canonical order.
 * Returns 0, or XP3_UNICODE_NO_MEMORY. */
static int
reorderSegment(xp3Normalizer *norm) {
    size_t pos = 0;

    while (pos < norm->len) {
        size_t end;

        if (xp3UnicodeCombiningClass(norm->chars[pos]) == 0) {
            pos++;
            continue;
        }
        end = pos + 1;
        while ((end < norm->len) &&
               (xp3UnicodeCombiningClass(norm->chars[end]) != 0))
            end++;
        if (end - pos <= XP3_INSERTION_MAX) {
            insertionSort(norm->chars + pos, end - pos);
        } else {
            if (countingSort(norm, norm->chars + pos, end - pos) < 0)
                return(XP3_UNICODE_NO_MEMORY);
        }
        pos = end;
    }
    return(0);
}

/*
 * Compose the segment in place (UAX #15, the canonical composition
 * algorithm): each code point combines with the last starter before
 * it if nothing in between blocks it, that is, if every code point in
 * between has a lower combining class, or it follows the starter
 * directly.
 */
static void
composeSegment(xp3Normalizer *norm) {
    int *chars = norm->chars;
    size_t starter = 0;
    size_t used = 1;
    int lastClass;
    size_t i;

    if (norm->len == 0)
        return;
    lastClass = xp3UnicodeCombiningClass(chars[0]);
    /* A segment that starts with a combining mark has no starter to
     * compose with until the next starter */
    if (lastClass != 0)
        lastClass = 256;
    for (i = 1; i < norm->len; i++) {
        int c = chars[i];
        int combining = xp3UnicodeCombiningClass(c);
        int composite = -1;

        if ((lastClass < combining) || (lastClass == 0))
            composite = xp3UnicodeCompose(chars[starter], c);
        if (composite >= 0) {
            chars[starter] = composite;
            continue;
        }
        if (combining == 0)
            starter = used;
        lastClass = combining;
        chars[used++] = c;
    }
    norm->len = used;
}

/* Reorder and compose the segment, and add it to the output. Returns
 * 0, or an error of xp3UnicodeNormalize(). */
static int
finishSegment(xp3Normalizer *norm) {
    size_t i;

    if (reorderSegment(norm) < 0)
        return(XP3_UNICODE_NO_MEMORY);
    if (norm->compose)
        composeSegment(norm);
    for (i = 0; i < norm->len; i++) {
        xmlChar bytes[4];
        int count = xp3EncodeChar(norm->chars[i], bytes);

        if (outputBytes(norm, bytes, (size_t) count) < 0)
            return(XP3_UNICODE_STOPPED);
    }
    norm->len = 0;
    return(0);
}

/* Decompose a character into the segment, finishing the segment
 * before each code point that starts a new one. Returns 0, or an error
 * of xp3UnicodeNormalize(). */
static int
addChar(xp3Normalizer *norm, int c) {
    int parts[XP3_UNICODE_DECOMPOSITION_MAX];
    int count = xp3UnicodeDecompose(c, norm->compatibility, parts);
    int i;

    for (i = 0; i < count; i++) {
        if (startsSegment(parts[i])) {
            int ret = finishSegment(norm);

            if (ret < 0)
                return(ret);
        }
        if (segmentAppend(norm, parts[i]) < 0)
            return(XP3_UNICODE_NO_MEMORY);
    }
    return(0);
}

/**
 * Normalize a string to one of the forms of UAX #15, as
 * fn:normalize-unicode does (F&O 3.1, 5.4.6). The result is written
 * in pieces to output, which may stop the work, for example at a
 * limit. A byte that isn't UTF-8 counts as U+FFFD and is written as it
 * is. Unassigned code points stay unchanged.
 *
 * @param str     the string
 * @param len     its length in bytes
 * @param form    the normalization form
 * @param output  called with each piece of the result
 * @param data    passed to output
 * @returns 0, XP3_UNICODE_NO_MEMORY if memory allocation failed, or
 *          XP3_UNICODE_STOPPED if output returned -1
 */
int
xp3UnicodeNormalize(const xmlChar *str, size_t len, xp3UnicodeForm form,
                    xp3UnicodeOutput output, void *data) {
    xp3Normalizer norm;
    size_t pos = 0;
    int ret = 0;

    norm.compose = (form == XP3_UNICODE_NFC) || (form == XP3_UNICODE_NFKC);
    norm.compatibility = (form == XP3_UNICODE_NFKC) ||
                         (form == XP3_UNICODE_NFKD);
    norm.chars = norm.fixed;
    norm.len = 0;
    norm.cap = XP3_SEGMENT_STACK;
    norm.scratch = NULL;
    norm.scratchCap = 0;
    norm.outLen = 0;
    norm.output = output;
    norm.data = data;

    while ((ret == 0) && (pos < len)) {
        size_t start = pos;
        int c = xp3NextChar(str, len, &pos);

        if ((pos - start == 1) && (str[start] >= 0x80)) {
            /* U+FFFD starts a segment and composes with nothing */
            ret = finishSegment(&norm);
            if (ret == 0)
                ret = outputBytes(&norm, str + start, 1);
        } else {
            ret = addChar(&norm, c);
        }
    }
    if (ret == 0)
        ret = finishSegment(&norm);
    if (ret == 0)
        ret = flushOutput(&norm);

    if (norm.chars != norm.fixed)
        xmlFree(norm.chars);
    xmlFree(norm.scratch);
    return(ret);
}
