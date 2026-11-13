/*
 * xp3regex.c: a libFuzzer target to test regular expressions, and that
 * the two matchers agree
 *
 * The input starts with a three-byte header: the first byte selects
 * the language level (byte % 3), and the next two are a big-endian
 * allocation-failure position, 0 for none. The rest is the flags, a
 * NUL byte, the pattern, a NUL byte and the subject; without NUL bytes
 * it is all pattern, with no flags and an empty subject.
 *
 * Without an allocation failure, the pattern is compiled and every
 * match in the subject is found, one after another, as fn:tokenize
 * will find them. Each search runs on the matcher the pattern gets,
 * and again on the backtracking matcher, and the target aborts if both
 * finish and disagree on whether there is a match or on any group.
 * Both run with a step budget, so that slow patterns end with LIMIT
 * instead of a timeout; the backtracking matcher may hit its budget
 * where the Pike VM doesn't, as it can take exponential time. The
 * target also aborts on an error code compiling can't raise, and on a
 * group outside the subject.
 *
 * With an allocation failure, compiling and the first search must end
 * in NO_MEMORY, never in a result and never in silence.
 *
 * Built with -DXP3_FUZZ_DRIVER, the file gets a main() that replays
 * files given as arguments, as xp3parse.c does.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

#include <libxpath3/xpath3.h>
#include "private/xp3chars.h"
#include "private/xp3error.h"
#include "private/xp3regex.h"
#include "../test/testalloc.h"

#define XP3_FUZZ_MAX_SIZE 10000

/* Steps each matcher may take for one input */
#define XP3_FUZZ_STEPS 20000

/* Matches looked for in one subject */
#define XP3_FUZZ_MATCHES 100

static int fuzzInjects;

/* Stops the searches of one matcher after XP3_FUZZ_STEPS */
static int
chargeBudget(void *data, uint64_t steps) {
    uint64_t *used = data;

    *used += steps;
    if (*used > XP3_FUZZ_STEPS)
        return(-1);
    return(0);
}

int
LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void) argc;
    (void) argv;

    fuzzInjects = xp3TestAllocInstall();
    xmlInitParser();
    return(0);
}

/* Split the input after the header into flags, pattern and subject */
static void
splitInput(const char *data, size_t size, const char **flags,
           size_t *flagsLen, const char **pattern, size_t *patternLen,
           const char **subject, size_t *subjectLen) {
    const char *first = memchr(data, 0, size);
    const char *second = NULL;

    *flags = data;
    *flagsLen = 0;
    *pattern = data;
    *patternLen = size;
    *subject = data + size;
    *subjectLen = 0;
    if (first == NULL)
        return;
    *flagsLen = (size_t) (first - data);
    *pattern = first + 1;
    *patternLen = size - *flagsLen - 1;
    second = memchr(*pattern, 0, *patternLen);
    if (second == NULL)
        return;
    *patternLen = (size_t) (second - *pattern);
    *subject = second + 1;
    *subjectLen = size - (size_t) (*subject - data);
}

/* Whether compiling may raise this code */
static int
isCompileCode(xp3ErrCode code) {
    return((code == XP3_ERR_FORX0001) || (code == XP3_ERR_FORX0002) ||
           (code == XP3_ERR_LIMIT) || (code == XP3_ERR_NO_MEMORY));
}

/* Check that the groups of a match lie in the subject, and that the
 * match starts at or after the start of the search */
static void
checkGroups(const xp3RegexSpan *groups, int groupCount, size_t len,
            size_t start) {
    int i;

    if ((groups[0].start < (ptrdiff_t) start) || (groups[0].end < 0)) {
        fprintf(stderr, "xp3regex: the match is before the start\n");
        abort();
    }
    for (i = 0; i <= groupCount; i++) {
        if ((groups[i].start < 0) && (groups[i].end < 0))
            continue;
        if ((groups[i].start < 0) || (groups[i].start > groups[i].end) ||
            (groups[i].end > (ptrdiff_t) len)) {
            fprintf(stderr, "xp3regex: group %d is outside the subject\n",
                    i);
            abort();
        }
    }
}

/* Find the matches of a subject one after another with both matchers */
static void
compareMatchers(const xp3Regex *regex, const xmlChar *subject,
                size_t len) {
    int groupCount = xp3RegexGroupCount(regex);
    size_t spanBytes = ((size_t) groupCount + 1) * sizeof(xp3RegexSpan);
    xp3RegexSpan *groups = malloc(spanBytes);
    xp3RegexSpan *other = malloc(spanBytes);
    xp3RegexScratch *scratch = xp3RegexNewScratch(regex);
    uint64_t used = 0;
    uint64_t otherUsed = 0;
    xp3RegexSteps steps;
    xp3RegexSteps otherSteps;
    size_t start = 0;
    int count;

    if ((groups == NULL) || (other == NULL) || (scratch == NULL)) {
        fprintf(stderr, "xp3regex: out of memory\n");
        abort();
    }
    memset(&steps, 0, sizeof(steps));
    steps.charge = chargeBudget;
    steps.data = &used;
    otherSteps = steps;
    otherSteps.data = &otherUsed;

    for (count = 0; count < XP3_FUZZ_MATCHES; count++) {
        int ret;
        int otherRet;

        ret = xp3RegexSearch(regex, scratch, subject, len, start, groups,
                             &steps);
        if (ret < 0)
            break;
        otherRet = xp3RegexSearchBacktracking(regex, scratch, subject, len,
                                              start, other, &otherSteps);
        if ((otherRet >= 0) &&
            ((otherRet != ret) ||
             ((ret == 1) && (memcmp(groups, other, spanBytes) != 0)))) {
            fprintf(stderr, "xp3regex: the matchers disagree at offset "
                    "%zu: %d and %d\n", start, ret, otherRet);
            abort();
        }
        if (ret == 0)
            break;
        checkGroups(groups, groupCount, len, start);

        /* Go on after the match, or a character further after an
         * empty one */
        if ((size_t) groups[0].end > start)
            start = (size_t) groups[0].end;
        else if (start < len)
            xp3NextChar(subject, len, &start);
        else
            break;
    }
    xp3RegexFreeScratch(scratch);
    free(groups);
    free(other);
}

/* Compile and search with an allocation failing: NO_MEMORY or no
 * failure at all, never a hidden one */
static void
checkAllocationFailure(xp3LanguageLevel level, const char *flags,
                       size_t flagsLen, const char *pattern,
                       size_t patternLen, const char *subject,
                       size_t subjectLen, size_t failurePos) {
    xp3Error *err = NULL;
    xp3Regex *regex;
    int ret = 0;
    int failed;

    xp3TestAllocFailAt((int) failurePos - 1);
    regex = xp3RegexCompile(BAD_CAST pattern, patternLen, BAD_CAST flags,
                            flagsLen, level, &err);
    if (regex != NULL) {
        uint64_t used = 0;
        xp3RegexSteps steps;

        memset(&steps, 0, sizeof(steps));
        steps.charge = chargeBudget;
        steps.data = &used;
        ret = xp3RegexSearch(regex, NULL, BAD_CAST subject, subjectLen, 0,
                             NULL, &steps);
    }
    failed = xp3TestAllocFailed();
    xp3TestAllocFailAt(-1);

    if (failed) {
        if ((regex == NULL) &&
            ((err == NULL) || (err->code != XP3_ERR_NO_MEMORY))) {
            fprintf(stderr, "xp3regex: allocation failure in compiling "
                    "not reported as NO_MEMORY\n");
            abort();
        }
        if ((regex != NULL) && (ret != XP3_REGEX_NO_MEMORY)) {
            fprintf(stderr, "xp3regex: allocation failure in searching "
                    "reported as %d\n", ret);
            abort();
        }
    }
    xp3RegexFree(regex);
    xp3ErrorFree(err);
}

int
LLVMFuzzerTestOneInput(const char *data, size_t size) {
    static const xp3LanguageLevel levels[3] = {
        XP3_XPATH_20, XP3_XPATH_30, XP3_XPATH_31
    };
    const unsigned char *bytes = (const unsigned char *) data;
    const char *flags;
    const char *pattern;
    const char *subject;
    size_t flagsLen;
    size_t patternLen;
    size_t subjectLen;
    size_t failurePos;
    xp3LanguageLevel level;
    xp3Error *err = NULL;
    xp3Regex *regex;

    if ((size < 3) || (size > XP3_FUZZ_MAX_SIZE))
        return(0);
    level = levels[bytes[0] % 3];
    failurePos = ((size_t) bytes[1] << 8) | bytes[2];
    splitInput(data + 3, size - 3, &flags, &flagsLen, &pattern, &patternLen,
               &subject, &subjectLen);

    if ((failurePos > 0) && fuzzInjects) {
        checkAllocationFailure(level, flags, flagsLen, pattern, patternLen,
                               subject, subjectLen, failurePos);
        return(0);
    }

    regex = xp3RegexCompile(BAD_CAST pattern, patternLen, BAD_CAST flags,
                            flagsLen, level, &err);
    if ((regex == NULL) == (err == NULL)) {
        fprintf(stderr, "xp3regex: compiling returned both or neither\n");
        abort();
    }
    if (regex == NULL) {
        if (!isCompileCode(err->code)) {
            fprintf(stderr, "xp3regex: compiling raised %s\n",
                    (const char *) xp3ErrorCode(err));
            abort();
        }
        (void) xp3ErrorMessage(err);
        xp3ErrorFree(err);
        return(0);
    }
    compareMatchers(regex, BAD_CAST subject, subjectLen);
    xp3RegexFree(regex);
    return(0);
}

#ifdef XP3_FUZZ_DRIVER

/* Replay driver: runs each file through the fuzz target once */
int
main(int argc, char **argv) {
    int i;

    LLVMFuzzerInitialize(NULL, NULL);
    if (!fuzzInjects)
        printf("xp3regex: this libxml2 ignores xmlMemSetup(), replaying "
               "without allocation failures\n");

    for (i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        char *data;
        size_t size;

        if (f == NULL) {
            fprintf(stderr, "xp3regex: cannot open %s\n", argv[i]);
            return(1);
        }
        data = malloc(XP3_FUZZ_MAX_SIZE + 1);
        if (data == NULL) {
            fclose(f);
            return(1);
        }
        size = fread(data, 1, XP3_FUZZ_MAX_SIZE + 1, f);
        fclose(f);
        if (size <= XP3_FUZZ_MAX_SIZE)
            LLVMFuzzerTestOneInput(data, size);
        else
            fprintf(stderr, "xp3regex: %s is too large, skipped\n",
                    argv[i]);
        free(data);
    }

    printf("xp3regex: %d inputs replayed\n", argc - 1);
    return(0);
}

#endif /* XP3_FUZZ_DRIVER */
