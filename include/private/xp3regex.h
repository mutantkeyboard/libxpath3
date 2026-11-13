/**
 * @file
 *
 * @brief Regular expressions of F&O 3.1 (internal)
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XP3REGEX_H
#define XP3_XP3REGEX_H

#include <stddef.h>
#include <stdint.h>

#include <libxml/xmlstring.h>

#include <libxpath3/xpath3.h>
#include "private/xp3error.h"

/*
 * Use. xp3RegexCompile() turns a pattern and its flags into a compiled
 * regular expression, which is immutable: several threads may search
 * with one at the same time. xp3RegexSearch() finds the leftmost match
 * at or after a byte offset of a subject, and the span of each group.
 * The state of a search lives in a scratch object (xp3RegexScratch),
 * which belongs to one thread; reusing it for the searches of one
 * function call saves allocating it again for each. The work of a
 * search is counted in operations and charged to the caller, a step
 * per XP3_REGEX_OPERATIONS_PER_STEP of them, through xp3RegexSteps.
 *
 * Strings are UTF-8 with a length in bytes, and offsets are byte
 * offsets into them. A byte that isn't UTF-8 is one character, U+FFFD,
 * as for the string functions (xp3chars.c).
 */

/** A compiled regular expression */
typedef struct _xp3Regex xp3Regex;

/** The state of searches, for one thread */
typedef struct _xp3RegexScratch xp3RegexScratch;

/**
 * The part of the subject a group matched, as byte offsets: from
 * start up to, not including, end. Both are -1 for a group that took
 * no part in the match.
 */
typedef struct {
    ptrdiff_t start;
    ptrdiff_t end;
} xp3RegexSpan;

/** Searches charge a step per this many operations (D26, D34) */
#define XP3_REGEX_OPERATIONS_PER_STEP 256

/**
 * Charges steps to the caller of a search. Returns 0 to go on, or -1
 * to stop the search, which then fails with XP3_REGEX_LIMIT.
 */
typedef int (*xp3RegexCharge)(void *data, uint64_t steps);

/**
 * Counts the work of searches. An operation is one instruction a
 * matcher runs: in the Pike VM, a thread taking a step and an
 * instruction visited while adding a thread; in the backtracking
 * matcher, an instruction run and a saved state resumed. The caller
 * sets charge and data and zeroes the counts; operations left over
 * from one search are charged by the next one that uses the same
 * struct.
 */
typedef struct {
    xp3RegexCharge charge;  /**< NULL to count without a limit */
    void *data;             /**< passed to charge */
    uint64_t operations;    /**< all operations counted */
    uint64_t charged;       /**< operations charged as steps so far */
} xp3RegexSteps;

/** xp3RegexSearch() failed to allocate memory */
#define XP3_REGEX_NO_MEMORY (-1)
/** The charge function of xp3RegexSteps stopped the search */
#define XP3_REGEX_LIMIT (-2)

/**
 * Compile a pattern with flags (F&O 3.1, 5.6). Errors are FORX0001 for
 * the flags, FORX0002 for the pattern, LIMIT for one nested too deeply
 * or one whose program is too large, and NO_MEMORY.
 */
xp3Regex *
xp3RegexCompile(const xmlChar *pattern, size_t len, const xmlChar *flags,
                size_t flagsLen, xp3LanguageLevel level, xp3Error **error);

/** Free a compiled regular expression. NULL is ignored. */
void
xp3RegexFree(xp3Regex *regex);

/** The number of capturing groups, not counting the whole match */
int
xp3RegexGroupCount(const xp3Regex *regex);

/** Create the scratch state for searches with one regular expression */
xp3RegexScratch *
xp3RegexNewScratch(const xp3Regex *regex);

/** Free a scratch state. NULL is ignored. */
void
xp3RegexFreeScratch(xp3RegexScratch *scratch);

/**
 * Find the leftmost match at or after an offset of the subject, with a
 * scratch state, or NULL to allocate one for this search. Returns 1
 * and the groups for a match, 0 for none, or XP3_REGEX_NO_MEMORY or
 * XP3_REGEX_LIMIT.
 */
int
xp3RegexSearch(const xp3Regex *regex, xp3RegexScratch *scratch,
               const xmlChar *subject, size_t len, size_t start,
               xp3RegexSpan *groups, xp3RegexSteps *steps);

/**
 * xp3RegexSearch() with the backtracking matcher, whatever the
 * pattern, so that tests and the fuzzer can compare the two matchers.
 */
int
xp3RegexSearchBacktracking(const xp3Regex *regex, xp3RegexScratch *scratch,
                           const xmlChar *subject, size_t len, size_t start,
                           xp3RegexSpan *groups, xp3RegexSteps *steps);

#endif /* XP3_XP3REGEX_H */
