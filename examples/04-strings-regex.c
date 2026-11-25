/*
 * 04-strings-regex.c: Unicode strings, case, whitespace and regular
 * expressions
 *
 * The program of doc/tutorial/04-strings-regex.md. Run it with the
 * directory of the example documents:
 *
 *     04-strings-regex examples/data
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdio.h>

#include <libxml/tree.h>
#include <libxpath3/xpath3.h>

#include "common.h"

/* A query, and the error code it raises, or NULL if it succeeds */
typedef struct {
    const char *text;
    const char *expectedError;
} Query;

static const Query queries[] = {
    /* The same name, precomposed and with combining accents */
    { "//contact[@id = 'c1']/name = //contact[@id = 'c2']/name", NULL },
    { "normalize-unicode(//contact[@id = 'c1']/name) = "
      "normalize-unicode(//contact[@id = 'c2']/name)", NULL },
    { "//contact[@id = ('c1', 'c2')]/string-length(name)", NULL },
    { "string-to-codepoints(//contact[@id = 'c2']/name)", NULL },
    { "starts-with(//contact[@id = 'c2']/name, 'José')", NULL },
    /* Lengths and positions count characters, not bytes */
    { "string-length(//contact[@id = 'c3']/name)", NULL },
    { "substring(//contact[@id = 'c3']/name, 1, 1)", NULL },
    { "string-to-codepoints(substring(//contact[@id = 'c3']/name, 1, 2))",
      NULL },
    { "string-length(//contact[@id = 'c7']/name)", NULL },
    { "substring(//contact[@id = 'c7']/name, 1, 3)", NULL },
    /* Case mappings of Unicode, the same in every locale */
    { "upper-case(//contact[@id = 'c5']/city)", NULL },
    { "upper-case('ß') = 'SS'", NULL },
    { "upper-case(//contact[@id = 'c4']/name)", NULL },
    { "lower-case(//contact[@id = 'c4']/city)", NULL },
    { "string-length(lower-case(//contact[@id = 'c4']/city))", NULL },
    { "matches('STRASSE', 'straße', 'i')", NULL },
    { "matches('ıstanbul', 'ISTANBUL', 'i')", NULL },
    { "matches(//contact[@id = 'c4']/city, '^istanbul$', 'i')", NULL },
    { "matches(//contact[@id = 'c5']/name, 'GROẞ', 'i')", NULL },
    { "translate(//contact[@id = 'c5']/city, 'ß', 'ss')", NULL },
    /* normalize-space() knows four whitespace characters only */
    { "normalize-space(//contact[@id = 'c6']/name)", NULL },
    { "string-to-codepoints(normalize-space(//contact[@id = 'c6']/name))[4]",
      NULL },
    { "normalize-space(replace(//contact[@id = 'c6']/name, '\\p{Zs}', ' '))",
      NULL },
    /* Patterns that match the empty string, invalid patterns and flags */
    { "tokenize('a,b,,c', ',*')", "FORX0003" },
    { "tokenize('a,b,,c', ',+')", NULL },
    { "tokenize('a,b,,c', ',')", NULL },
    { "replace('abc', 'x*', '-')", "FORX0003" },
    { "matches('abc', '(')", "FORX0002" },
    { "matches('abc', 'a', 'g')", "FORX0001" },
    /* Groups, back-references and reluctant quantifiers */
    { "matches('abab', '^(ab)\\1$')", NULL },
    { "replace('2026-11-25', '(\\d+)-(\\d+)-(\\d+)', '$3.$2.$1')", NULL },
    { "replace('<b>x</b> and <b>y</b>', '<b>(.*)</b>', '[$1]')", NULL },
    { "replace('<b>x</b> and <b>y</b>', '<b>(.*?)</b>', '[$1]')", NULL },
    /* The flags x, s, m and q */
    { "matches('2026-11-25', '^ \\d{4} - \\d{2} - \\d{2} $', 'x')", NULL },
    { "matches(//contact[@id = 'c6']/name, 'Lee.+')", NULL },
    { "matches(//contact[@id = 'c6']/name, 'Lee.+', 's')", NULL },
    { "matches(//contact[@id = 'c6']/name, '^\\s*$')", NULL },
    { "matches(//contact[@id = 'c6']/name, '^\\s*$', 'm')", NULL },
    { "matches(//contact[@id = 'c7']/note, 'C++ (v2.0)?')", "FORX0002" },
    { "matches(//contact[@id = 'c7']/note, 'C++ (v2.0)?', 'q')", NULL },
    { "replace(//contact[@id = 'c7']/note, '$5+', 'five', 'q')", NULL },
    { "replace('price: 5', '\\d', '$')", "FORX0004" },
    { "replace('price: 5', '\\d', '\\$')", NULL },
    /* \d is any decimal digit of Unicode, not only 0 to 9 */
    { "//phone ! replace(., '[^0-9+]', '')", NULL },
    { "matches(//contact[@id = 'c7']/phone, '^[\\d ]+$')", NULL },
    { "matches(//contact[@id = 'c7']/phone, '^[0-9 ]+$')", NULL },
    { "xs:integer(substring-before(//contact[@id = 'c7']/phone, ' '))",
      "FORG0001" },
    /* A quoted local part may contain an @ */
    { "substring-before(//contact[@id = 'c2']/email, '@')", NULL },
    { "replace(//contact[@id = 'c2']/email, '^(.*)@[^@]*$', '$1')", NULL },
    /* The codepoint collation orders by code point */
    { "compare('Zebra', 'apple')", NULL },
    { "compare('zoo', 'ábaco')", NULL },
    { "min(('b', 'B', 'á', 'a', 'Z'))", NULL },
    { "max(('b', 'B', 'á', 'a', 'Z'))", NULL },
    /* Every string contains the empty string */
    { "contains('abc', '')", NULL },
    { "contains((), '')", NULL },
    { "substring-before('abc', '')", NULL },
    /* An empty element and a missing one */
    { "string-length(//contact[@id = 'c3']/email)", NULL },
    { "matches(//contact[@id = 'c4']/email, '^$')", NULL },
    { "tokenize(//contact[@id = 'c4']/email, ',')", NULL }
};

/*
 * Nested quantifiers on a long subject that doesn't match: the first
 * pattern runs in linear time, the second has a back-reference, which
 * needs backtracking, and runs until the step limit stops it.
 */
static int
patternsAndSteps(const xp3StaticContext *sctx, xp3DynamicContext *dctx) {
    int failures = 0;

    printf("With at most 1,000,000 steps:\n");
    xp3SetMaxSteps(dctx, 1000000);
    failures += exRun(sctx, dctx, NULL,
                      "matches(string-join((1 to 100000) ! 'a') || 'b', "
                      "'(a+)+$')", NULL);
    failures += exRun(sctx, dctx, NULL,
                      "matches(string-join((1 to 30) ! 'a') || 'b', "
                      "'^(a+)+\\1$')", "LIMIT");

    /* The contexts work as before; 0 removes the limit again */
    xp3SetMaxSteps(dctx, 0);
    failures += exRun(sctx, dctx, NULL, "count(//contact)", NULL);
    return(failures);
}

int
main(int argc, char **argv) {
    xmlDocPtr doc = NULL;
    xp3StaticContext *sctx = NULL;
    xp3DynamicContext *dctx = NULL;
    size_t i;
    int failures = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: %s DATA-DIRECTORY\n", argv[0]);
        return(2);
    }

    doc = exReadDocument(argv[1], "contacts.xml", 0);
    sctx = xp3NewStaticContext();
    dctx = xp3NewDynamicContext();
    if ((doc == NULL) || (sctx == NULL) || (dctx == NULL)) {
        failures++;
        goto done;
    }
    xp3SetContextNode(dctx, (xmlNodePtr) doc);

    for (i = 0; i < sizeof(queries) / sizeof(queries[0]); i++)
        failures += exRun(sctx, dctx, NULL, queries[i].text,
                          queries[i].expectedError);
    failures += patternsAndSteps(sctx, dctx);

done:
    xp3FreeDynamicContext(dctx);
    xp3FreeStaticContext(sctx);
    xmlFreeDoc(doc);
    if (failures > 0)
        return(1);
    return(0);
}
