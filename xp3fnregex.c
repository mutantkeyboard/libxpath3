/*
 * xp3fnregex.c: the functions that use regular expressions
 *
 * Implementations of fn:matches, fn:replace and fn:tokenize#2 and #3
 * of F&O 3.1, 5.6,
 * https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/, bound
 * to their signatures in xp3func.c, with the regular expressions of
 * xp3regex.c. fn:tokenize#1 splits at whitespace without a pattern and
 * is in xp3fnstring.c; fn:analyze-string, which builds nodes, comes
 * with Part 12 (doc/DESIGN.md 6.4).
 *
 * A pattern is compiled once per evaluation. The evaluation keeps a
 * small cache, keyed by the pattern and the flags, of compiled
 * regular expressions with the state of their searches, and frees it
 * at its end, as it does its document order. A compiled expression is
 * shared by threads and never written, so the cache can't live in it.
 * Errors in the pattern and the flags (FORX0001, FORX0002) are dynamic
 * errors of the call, so constant folding leaves a call that raises
 * one to the evaluation.
 *
 * Matching and compiling count against the steps of the evaluation, a
 * step per XP3_REGEX_OPERATIONS_PER_STEP operations of the matchers or
 * instructions compiled, through one counter that the cache keeps for
 * the whole evaluation, so that the many short searches of fn:tokenize
 * and fn:replace add up. A pattern found in the cache costs only the
 * reading of its bytes. As for the other string functions, each call
 * also counts the bytes of the strings it reads and makes
 * (xp3FuncBytes()).
 *
 * Offsets are bytes. A byte of the input that isn't UTF-8 is matched
 * as U+FFFD and copied unchanged, as the string functions do.
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

#include "private/xp3error.h"
#include "private/xp3func.h"
#include "private/xp3regex.h"
#include "private/xp3types.h"
#include "private/xp3value.h"

/*
 * The compiled patterns of an evaluation
 */

/* Compiled patterns an evaluation keeps. When all are taken, a new
 * one replaces the one added longest ago, so a loop over more patterns
 * than this compiles each of them on every call. */
#define XP3_REGEX_CACHE_SIZE 8

/* A compiled pattern and what its searches need */
typedef struct {
    xmlChar *key;               /* the pattern, a NUL, the flags, a NUL;
                                 * NULL for an unused entry */
    size_t patternLen;
    size_t flagsLen;
    xp3Regex *regex;
    xp3RegexScratch *scratch;
    xp3RegexSpan *groups;       /* the groups of the last match */
    int matchesEmpty;           /* whether the pattern matches the empty
                                 * string (FORX0003), -1 until known */
} xp3RegexEntry;

struct _xp3RegexCache {
    xp3RegexEntry entries[XP3_REGEX_CACHE_SIZE];
    int next;                   /* the entry to replace next */
    xp3RegexSteps steps;        /* the work of every search */
};

/**
 * Create the cache of compiled patterns of an evaluation, empty.
 *
 * @returns the cache, to free with xp3RegexCacheFree(), or NULL if
 *          memory allocation failed
 */
xp3RegexCache *
xp3RegexCacheNew(void) {
    xp3RegexCache *cache = xmlMalloc(sizeof(*cache));

    if (cache == NULL)
        return(NULL);
    memset(cache, 0, sizeof(*cache));
    return(cache);
}

static void
entryClear(xp3RegexEntry *entry) {
    xmlFree(entry->key);
    xmlFree(entry->groups);
    xp3RegexFreeScratch(entry->scratch);
    xp3RegexFree(entry->regex);
    memset(entry, 0, sizeof(*entry));
}

/**
 * Free the cache of compiled patterns and everything in it.
 *
 * @param cache  the cache, or NULL
 */
void
xp3RegexCacheFree(xp3RegexCache *cache) {
    int i;

    if (cache == NULL)
        return;
    for (i = 0; i < XP3_REGEX_CACHE_SIZE; i++)
        entryClear(&cache->entries[i]);
    xmlFree(cache);
}

/* Whether an entry holds a pattern with these flags */
static int
entryHas(const xp3RegexEntry *entry, const xmlChar *pattern,
         size_t patternLen, const xmlChar *flags, size_t flagsLen) {
    const xmlChar *entryFlags;

    if (entry->key == NULL)
        return(0);
    if ((entry->patternLen != patternLen) || (entry->flagsLen != flagsLen))
        return(0);
    entryFlags = entry->key + patternLen + 1;
    return((memcmp(entry->key, pattern, patternLen) == 0) &&
           (memcmp(entryFlags, flags, flagsLen) == 0));
}

/*
 * Fill an empty entry with a compiled pattern, which it takes over,
 * also on failure. Returns 0, or -1 if memory allocation failed; the
 * entry is empty again then.
 */
static int
entryFill(xp3RegexEntry *entry, xp3Regex *regex, const xmlChar *pattern,
          size_t patternLen, const xmlChar *flags, size_t flagsLen) {
    size_t spans = (size_t) xp3RegexGroupCount(regex) + 1;
    xmlChar *entryFlags;

    entry->regex = regex;
    entry->matchesEmpty = -1;
    entry->key = xmlMalloc(patternLen + flagsLen + 2);
    entry->scratch = xp3RegexNewScratch(regex);
    entry->groups = xmlMalloc(spans * sizeof(xp3RegexSpan));
    if ((entry->key == NULL) || (entry->scratch == NULL) ||
        (entry->groups == NULL)) {
        entryClear(entry);
        return(-1);
    }
    entry->patternLen = patternLen;
    entry->flagsLen = flagsLen;
    memcpy(entry->key, pattern, patternLen);
    entry->key[patternLen] = 0;
    entryFlags = entry->key + patternLen + 1;
    memcpy(entryFlags, flags, flagsLen);
    entryFlags[flagsLen] = 0;
    return(0);
}

/* Charge the steps of searches to the evaluation of a call. Returns
 * 0, or -1 after recording LIMIT, which stops the search. */
static int
chargeSteps(void *data, uint64_t steps) {
    return(xp3FuncSteps(data, steps));
}

/* Record the error of compiling a pattern as the error of the call,
 * and free it */
static void
compileError(xp3FuncCall *call, xp3Error *error) {
    if ((error == NULL) || (error->code == XP3_ERR_NO_MEMORY))
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
    else if (error->message == NULL)
        xp3FuncError(call, error->code, "invalid regular expression");
    else
        xp3FuncError(call, error->code, "%s",
                     (const char *) error->message);
    xp3ErrorFree(error);
}

/*
 * The compiled pattern of a call (F&O 3.1, 5.6.1 and 5.6.2), from the
 * cache of the evaluation, or compiled and added to it. Reading the
 * pattern and the flags counts as for any string, and compiling as
 * the searches do, by the operations it took. Sets *steps to the
 * counter of the evaluation's searches, charging this call. Returns
 * the entry, or NULL after recording FORX0001, FORX0002, LIMIT or
 * NO_MEMORY.
 */
static xp3RegexEntry *
callRegex(xp3FuncCall *call, const xmlChar *pattern, const xmlChar *flags,
          xp3RegexSteps **steps) {
    size_t patternLen = strlen((const char *) pattern);
    size_t flagsLen = strlen((const char *) flags);
    xp3RegexCache *cache;
    xp3RegexEntry *entry;
    xp3Regex *regex;
    xp3Error *error = NULL;
    int i;

    *steps = NULL;
    if (xp3FuncBytes(call, (uint64_t) patternLen + flagsLen) < 0)
        return(NULL);
    cache = xp3FuncRegexCache(call);
    if (cache == NULL)
        return(NULL);
    cache->steps.charge = chargeSteps;
    cache->steps.data = call;
    *steps = &cache->steps;

    for (i = 0; i < XP3_REGEX_CACHE_SIZE; i++) {
        entry = &cache->entries[i];
        if (entryHas(entry, pattern, patternLen, flags, flagsLen))
            return(entry);
    }

    regex = xp3RegexCompile(pattern, patternLen, flags, flagsLen,
                            xp3FuncLevel(call), &error);
    if (regex == NULL) {
        compileError(call, error);
        return(NULL);
    }
    /* chargeSteps() records the step limit */
    if (xp3RegexChargeOperations(&cache->steps,
                                 xp3RegexCompileOperations(regex)) < 0) {
        xp3RegexFree(regex);
        return(NULL);
    }
    entry = &cache->entries[cache->next];
    entryClear(entry);
    if (entryFill(entry, regex, pattern, patternLen, flags, flagsLen) < 0) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(NULL);
    }
    cache->next = (cache->next + 1) % XP3_REGEX_CACHE_SIZE;
    return(entry);
}

/*
 * Searching
 */

/*
 * Find the leftmost match at or after a byte offset of the input, with
 * its groups in entry->groups. Returns 1 for a match, 0 for none, or
 * -1 after recording an error: LIMIT for the steps or for a program
 * too large to build, or NO_MEMORY.
 */
static int
searchFrom(xp3FuncCall *call, xp3RegexEntry *entry, xp3RegexSteps *steps,
           const xmlChar *input, size_t len, size_t start) {
    int ret = xp3RegexSearch(entry->regex, entry->scratch, input, len, start,
                             entry->groups, steps);

    if (ret >= 0)
        return(ret);
    if (ret == XP3_REGEX_TOO_LARGE)
        xp3FuncError(call, XP3_ERR_LIMIT, "regular expression too large");
    else if (ret == XP3_REGEX_NO_MEMORY)
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
    /* XP3_REGEX_LIMIT: chargeSteps() recorded the step limit already */
    return(-1);
}

static void
emptyMatchError(xp3FuncCall *call, const char *name) {
    xp3FuncError(call, XP3_ERR_FORX0003, "the pattern of %s matches the "
                 "empty string", name);
}

/*
 * Raise FORX0003 if the pattern matches the empty string, as
 * fn:replace and fn:tokenize must (F&O 3.1, 5.6.4 and 5.6.5): that
 * is, if matches("", $pattern, $flags) is true. The answer is kept
 * with the compiled pattern. Returns 0, or -1 after recording an
 * error.
 */
static int
checkNoEmptyMatch(xp3FuncCall *call, xp3RegexEntry *entry,
                  xp3RegexSteps *steps, const char *name) {
    if (entry->matchesEmpty < 0) {
        int found = searchFrom(call, entry, steps, BAD_CAST "", 0, 0);

        if (found < 0)
            return(-1);
        entry->matchesEmpty = found;
    }
    if (entry->matchesEmpty) {
        emptyMatchError(call, name);
        return(-1);
    }
    return(0);
}

/* The flags of a call whose flags come after arity other arguments,
 * or none if the call has no flags argument */
static const xmlChar *
callFlags(xp3Value **args, int count, int arity) {
    if (count > arity)
        return(xp3FuncString(args[arity]));
    return(BAD_CAST "");
}

/**
 * fn:matches#2 and #3 (F&O 3.1, 5.6.3): whether some part of a string
 * matches a pattern. The empty sequence is the empty string.
 *
 * @param call   the call
 * @param args   the string (xs:string?), the pattern and the flags
 *               (xs:string)
 * @param count  2 or 3
 * @returns an xs:boolean, or NULL on error
 */
xp3Value *
xp3FnMatches(xp3FuncCall *call, xp3Value **args, int count) {
    const xmlChar *input = xp3FuncString(args[0]);
    size_t len = strlen((const char *) input);
    xp3RegexEntry *entry;
    xp3RegexSteps *steps;
    int found;

    if (xp3FuncBytes(call, len) < 0)
        return(NULL);
    entry = callRegex(call, xp3FuncString(args[1]),
                      callFlags(args, count, 2), &steps);
    if (entry == NULL)
        return(NULL);
    found = searchFrom(call, entry, steps, input, len, 0);
    if (found < 0)
        return(NULL);
    return(xp3FuncAtomic(call, xp3AtomicNewBoolean(found)));
}

/*
 * Replacement strings
 */

/* What a part of the replacement string of fn:replace stands for */
typedef enum {
    XP3_PART_TEXT,              /* bytes of the replacement as they are */
    XP3_PART_GROUP,             /* $N: what a group matched */
    XP3_PART_INVALID            /* a $ or \ that FORX0004 rejects */
} xp3PartKind;

typedef struct {
    xp3PartKind kind;
    size_t start;               /* TEXT: the offset of the bytes;
                                 * INVALID: of the $ or \ */
    size_t len;                 /* TEXT: the number of bytes */
    int group;                  /* GROUP: 0 for the whole match; above
                                 * the number of groups for the empty
                                 * string */
} xp3ReplacementPart;

static int
isDigit(xmlChar c) {
    return((c >= '0') && (c <= '9'));
}

/*
 * The group that $ followed by digits refers to (F&O 3.1, 5.6.4). N is
 * the number all the digits make; while N is above both the number of
 * groups and 9, its last digit is text instead. As N only grows with
 * each digit, that takes the longest run of the digits whose number is
 * at most the larger of the two, and at least one digit. Sets *used to
 * the number of digits taken. Returns N, which is the empty string if
 * it is above the number of groups.
 */
static int
replacementGroup(const xmlChar *digits, size_t count, int groupCount,
                 size_t *used) {
    int64_t most = 9;
    int64_t value = digits[0] - '0';
    size_t taken = 1;

    if (groupCount > 9)
        most = groupCount;
    while (taken < count) {
        int64_t longer = value * 10 + (digits[taken] - '0');

        if (longer > most)
            break;
        value = longer;
        taken++;
    }
    *used = taken;
    return((int) value);
}

/*
 * Read the part of a replacement string at *pos and move *pos past it
 * (F&O 3.1, 5.6.4): a run of characters other than $ and \, the
 * escape \\ or \$, or $ and digits. A $ without a digit after it, or
 * a \ before anything but \ and $, is invalid (FORX0004); *pos moves
 * one byte past it.
 */
static void
nextPart(const xmlChar *repl, size_t len, size_t *pos, int groupCount,
         xp3ReplacementPart *part) {
    size_t at = *pos;
    size_t end = at;

    part->kind = XP3_PART_TEXT;
    part->start = at;
    part->len = 0;
    part->group = 0;
    if (repl[at] == '\\') {
        int escape = 0;

        if (at + 1 < len)
            escape = (repl[at + 1] == '\\') || (repl[at + 1] == '$');
        if (!escape) {
            part->kind = XP3_PART_INVALID;
            *pos = at + 1;
            return;
        }
        part->start = at + 1;
        part->len = 1;
        *pos = at + 2;
        return;
    }
    if (repl[at] == '$') {
        size_t digits = 0;
        size_t used;

        while ((at + 1 + digits < len) && isDigit(repl[at + 1 + digits]))
            digits++;
        if (digits == 0) {
            part->kind = XP3_PART_INVALID;
            *pos = at + 1;
            return;
        }
        part->kind = XP3_PART_GROUP;
        part->group = replacementGroup(repl + at + 1, digits, groupCount,
                                       &used);
        *pos = at + 1 + used;
        return;
    }
    while ((end < len) && (repl[end] != '$') && (repl[end] != '\\'))
        end++;
    part->len = end - at;
    *pos = end;
}

/* Check a replacement string used without the q flag (FORX0004).
 * Returns 0, or -1 after recording the error. */
static int
checkReplacement(xp3FuncCall *call, const xmlChar *repl, size_t len) {
    size_t pos = 0;

    while (pos < len) {
        xp3ReplacementPart part;

        nextPart(repl, len, &pos, 0, &part);
        if (part.kind != XP3_PART_INVALID)
            continue;
        if (repl[part.start] == '$')
            xp3FuncError(call, XP3_ERR_FORX0004, "\"$\" at offset %zu of "
                         "the replacement string is not followed by a "
                         "digit", part.start);
        else
            xp3FuncError(call, XP3_ERR_FORX0004, "\"\\\" at offset %zu of "
                         "the replacement string is not followed by "
                         "\"\\\" or \"$\"", part.start);
        return(-1);
    }
    return(0);
}

/* Append the replacement of a match, checked by checkReplacement(),
 * with each $N replaced by what its group matched. Returns 0, or -1
 * after recording an error. */
static int
appendReplacement(xp3FuncCall *call, xp3StrBuf *buf, const xmlChar *input,
                  const xp3RegexEntry *entry, const xmlChar *repl,
                  size_t len) {
    int groupCount = xp3RegexGroupCount(entry->regex);
    size_t pos = 0;

    while (pos < len) {
        xp3ReplacementPart part;
        int ret = 0;

        nextPart(repl, len, &pos, groupCount, &part);
        if (part.kind == XP3_PART_TEXT) {
            ret = xp3StrBufAppend(call, buf, repl + part.start, part.len);
        } else if ((part.kind == XP3_PART_GROUP) &&
                   (part.group <= groupCount)) {
            const xp3RegexSpan *span = &entry->groups[part.group];

            /* A group that took no part in the match is empty */
            if (span->start >= 0)
                ret = xp3StrBufAppend(call, buf, input + span->start,
                                      (size_t) (span->end - span->start));
        }
        if (ret < 0)
            return(-1);
    }
    return(0);
}

/*
 * Count bytes that are read again and again, as the replacement
 * string is for each match, by their total in *pending: a step per
 * XP3_BYTES_PER_STEP. Returns 0, or -1 after recording LIMIT.
 */
static int
countRepeatedRead(xp3FuncCall *call, uint64_t *pending, size_t bytes) {
    *pending += bytes;
    if (*pending < XP3_BYTES_PER_STEP)
        return(0);
    if (xp3FuncBytes(call, *pending) < 0)
        return(-1);
    *pending %= XP3_BYTES_PER_STEP;
    return(0);
}

/**
 * fn:replace#3 and #4 (F&O 3.1, 5.6.4): a string with each part that
 * matches a pattern replaced, from the left and without overlaps. In
 * the replacement, $N stands for what group N matched, $0 for the
 * whole match, and \$ and \\ for $ and \; with the q flag, the
 * replacement is used as it is. A pattern that matches the empty
 * string raises FORX0003, and a replacement with any other $ or \
 * FORX0004. The empty sequence is the empty string.
 *
 * @param call   the call
 * @param args   the string (xs:string?), the pattern, the replacement
 *               and the flags (xs:string)
 * @param count  3 or 4
 * @returns an xs:string, or NULL on error
 */
xp3Value *
xp3FnReplace(xp3FuncCall *call, xp3Value **args, int count) {
    const xmlChar *input = xp3FuncString(args[0]);
    const xmlChar *repl = xp3FuncString(args[2]);
    const xmlChar *flags = callFlags(args, count, 3);
    size_t len = strlen((const char *) input);
    size_t replLen = strlen((const char *) repl);
    int literal = (strchr((const char *) flags, 'q') != NULL);
    xp3RegexEntry *entry;
    xp3RegexSteps *steps;
    uint64_t pending = 0;
    size_t pos = 0;
    xp3StrBuf buf;

    if (xp3FuncBytes(call, (uint64_t) len + replLen) < 0)
        return(NULL);
    entry = callRegex(call, xp3FuncString(args[1]), flags, &steps);
    if (entry == NULL)
        return(NULL);
    /* The flags compiled, so a "q" in them is the flag */
    if ((!literal) && (checkReplacement(call, repl, replLen) < 0))
        return(NULL);
    if (checkNoEmptyMatch(call, entry, steps, "fn:replace") < 0)
        return(NULL);

    xp3StrBufInit(&buf, "fn:replace");
    while (pos < len) {
        const xp3RegexSpan *match = &entry->groups[0];
        int found = searchFrom(call, entry, steps, input, len, pos);
        int ret;

        if (found < 0)
            goto error;
        if (found == 0)
            break;
        /* Ruled out by checkNoEmptyMatch(), but would never end */
        if (match->end == match->start) {
            emptyMatchError(call, "fn:replace");
            goto error;
        }
        if (xp3StrBufAppend(call, &buf, input + pos,
                            (size_t) match->start - pos) < 0)
            goto error;
        if (literal) {
            ret = xp3StrBufAppend(call, &buf, repl, replLen);
        } else {
            ret = countRepeatedRead(call, &pending, replLen);
            if (ret == 0)
                ret = appendReplacement(call, &buf, input, entry, repl,
                                        replLen);
        }
        if (ret < 0)
            goto error;
        pos = (size_t) match->end;
    }
    if (xp3StrBufAppend(call, &buf, input + pos, len - pos) < 0)
        goto error;
    return(xp3StrBufFinish(call, &buf));

error:
    xmlFree(buf.data);
    return(NULL);
}

/*
 * Splitting strings
 */

/* Add a token to the result of fn:tokenize, a step for each. Returns
 * 0, or -1 after recording an error. */
static int
addToken(xp3FuncCall *call, xp3SeqBuilder *builder, const xmlChar *start,
         size_t len) {
    xp3Atomic *token = xp3AtomicNewString(XP3_TYPE_STRING, start, len);

    if (token == NULL) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(-1);
    }
    if (xp3FuncBuilderAdd(call, builder, xp3ItemFromAtomic(token)) < 0)
        return(-1);
    return(xp3FuncSteps(call, 1));
}

/**
 * fn:tokenize#2 and #3 (F&O 3.1, 5.6.5): the parts of a string between
 * the parts that match a pattern. A separator at the start or the end,
 * or two separators next to each other, make an empty token; the
 * empty string and the empty sequence have no tokens. A pattern that
 * matches the empty string raises FORX0003.
 *
 * @param call   the call
 * @param args   the string (xs:string?), the pattern and the flags
 *               (xs:string)
 * @param count  2 or 3
 * @returns the xs:string values, or NULL on error
 */
xp3Value *
xp3FnTokenizeRegex(xp3FuncCall *call, xp3Value **args, int count) {
    const xmlChar *input = xp3FuncString(args[0]);
    size_t len = strlen((const char *) input);
    xp3RegexEntry *entry;
    xp3RegexSteps *steps;
    xp3SeqBuilder builder;
    size_t pos = 0;

    /* The string is read, and its tokens together are no longer */
    if (xp3FuncBytes(call, 2 * (uint64_t) len) < 0)
        return(NULL);
    entry = callRegex(call, xp3FuncString(args[1]),
                      callFlags(args, count, 2), &steps);
    if (entry == NULL)
        return(NULL);
    if (checkNoEmptyMatch(call, entry, steps, "fn:tokenize") < 0)
        return(NULL);

    xp3FuncBuilderInit(call, &builder);
    if (len == 0)
        return(xp3FuncBuilderFinish(call, &builder));
    while (pos < len) {
        const xp3RegexSpan *match = &entry->groups[0];
        int found = searchFrom(call, entry, steps, input, len, pos);

        if (found < 0)
            goto error;
        if (found == 0)
            break;
        /* Ruled out by checkNoEmptyMatch(), but would never end */
        if (match->end == match->start) {
            emptyMatchError(call, "fn:tokenize");
            goto error;
        }
        if (addToken(call, &builder, input + pos,
                     (size_t) match->start - pos) < 0)
            goto error;
        pos = (size_t) match->end;
    }
    if (addToken(call, &builder, input + pos, len - pos) < 0)
        goto error;
    return(xp3FuncBuilderFinish(call, &builder));

error:
    xp3SeqBuilderClear(&builder);
    return(NULL);
}
