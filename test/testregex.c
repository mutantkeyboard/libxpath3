/*
 * testregex.c: tests for the regular expression engine
 *
 * Each construct of the dialect, the examples of XML Schema and F&O
 * 3.1, the flags, errors with their offsets, the limits, patterns that
 * take exponential time on a backtracking matcher, and allocation
 * failures. Every search runs on the matcher the pattern gets and on
 * the backtracking matcher too, with a budget, and both must find the
 * same match with the same groups.
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

#include "private/xp3error.h"
#include "private/xp3regex.h"
#include "testalloc.h"

/* The steps the backtracking matcher may take when it is compared */
#define XP3_TEST_BACKTRACK_STEPS 100000

/* Room for the description of a match */
#define XP3_TEST_SPANS 400

static int failures = 0;

static void
check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

/* Stops a search after a number of steps */
typedef struct {
    uint64_t steps;
    uint64_t max;
} xp3TestBudget;

static int
chargeBudget(void *data, uint64_t steps) {
    xp3TestBudget *budget = data;

    budget->steps += steps;
    if (budget->steps > budget->max)
        return(-1);
    return(0);
}

/* Describe the result of a search: "none", an error, or the spans of
 * the groups as "start,end" separated by spaces */
static void
describe(int ret, const xp3RegexSpan *groups, int groupCount, char *out,
         size_t size) {
    size_t used = 0;
    int i;

    out[0] = 0;
    if (ret == 0) {
        snprintf(out, size, "none");
        return;
    }
    if (ret < 0) {
        snprintf(out, size, "error %d", ret);
        return;
    }
    for (i = 0; i <= groupCount; i++) {
        int written;

        if (i > 0)
            written = snprintf(out + used, size - used, " %ld,%ld",
                               (long) groups[i].start, (long) groups[i].end);
        else
            written = snprintf(out + used, size - used, "%ld,%ld",
                               (long) groups[i].start, (long) groups[i].end);
        if ((written < 0) || ((size_t) written >= size - used))
            return;
        used += (size_t) written;
    }
}

/* Compile, or report the error. Returns NULL on error. */
static xp3Regex *
compile(const char *pattern, const char *flags, xp3LanguageLevel level) {
    xp3Error *err = NULL;
    xp3Regex *regex;
    size_t flagsLen = 0;

    if (flags != NULL)
        flagsLen = strlen(flags);
    regex = xp3RegexCompile(BAD_CAST pattern, strlen(pattern),
                            BAD_CAST flags, flagsLen, level, &err);
    if (regex == NULL) {
        char what[300];

        snprintf(what, sizeof(what), "/%s/ compiles: %s %s", pattern,
                 (const char *) xp3ErrorCode(err),
                 (const char *) xp3ErrorMessage(err));
        check(0, what);
        xp3ErrorFree(err);
    }
    return(regex);
}

/*
 * Search with the matcher the pattern gets, and compare with the
 * backtracking matcher when that stays within its budget. Writes the
 * description of the result to out.
 */
static int
searchBoth(const xp3Regex *regex, const char *pattern, const char *subject,
           size_t len, size_t start, char *out, size_t size) {
    int groupCount = xp3RegexGroupCount(regex);
    xp3RegexSpan *groups = calloc((size_t) groupCount + 1, sizeof(*groups));
    xp3RegexSpan *other = calloc((size_t) groupCount + 1, sizeof(*other));
    xp3TestBudget budget = { 0, XP3_TEST_BACKTRACK_STEPS };
    xp3RegexSteps steps;
    char otherText[XP3_TEST_SPANS];
    int ret;
    int otherRet;

    if ((groups == NULL) || (other == NULL)) {
        fprintf(stderr, "testregex: out of memory\n");
        exit(1);
    }
    memset(&steps, 0, sizeof(steps));
    steps.charge = chargeBudget;
    steps.data = &budget;

    ret = xp3RegexSearch(regex, NULL, BAD_CAST subject, len, start, groups,
                         NULL);
    describe(ret, groups, groupCount, out, size);
    otherRet = xp3RegexSearchBacktracking(regex, NULL, BAD_CAST subject, len,
                                          start, other, &steps);
    if (otherRet != XP3_REGEX_LIMIT) {
        char what[XP3_TEST_SPANS * 2 + 100];

        describe(otherRet, other, groupCount, otherText, sizeof(otherText));
        snprintf(what, sizeof(what), "/%s/ on \"%s\": both matchers find "
                 "%s, backtracking %s", pattern, subject, out, otherText);
        check(strcmp(out, otherText) == 0, what);
    }
    free(groups);
    free(other);
    return(ret);
}

/*
 * Searches
 */

static const struct {
    const char *pattern;
    const char *flags;
    const char *subject;
    const char *expect;         /* the spans, or "none" */
} searchCases[] = {
    /* Characters, ., concatenation */
    { "abc", "", "xxabcxx", "2,5" },
    { "abc", "", "ab", "none" },
    { "", "", "abc", "0,0" },
    { "a.c", "", "abc", "0,3" },
    { "a.c", "", "a\nc", "none" },
    { "a.c", "", "a\rc", "none" },
    { "a.c", "s", "a\nc", "0,3" },
    { "a.c", "s", "a\rc", "0,3" },
    { "\xC3\xA9.", "", "x\xC3\xA9\xE2\x82\xAC", "1,6" },
    /* Alternatives: leftmost, then the first that matches
     * (F&O 3.1, 5.6.4) */
    { "(ab)|(a)", "", "abcd", "0,2 0,2 -1,-1" },
    { "a|ab", "", "ab", "0,1" },
    { "ab|a", "", "ab", "0,2" },
    { "b|a", "", "ab", "0,1" },
    { "x|", "", "ab", "0,0" },
    { "(|x)y", "", "xy", "0,2 0,1" },
    /* Quantifiers, greedy and reluctant (5.6.1.2) */
    { "a?", "", "aa", "0,1" },
    { "a??", "", "aa", "0,0" },
    { "a*", "", "aaa", "0,3" },
    { "a*?", "", "aaa", "0,0" },
    { "a+", "", "baaa", "1,4" },
    { "a+?", "", "baaa", "1,2" },
    { "a{2}", "", "aaaa", "0,2" },
    { "a{2}?", "", "aaaa", "0,2" },
    { "a{2,}", "", "aaaa", "0,4" },
    { "a{2,}?", "", "aaaa", "0,2" },
    { "a{1,3}", "", "aaaa", "0,3" },
    { "a{1,3}?", "", "aaaa", "0,1" },
    { "a{0}", "", "aaa", "0,0" },
    { "a{0,0}b", "", "ab", "1,2" },
    { "a{3}", "", "aa", "none" },
    { "(a{2,3}?)a", "", "aaaa", "0,3 0,2" },
    { "a.*?a", "", "abracadabra", "0,4" },
    { "a.*a", "", "abracadabra", "0,11" },
    { "a*?b", "", "aaab", "0,4" },
    { "(a|b)*c", "", "abac", "0,4 2,3" },
    { "(a*)+", "", "b", "0,0 0,0" },
    { "(a*)*", "", "b", "0,0 -1,-1" },
    { "(a*)*", "", "aa", "0,2 0,2" },
    { "(a|ab)(c|bcd)(d*)", "", "abcd", "0,4 0,1 1,4 4,4" },
    { "(?:a|b)+?c", "", "abc", "0,3" },
    { "(?:){99999999}x", "", "x", "0,1" },
    /* Groups, numbered by their opening parenthesis (5.6.1.3) */
    { "A(BC(?:D(EF(GH[()]))))", "", "ABCDEFGH(", "0,9 1,9 4,9 6,9" },
    { "(a)(?:b)(c)", "", "abc", "0,3 0,1 2,3" },
    { "((a)|b)+", "", "ab", "0,2 1,2 0,1" },
    { "(a)?b", "", "b", "0,1 -1,-1" },
    /* Anchors (5.6.1.1), with and without m (5.6.2) */
    { "^a", "", "ba", "none" },
    { "^b", "", "ab", "none" },
    { "a$", "", "ab", "none" },
    { "b$", "", "ab", "1,2" },
    { "b$", "", "ab\n", "none" },
    { "^b", "m", "a\nb", "2,3" },
    { "a$", "m", "a\nb", "0,1" },
    { "^$", "m", "abcd\ndefg\n", "none" },
    { "^$", "m", "\nabcd\ndefg\n", "0,0" },
    { "^$", "m", "abcd\n\ndefg\n", "5,5" },
    { "$", "m", "ab\n", "2,2" },
    { "^", "m", "ab\n", "0,0" },
    { "^\\$\\^", "", "$^", "0,2" },
    { "a^*b", "", "ab", "0,2" },
    { "^*a", "", "ba", "1,2" },
    /* Character classes */
    { "[abc]+", "", "xcabz", "1,4" },
    { "[a-c]+", "", "xcabz", "1,4" },
    { "[^a-c]+", "", "abxyc", "2,4" },
    { "[a-z-[aeiou]]+", "", "aebcdi", "2,5" },
    { "[a-z-[a-m-[c]]]+", "", "abcnz", "2,5" },
    { "[^a-z-[aeiou]]", "", "bx1", "2,3" },
    { "[-a]+", "", "x-a-", "1,4" },
    { "[a-]+", "", "x-a-", "1,4" },
    { "[--]", "", "a-", "1,2" },
    { "[a--[a]]", "", "a-", "1,2" },
    { "[\\--/]+", "", "a-./b", "1,4" },
    { "[\\n\\r\\t\\\\\\[\\]\\^\\-]+", "", "a\n\r\t\\[]^-b", "1,9" },
    { "[\\^]", "", "a^", "1,2" },
    { "[^^]", "", "^a", "1,2" },
    { "[a^]+", "", "x^a", "1,3" },
    { "[\\d\\s]+", "", "a1 2b", "1,4" },
    { "[{}()|.?*+$]+", "", "a{}()|.?*+$", "1,11" },
    /* Escapes */
    { "\\n\\r\\t", "", "\n\r\t", "0,3" },
    { "\\.\\?\\*\\+\\{\\}\\(\\)\\|\\[\\]\\\\\\-\\$\\^", "",
      ".?*+{}()|[]\\-$^", "0,15" },
    { "\\s+", "", "a \t\r\nb", "1,5" },
    { "\\S+", "", " ab ", "1,3" },
    { "\\d+", "", "a1\xD9\xA3x", "1,4" },
    { "\\D+", "", "12ab3", "2,4" },
    { "\\w+", "", "!ab1\xC3\xA9 ", "1,6" },
    { "\\W+", "", "ab!? c", "2,5" },
    { "\\i\\c*", "", " :a-b.1 ", "1,7" },
    { "\\I+", "", "a-1b", "1,3" },
    { "\\C+", "", "ab !c", "2,4" },
    { "\\i", "", "1\xC3\x80", "1,3" },
    /* Categories and blocks */
    { "\\p{Lu}+", "", "abCDe", "2,4" },
    { "\\P{Lu}+", "", "ABcdE", "2,4" },
    { "\\p{L}+", "", "1a\xC3\xA9\xE4\xB8\x80!", "1,7" },
    { "\\p{N}+", "", "a1\xC2\xB2\xE2\x85\xA0x", "1,7" },
    { "\\p{P}", "", "a!", "1,2" },
    { "\\p{Zs}", "", "a b", "1,2" },
    { "\\p{Cc}", "", "a\tb", "1,2" },
    { "\\p{Cn}", "", "a\xCD\xB8", "1,3" },
    { "\\p{Sm}", "", "a+", "1,2" },
    { "[\\p{Lu}\\p{Nd}]+", "", "aB1c", "1,3" },
    { "\\p{IsBasicLatin}+", "", "\xC3\xA9" "abc\xC3\xA9", "2,5" },
    { "\\p{IsGreekandCoptic}", "", "a\xCE\xB1", "1,3" },
    { "\\p{IsLatin-1Supplement}", "", "a\xC3\xA9", "1,3" },
    /* A block added in Unicode 18.0 */
    { "\\p{IsJurchen}", "", "a\xF0\x98\xB8\x80", "1,5" },
    { "\\P{IsBasicLatin}", "", "a\xC3\xA9", "1,3" },
    { "[\\p{IsGreekandCoptic}-[\\P{Lu}]]+", "", "\xCE\xB1\xCE\x91\xCE\x92",
      "2,6" },
    /* Back-references (5.6.1.4) */
    { "('|\").*\\1", "", "say \"hi'\" now", "4,9 4,5" },
    { "(a)\\1", "", "aab", "0,2 0,1" },
    { "(a)\\10", "", "aa0", "0,3 0,1" },
    { "(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)\\10", "", "abcdefghijj",
      "0,11 0,1 1,2 2,3 3,4 4,5 5,6 6,7 7,8 8,9 9,10" },
    { "(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)\\11", "", "abcdefghija1",
      "0,12 0,1 1,2 2,3 3,4 4,5 5,6 6,7 7,8 8,9 9,10" },
    { "(a)?b\\1", "", "b", "0,1 -1,-1" },
    { "(a)|b\\1", "", "b", "0,1 -1,-1" },
    { "(a*)b\\1", "", "aabaa", "0,5 0,2" },
    { "(a*)b\\1", "", "aaba", "1,4 1,2" },
    { "^(a+)\\1$", "", "aaaa", "0,4 0,2" },
    { "(\\w+)\\s\\1", "", "the the end", "0,7 0,3" },
    /* The x flag removes whitespace outside character classes */
    { "hello world", "x", "helloworld", "0,10" },
    { "hello world", "x", "hello world", "none" },
    { "hello[ ]world", "x", "helloworld", "none" },
    { "hello[ ]world", "x", "hello world", "0,11" },
    { "hello\\ sworld", "x", "hello world", "0,11" },
    { "a{ 1 , 2 } b", "x", "aab", "0,3" },
    { "( ? : a ) +", "x", "aa", "0,2" },
    { "\\p{ L u }", "x", "aB", "1,2" },
    { "(a)\\1 0", "x", "aa0", "0,3 0,1" },
    { "[a - z]", "x", "- ", "1,2" },
    /* The q flag: every character stands for itself */
    { ".", "q", "12.3", "2,3" },
    { ".*", "q", "abcd", "none" },
    { "a+(", "q", "xa+(", "1,4" },
    { "\\", "q", "a\\b", "1,2" },
    { "^a$", "qm", "x^a$", "1,4" },
    { "a b", "qx", "a b", "0,3" },
    { "a.b", "qs", "a\nb", "none" },
    { "B. OBAMA", "iq", "Mr. B. Obama", "4,12" },
    /* The i flag: case variants of characters and ranges, not of
     * categories (5.6.2) */
    { "k", "i", "K", "0,1" },
    { "k", "i", "\xE2\x84\xAA", "0,3" },
    { "\xE2\x84\xAA", "i", "k", "0,1" },
    { "K", "i", "\xE2\x84\xAA", "0,3" },
    { "\xC3\x9F", "i", "\xE1\xBA\x9E", "0,3" },
    { "\xE1\xBA\x9E", "i", "\xC3\x9F", "0,2" },
    { "z", "i", "Z", "0,1" },
    { "[A-Z]", "i", "1\xE2\x84\xAA", "1,4" },
    { "[a-z]+", "i", "1HeLLo", "1,6" },
    { "[A-Z-[IO]]+", "i", "AbIo", "0,2" },
    { "[^Q]", "i", "qQx", "2,3" },
    { "\\p{Lu}", "i", "m", "none" },
    { "\\P{Lu}", "i", "m", "0,1" },
    { "\\p{IsBasicLatin}", "i", "\xE2\x84\xAA", "none" },
    { "([md])[aeiou]\\1", "i", "Mum", "0,3 0,1" },
    { "([md])[aeiou]\\1", "i", "mom", "0,3 0,1" },
    { "([md])[aeiou]\\1", "i", "Dad", "0,3 0,1" },
    { "([md])[aeiou]\\1", "i", "DUD", "0,3 0,1" },
    { "(k)\\1", "i", "k\xE2\x84\xAA", "0,4 0,1" },
    { "kiki", "i", "Kikeriki! KIKIkerikih", "10,14" },
    /* Examples of F&O 3.1, 5.6.3 to 5.6.5 */
    { "bra", "", "abracadabra", "1,4" },
    { "^a.*a$", "", "abracadabra", "0,11" },
    { "^bra", "", "abracadabra", "none" },
    { "Kaum.*kr\xC3\xA4hen", "",
      "\nKaum hat dies der Hahn gesehen,\n"
      "F\xC3\xA4ngt er auch schon an zu kr\xC3\xA4hen:\n", "none" },
    { "Kaum.*kr\xC3\xA4hen", "s",
      "\nKaum hat dies der Hahn gesehen,\n"
      "F\xC3\xA4ngt er auch schon an zu kr\xC3\xA4hen:\n", "1,67" },
    { "^Kaum.*gesehen,$", "m",
      "\nKaum hat dies der Hahn gesehen,\nF\xC3\xA4ngt er\n", "1,32" },
    { "^Kaum.*gesehen,$", "",
      "\nKaum hat dies der Hahn gesehen,\nF\xC3\xA4ngt er\n", "none" },
    { "a(.)", "", "abracadabra", "0,2 1,2" },
    { "(ac)|(a)", "", "abracadabra", "0,1 -1,-1 0,1" },
    { "\\s+", "", "The cat sat", "3,4" },
    { "\\s*", "", "The cat", "0,0" },
    /* Invalid UTF-8 is U+FFFD, and offsets are into the bytes */
    { "\xFF", "", "a\xFF", "1,2" },
    { "\xFF", "", "a\xEF\xBF\xBD", "1,4" },
    { "\xEF\xBF\xBD", "", "a\xFE", "1,2" },
    { "a.b", "", "xa\xFF" "b", "1,4" },
    { "[\xFF]+", "", "a\xFE\xFF", "1,3" },
    { "(.)\\1", "", "\xFF\xEF\xBF\xBD", "0,4 0,1" },
    { "\\p{So}", "", "a\xFF", "1,2" }
};

static void
testSearches(void) {
    size_t i;

    for (i = 0; i < sizeof(searchCases) / sizeof(searchCases[0]); i++) {
        xp3Regex *regex = compile(searchCases[i].pattern,
                                  searchCases[i].flags, XP3_XPATH_31);
        char result[XP3_TEST_SPANS];
        char what[XP3_TEST_SPANS + 200];

        if (regex == NULL)
            continue;
        searchBoth(regex, searchCases[i].pattern, searchCases[i].subject,
                   strlen(searchCases[i].subject), 0, result,
                   sizeof(result));
        snprintf(what, sizeof(what), "/%s/%s on \"%s\": %s, not %s",
                 searchCases[i].pattern, searchCases[i].flags,
                 searchCases[i].subject, result, searchCases[i].expect);
        check(strcmp(result, searchCases[i].expect) == 0, what);
        xp3RegexFree(regex);
    }
}

/* Searches from an offset: ^ still means the start of the subject */
static void
testStartOffsets(void) {
    xp3Regex *regex = compile("^a|b", "", XP3_XPATH_31);
    xp3Regex *anchored = compile("^a", "m", XP3_XPATH_31);
    xp3RegexScratch *scratch;
    xp3RegexSpan groups[1];
    char result[XP3_TEST_SPANS];
    int ret;

    if ((regex == NULL) || (anchored == NULL))
        return;
    searchBoth(regex, "^a|b", "aab", 3, 1, result, sizeof(result));
    check(strcmp(result, "2,3") == 0, "^ doesn't match at the start offset");
    searchBoth(regex, "^a|b", "aab", 3, 3, result, sizeof(result));
    check(strcmp(result, "none") == 0, "nothing after the last match");
    searchBoth(anchored, "^a", "b\na", 3, 1, result, sizeof(result));
    check(strcmp(result, "2,3") == 0, "^ after a newline with m");

    /* One scratch for several searches, as fn:tokenize will use it */
    scratch = xp3RegexNewScratch(regex);
    check(scratch != NULL, "scratch created");
    if (scratch != NULL) {
        ret = xp3RegexSearch(regex, scratch, BAD_CAST "abab", 4, 0, groups,
                             NULL);
        check((ret == 1) && (groups[0].start == 0), "first search");
        ret = xp3RegexSearch(regex, scratch, BAD_CAST "abab", 4, 1, groups,
                             NULL);
        check((ret == 1) && (groups[0].start == 1), "second search");
        ret = xp3RegexSearchBacktracking(regex, scratch, BAD_CAST "abab", 4,
                                         2, groups, NULL);
        check((ret == 1) && (groups[0].start == 3), "third search");
        ret = xp3RegexSearch(regex, scratch, BAD_CAST "abab", 4, 5, groups,
                             NULL);
        check(ret == 0, "a start past the end finds nothing");
        ret = xp3RegexSearch(regex, scratch, BAD_CAST "abab", 4, 0, NULL,
                             NULL);
        check(ret == 1, "a search without groups");
    }
    xp3RegexFreeScratch(scratch);
    check(xp3RegexGroupCount(regex) == 0, "no groups");
    xp3RegexFree(regex);
    xp3RegexFree(anchored);
}

/*
 * Errors
 */

static const struct {
    const char *pattern;
    const char *flags;
    xp3LanguageLevel level;
    const char *code;
    int offset;                 /* in the message, or -1 */
} errorCases[] = {
    /* Flags (5.6.2) */
    { "a", "z", XP3_XPATH_31, "FORX0001", -1 },
    { "a", "I", XP3_XPATH_31, "FORX0001", -1 },
    { "a", " ", XP3_XPATH_31, "FORX0001", -1 },
    { "a", "q", XP3_XPATH_20, "FORX0001", -1 },
    { "a", "\xC3\xA9", XP3_XPATH_31, "FORX0001", -1 },
    /* Syntax */
    { "abc)", "", XP3_XPATH_31, "FORX0002", 3 },
    { "(abc", "", XP3_XPATH_31, "FORX0002", 0 },
    { "a**", "", XP3_XPATH_31, "FORX0002", 2 },
    { "a???", "", XP3_XPATH_31, "FORX0002", 3 },
    { "a{2}{3}", "", XP3_XPATH_31, "FORX0002", 4 },
    { "*a", "", XP3_XPATH_31, "FORX0002", 0 },
    { "a|*", "", XP3_XPATH_31, "FORX0002", 2 },
    { "(+)", "", XP3_XPATH_31, "FORX0002", 1 },
    { "a{", "", XP3_XPATH_31, "FORX0002", 1 },
    { "a{,3}", "", XP3_XPATH_31, "FORX0002", 1 },
    { "a{1,2,3}", "", XP3_XPATH_31, "FORX0002", 1 },
    { "x{3,1}", "", XP3_XPATH_31, "FORX0002", 1 },
    { "a{99999999999}", "", XP3_XPATH_31, "FORX0002", 1 },
    { "{", "", XP3_XPATH_31, "FORX0002", 0 },
    { "a}", "", XP3_XPATH_31, "FORX0002", 1 },
    { "a]", "", XP3_XPATH_31, "FORX0002", 1 },
    { "(?a)", "", XP3_XPATH_31, "FORX0002", 0 },
    { "(?=a)", "", XP3_XPATH_31, "FORX0002", 0 },
    { "\\", "", XP3_XPATH_31, "FORX0002", 0 },
    { "a\\b", "", XP3_XPATH_31, "FORX0002", 1 },
    { "\\0", "", XP3_XPATH_31, "FORX0002", 0 },
    { "\\x41", "", XP3_XPATH_31, "FORX0002", 0 },
    /* Classes */
    { "[a", "", XP3_XPATH_31, "FORX0002", 0 },
    { "[]", "", XP3_XPATH_31, "FORX0002", 0 },
    { "[^]", "", XP3_XPATH_31, "FORX0002", 0 },
    { "[[a]]", "", XP3_XPATH_31, "FORX0002", 1 },
    { "[z-a]", "", XP3_XPATH_31, "FORX0002", 1 },
    { "[--z]", "", XP3_XPATH_31, "FORX0002", 1 },
    { "[!--]", "", XP3_XPATH_31, "FORX0002", 1 },
    { "[a-\\d]", "", XP3_XPATH_31, "FORX0002", 1 },
    { "[0-9-.]", "", XP3_XPATH_31, "FORX0002", 4 },
    { "[a-c-e]", "", XP3_XPATH_31, "FORX0002", 4 },
    { "[\\d-z]", "", XP3_XPATH_31, "FORX0002", 3 },
    { "[a-[b]c]", "", XP3_XPATH_31, "FORX0002", 0 },
    { "[-[b]]", "", XP3_XPATH_31, "FORX0002", 0 },
    { "[\\1]", "", XP3_XPATH_31, "FORX0002", 1 },
    { "[\\x]", "", XP3_XPATH_31, "FORX0002", 1 },
    /* Properties and blocks (5.6.1.5) */
    { "\\p{Cs}", "", XP3_XPATH_31, "FORX0002", 0 },
    { "\\p{Lx}", "", XP3_XPATH_31, "FORX0002", 0 },
    { "\\p{}", "", XP3_XPATH_31, "FORX0002", 0 },
    { "\\p{L", "", XP3_XPATH_31, "FORX0002", 0 },
    { "\\pL", "", XP3_XPATH_31, "FORX0002", 0 },
    { "\\p{Is}", "", XP3_XPATH_31, "FORX0002", 0 },
    { "\\p{IsFoo}", "", XP3_XPATH_31, "FORX0002", 0 },
    { "\\p{IsGreek}", "", XP3_XPATH_31, "FORX0002", 0 },
    { "\\p{IsaA0-a9}", "", XP3_XPATH_31, "FORX0002", 0 },
    { "\\p{Is Basic Latin}", "", XP3_XPATH_31, "FORX0002", 0 },
    { "\\p{isBasicLatin}", "", XP3_XPATH_31, "FORX0002", 0 },
    { "x\\P{IsBasicLatin_}", "", XP3_XPATH_31, "FORX0002", 1 },
    /* Back-references (5.6.1.4) */
    { "\\1(a)", "", XP3_XPATH_31, "FORX0002", 0 },
    { "(a\\1)", "", XP3_XPATH_31, "FORX0002", 2 },
    { "(a)\\2", "", XP3_XPATH_31, "FORX0002", 3 },
    { "(a)(b\\2)", "", XP3_XPATH_31, "FORX0002", 5 },
    /* Invalid UTF-8 counts as one character; offsets are bytes */
    { "\xFF\xFE(", "", XP3_XPATH_31, "FORX0002", 2 },
    { "\xC3\xA9)", "", XP3_XPATH_31, "FORX0002", 2 },
    /* With x, the offset is still that of the pattern as given */
    { "a  )", "x", XP3_XPATH_31, "FORX0002", 3 }
};

static void
testErrors(void) {
    size_t i;

    for (i = 0; i < sizeof(errorCases) / sizeof(errorCases[0]); i++) {
        xp3Error *err = NULL;
        xp3Regex *regex;
        const char *code = "none";
        const char *message = "";
        char what[300];
        char offset[50];

        regex = xp3RegexCompile(BAD_CAST errorCases[i].pattern,
                                strlen(errorCases[i].pattern),
                                BAD_CAST errorCases[i].flags,
                                strlen(errorCases[i].flags),
                                errorCases[i].level, &err);
        if (err != NULL) {
            code = (const char *) xp3ErrorCode(err);
            message = (const char *) xp3ErrorMessage(err);
        }
        snprintf(what, sizeof(what), "/%s/%s raises %s, not %s (%s)",
                 errorCases[i].pattern, errorCases[i].flags,
                 errorCases[i].code, code, message);
        check((regex == NULL) && (err != NULL) &&
              xmlStrEqual(xp3ErrorCode(err), BAD_CAST errorCases[i].code),
              what);
        if ((err != NULL) && (errorCases[i].offset >= 0)) {
            snprintf(offset, sizeof(offset), "at offset %d of",
                     errorCases[i].offset);
            check(strstr((const char *) xp3ErrorMessage(err), offset) != NULL,
                  what);
        }
        xp3RegexFree(regex);
        xp3ErrorFree(err);
    }
}

/* Flags that are fine: repeated, any order, q from XPath 3.0 */
static void
testFlags(void) {
    static const char *const valid[] = { "", "smixq", "iiss", "qi", "xm" };
    xp3Error *err = NULL;
    xp3Regex *regex;
    size_t i;

    for (i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
        regex = compile("a", valid[i], XP3_XPATH_31);
        xp3RegexFree(regex);
    }
    regex = compile("a", "q", XP3_XPATH_30);
    xp3RegexFree(regex);
    regex = compile("a", "smix", XP3_XPATH_20);
    xp3RegexFree(regex);
    regex = xp3RegexCompile(BAD_CAST "a", 1, NULL, 0, XP3_XPATH_20, &err);
    check((regex != NULL) && (err == NULL), "NULL flags are none");
    xp3RegexFree(regex);
}

/*
 * Limits
 */

/* A pattern of count copies of open, then count of close */
static char *
nested(const char *open, const char *middle, const char *close, int count) {
    size_t openLen = strlen(open);
    size_t closeLen = strlen(close);
    size_t middleLen = strlen(middle);
    char *out = malloc(count * (openLen + closeLen) + middleLen + 1);
    char *p = out;
    int i;

    if (out == NULL) {
        fprintf(stderr, "testregex: out of memory\n");
        exit(1);
    }
    for (i = 0; i < count; i++) {
        memcpy(p, open, openLen);
        p += openLen;
    }
    memcpy(p, middle, middleLen);
    p += middleLen;
    for (i = 0; i < count; i++) {
        memcpy(p, close, closeLen);
        p += closeLen;
    }
    *p = 0;
    return(out);
}

/* Compile a pattern that must fail with a code */
static void
expectError(const char *pattern, const char *code, const char *what) {
    xp3Error *err = NULL;
    xp3Regex *regex;

    regex = xp3RegexCompile(BAD_CAST pattern, strlen(pattern), NULL, 0,
                            XP3_XPATH_31, &err);
    check((regex == NULL) && (err != NULL) &&
          xmlStrEqual(xp3ErrorCode(err), BAD_CAST code), what);
    xp3RegexFree(regex);
    xp3ErrorFree(err);
}

static void
testLimits(void) {
    char *pattern;
    xp3Regex *regex;

    pattern = nested("(", "a", ")", 200);
    regex = compile(pattern, "", XP3_XPATH_31);
    check((regex != NULL) && (xp3RegexGroupCount(regex) == 200),
          "200 nested groups");
    xp3RegexFree(regex);
    free(pattern);

    pattern = nested("(?:", "a", ")", 1000);
    expectError(pattern, "LIMIT", "1000 nested groups are too deep");
    free(pattern);
    pattern = nested("[a-", "[b]", "]", 1000);
    expectError(pattern, "LIMIT", "1000 nested subtractions are too deep");
    free(pattern);

    expectError("(a{1000}){1000}", "LIMIT", "a program of a million");
    expectError("a{100001}", "LIMIT", "a program of 100001");
    expectError("(?:a{100}|b){2000}", "LIMIT", "a large alternation");
    expectError("((((((((((a{100}){2}){2}){2}){2}){2}){2}){2}){2}){2}){2}",
                "LIMIT", "doubling repetitions");
    expectError("(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a)(a).{50000}",
                "LIMIT", "many instructions with many groups");
    regex = compile("a{50000}", "", XP3_XPATH_31);
    xp3RegexFree(regex);
    regex = compile("(?:(?:){1000}){1000}", "", XP3_XPATH_31);
    xp3RegexFree(regex);
}

/*
 * Work
 */

/* Search the whole subject, counting operations */
static uint64_t
countOperations(const char *pattern, const char *subject, size_t len,
                int backtrack, int *ret) {
    xp3Regex *regex = compile(pattern, "", XP3_XPATH_31);
    xp3RegexSteps steps;

    memset(&steps, 0, sizeof(steps));
    if (regex == NULL)
        return(0);
    if (backtrack)
        *ret = xp3RegexSearchBacktracking(regex, NULL, BAD_CAST subject, len,
                                          0, NULL, &steps);
    else
        *ret = xp3RegexSearch(regex, NULL, BAD_CAST subject, len, 0, NULL,
                              &steps);
    xp3RegexFree(regex);
    return(steps.operations);
}

/*
 * Patterns that take exponential time on a backtracking matcher are
 * linear on the Pike VM: at most a few operations per instruction and
 * character
 */
static void
testPathological(void) {
    static const char *const patterns[] = {
        "(a*)*b", "(a|a)*b", "(a|aa)*c", "(?:a?){20}a{20}", "(x+x+)+y"
    };
    size_t len = 100000;
    char *subject = malloc(len + 1);
    size_t i;

    if (subject == NULL) {
        fprintf(stderr, "testregex: out of memory\n");
        exit(1);
    }
    memset(subject, 'a', len);
    subject[len] = 0;
    for (i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
        char what[200];
        uint64_t operations;
        int ret = -1;

        operations = countOperations(patterns[i], subject, len, 0, &ret);
        snprintf(what, sizeof(what), "/%s/ on 100,000 a: %llu operations, "
                 "at most 20 per character", patterns[i],
                 (unsigned long long) operations);
        check((ret >= 0) && (operations <= 20 * len), what);
    }

    /* The same work twice as long takes twice the operations */
    {
        int ret;
        uint64_t half = countOperations("(a|aa)*c", subject, len / 2, 0,
                                        &ret);
        uint64_t whole = countOperations("(a|aa)*c", subject, len, 0, &ret);

        check((whole > half) && (whole <= 2 * half + 100),
              "operations grow linearly");
    }
    free(subject);
}

/* A back-reference pattern that takes exponential time stops at the
 * budget of its caller, with LIMIT */
static void
testBacktrackingLimit(void) {
    const char *pattern = "(a|aa)*\\1c";
    xp3Regex *regex = compile(pattern, "", XP3_XPATH_31);
    xp3TestBudget budget = { 0, 10000 };
    xp3RegexSteps steps;
    char subject[41];
    int ret;

    if (regex == NULL)
        return;
    memset(subject, 'a', 40);
    subject[40] = 0;
    memset(&steps, 0, sizeof(steps));
    steps.charge = chargeBudget;
    steps.data = &budget;
    ret = xp3RegexSearch(regex, NULL, BAD_CAST subject, 40, 0, NULL, &steps);
    check(ret == XP3_REGEX_LIMIT, "an exponential search hits LIMIT");
    check(steps.operations <= 10001 * XP3_REGEX_OPERATIONS_PER_STEP,
          "it stops at the budget");

    /* Short inputs still match */
    ret = xp3RegexSearch(regex, NULL, BAD_CAST "aaaac", 5, 0, NULL, NULL);
    check(ret == 1, "a short input matches");
    xp3RegexFree(regex);

    /* Without back-references, the backtracker is exponential too, which
     * is why the Pike VM runs such patterns */
    regex = compile("(a|aa)*c", "", XP3_XPATH_31);
    if (regex == NULL)
        return;
    budget.steps = 0;
    ret = xp3RegexSearchBacktracking(regex, NULL, BAD_CAST subject, 40, 0,
                                     NULL, &steps);
    check(ret == XP3_REGEX_LIMIT, "the backtracker hits LIMIT too");
    xp3RegexFree(regex);
}

/* Steps are charged one per XP3_REGEX_OPERATIONS_PER_STEP operations,
 * and what is left over carries over to the next search */
static void
testSteps(void) {
    xp3Regex *regex = compile("[a-z]+!", "", XP3_XPATH_31);
    xp3TestBudget budget = { 0, 1000000 };
    xp3RegexSteps steps;
    char subject[2001];
    int ret;

    if (regex == NULL)
        return;
    memset(subject, 'a', 2000);
    subject[2000] = 0;
    memset(&steps, 0, sizeof(steps));
    steps.charge = chargeBudget;
    steps.data = &budget;
    ret = xp3RegexSearch(regex, NULL, BAD_CAST subject, 2000, 0, NULL,
                         &steps);
    check(ret == 0, "no match");
    check(budget.steps == steps.operations / XP3_REGEX_OPERATIONS_PER_STEP,
          "a step per 256 operations");
    check(steps.charged == budget.steps * XP3_REGEX_OPERATIONS_PER_STEP,
          "the charged operations are counted");
    ret = xp3RegexSearch(regex, NULL, BAD_CAST "ab!", 3, 0, NULL, &steps);
    check(ret == 1, "a match");
    check(budget.steps == steps.operations / XP3_REGEX_OPERATIONS_PER_STEP,
          "the rest carries over");
    xp3RegexFree(regex);
}

/*
 * Allocation failures
 */

/* Compile and search with the n-th allocation failing: either it
 * fails with NO_MEMORY, or it finds the expected match */
static void
testAllocationFailures(void) {
    static const struct {
        const char *pattern;
        int backtrack;
    } cases[] = {
        { "(a|b)*(c)[\\p{Lu}-[X]]", 0 },
        { "(a|b)*(c)[\\p{Lu}-[X]]", 1 },
        { "(a+)\\1(?:x|y)*", 0 },
        { "((((a)))){3}", 0 }
    };
    const char *subject = "zzababcBaaaaxyx";
    size_t i;

    if (!xp3TestAllocInstall()) {
        printf("testregex: allocation-failure tests skipped: this "
               "libxml2 ignores xmlMemSetup()\n");
        xp3TestAllocRestore();
        return;
    }
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char expected[XP3_TEST_SPANS];
        xp3RegexSpan groups[10];
        xp3Regex *regex;
        int n;

        regex = compile(cases[i].pattern, "", XP3_XPATH_31);
        if (regex == NULL)
            continue;
        searchBoth(regex, cases[i].pattern, subject, strlen(subject), 0,
                   expected, sizeof(expected));
        xp3RegexFree(regex);

        for (n = 0; n < 1000; n++) {
            char result[XP3_TEST_SPANS];
            xp3Error *err = NULL;
            int injected;
            int ret = 0;

            xp3TestAllocFailAt(n);
            regex = xp3RegexCompile(BAD_CAST cases[i].pattern,
                                    strlen(cases[i].pattern), NULL, 0,
                                    XP3_XPATH_31, &err);
            if ((regex != NULL) && cases[i].backtrack)
                ret = xp3RegexSearchBacktracking(regex, NULL,
                                                 BAD_CAST subject,
                                                 strlen(subject), 0, groups,
                                                 NULL);
            else if (regex != NULL)
                ret = xp3RegexSearch(regex, NULL, BAD_CAST subject,
                                     strlen(subject), 0, groups, NULL);
            injected = xp3TestAllocFailed();
            xp3TestAllocFailAt(-1);

            if (!injected) {
                describe(ret, groups, xp3RegexGroupCount(regex), result,
                         sizeof(result));
                check(strcmp(result, expected) == 0,
                      "the search is complete after allocation failures");
                xp3RegexFree(regex);
                break;
            }
            if (regex == NULL)
                check((err != NULL) && (err->code == XP3_ERR_NO_MEMORY),
                      "a failed allocation in compiling is NO_MEMORY");
            else
                check(ret == XP3_REGEX_NO_MEMORY,
                      "a failed allocation in searching is NO_MEMORY");
            xp3RegexFree(regex);
            xp3ErrorFree(err);
        }
        check((n > 2) && (n < 1000), "allocation failure points exercised");
    }

    /* A scratch that failed to allocate recovers in the next search */
    {
        xp3Regex *regex = compile("(a)|b", "", XP3_XPATH_31);
        xp3RegexScratch *scratch = NULL;
        xp3RegexSpan groups[2];
        int ret;

        if (regex != NULL)
            scratch = xp3RegexNewScratch(regex);
        if (scratch != NULL) {
            xp3TestAllocFailAt(2);
            ret = xp3RegexSearch(regex, scratch, BAD_CAST "xb", 2, 0, groups,
                                 NULL);
            xp3TestAllocFailAt(-1);
            check(ret == XP3_REGEX_NO_MEMORY, "the scratch fails");
            ret = xp3RegexSearch(regex, scratch, BAD_CAST "xb", 2, 0, groups,
                                 NULL);
            check((ret == 1) && (groups[0].start == 1),
                  "the scratch recovers");
        }
        xp3RegexFreeScratch(scratch);
        xp3RegexFree(regex);
    }
    xp3TestAllocRestore();
}

int
main(void) {
    xmlInitParser();

    testSearches();
    testStartOffsets();
    testErrors();
    testFlags();
    testLimits();
    testPathological();
    testBacktrackingLimit();
    testSteps();
    testAllocationFailures();

    if (failures != 0)
        return(1);
    printf("testregex: OK\n");
    return(0);
}
