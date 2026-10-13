/*
 * qt3run.c: run the W3C QT3 test suite against libxpath3
 *
 * Only the parser exists so far, so every test runs in parse-only
 * mode. A test passes if its expression parses and the expected
 * result allows that, or if parsing fails with an error code the
 * expected result names. An expected error other than XPST0003 counts
 * as "may parse", because static analysis or evaluation raises it
 * later.
 *
 * Usage: qt3run [-s XP20|XP30|XP31] [-t PREFIX] [-k FILE] [-v] CATALOG
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/hash.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

#include <libxpath3/xpath3.h>
#include "private/xp3ast.h"
#include "private/xp3parser.h"

#define QT3_NS "http://www.w3.org/2010/09/qt-fots-catalog"

/* Longest part of an expression or result shown for a failure */
#define QT3_MAX_SHOWN 300

typedef struct {
    int level;                  /* 20, 30 or 31 */
    const char *spec;           /* "XP20", "XP30" or "XP31" */
    const char *prefix;         /* only test sets starting with this */
    int verbose;
    xp3StaticContext *ctx;
    xmlHashTable *known;        /* "set/case" of known failures */

    int passed;
    int failed;                 /* not in the known failures */
    int knownFailed;
    int fixed;                  /* known failures that now pass */
    int notApplicable;
} qt3Run;

/*
 * Dependencies the implementation does not satisfy. A NULL value
 * means every value of the type. Anything not listed is assumed to be
 * satisfied, which only matters once evaluation exists: until then
 * these dependencies do not change whether an expression parses.
 */
static const struct {
    const char *type;
    const char *value;
} qt3Unsupported[] = {
    /* Not planned (DESIGN.md, sections 2 and 9) */
    { "feature", "schemaImport" },
    { "feature", "schemaValidation" },
    { "feature", "schema-location-hint" },
    { "feature", "staticTyping" },
    { "feature", "typedData" },
    { "feature", "namespace-axis" },
    { "feature", "xpath-1.0-compatibility" },
    /* XQuery and XSLT */
    { "feature", "moduleImport" },
    { "feature", "fn-load-xquery-module" },
    { "feature", "fn-transform-XSLT" },
    { "feature", "fn-transform-XSLT30" },
    /* No network access */
    { "feature", "remote_http" },
    /* Names follow XML 1.0 Fifth Edition (xp3chars.c) */
    { "xml-version", "1.1" },
    { "xml-version", "1.0:4-" },
    /* Datatypes follow XML Schema 1.1 */
    { "xsd-version", "1.0" },
    /* One current Unicode version, no older ones (D4) */
    { "unicode-version", NULL },
    { "unicode-normalization-form", "FULLY-NORMALIZED" },
    /* Formatting beyond the defaults is not planned yet */
    { "format-integer-sequence", NULL },
    { "calendar", NULL },
    { "limits", NULL }
};

/* Languages for date and number formatting */
static const char *const qt3Languages[] = { "en" };

static int
isElement(xmlNodePtr node, const char *name) {
    return((node != NULL) && (node->type == XML_ELEMENT_NODE) &&
           (node->ns != NULL) &&
           xmlStrEqual(node->ns->href, BAD_CAST QT3_NS) &&
           xmlStrEqual(node->name, BAD_CAST name));
}

static xmlNodePtr
firstChild(xmlNodePtr node, const char *name) {
    xmlNodePtr cur;

    for (cur = node->children; cur != NULL; cur = cur->next) {
        if (isElement(cur, name))
            return(cur);
    }
    return(NULL);
}

/*
 * Whether one token of a spec dependency covers the run, for example
 * "XP30+" for XP31. XQuery tokens never do.
 */
static int
specTokenMatches(const qt3Run *run, const char *token, size_t len) {
    int version;

    if ((len < 4) || (strncmp(token, "XP", 2) != 0))
        return(0);
    if ((token[2] < '0') || (token[2] > '9') ||
        (token[3] < '0') || (token[3] > '9'))
        return(0);
    version = (token[2] - '0') * 10 + (token[3] - '0');
    if ((len == 5) && (token[4] == '+'))
        return(run->level >= version);
    return((len == 4) && (run->level == version));
}

/* Whether one token of another dependency type is supported. */
static int
tokenSupported(const char *type, const char *token, size_t len) {
    size_t i;

    if ((strcmp(type, "language") == 0) ||
        (strcmp(type, "default-language") == 0)) {
        for (i = 0; i < sizeof(qt3Languages) / sizeof(qt3Languages[0]); i++) {
            if ((strlen(qt3Languages[i]) == len) &&
                (strncmp(qt3Languages[i], token, len) == 0))
                return(1);
        }
        return(0);
    }
    for (i = 0; i < sizeof(qt3Unsupported) / sizeof(qt3Unsupported[0]); i++) {
        if (strcmp(qt3Unsupported[i].type, type) != 0)
            continue;
        if ((qt3Unsupported[i].value == NULL) ||
            ((strlen(qt3Unsupported[i].value) == len) &&
             (strncmp(qt3Unsupported[i].value, token, len) == 0)))
            return(0);
    }
    return(1);
}

/*
 * Whether a dependency is satisfied. Its value is a list of
 * alternatives separated by whitespace, and one is enough.
 */
static int
dependencySatisfied(const qt3Run *run, const char *type,
                    const char *value) {
    const char *cur = value;

    for (;;) {
        size_t len;

        while ((*cur == ' ') || (*cur == '\t') || (*cur == '\n') ||
               (*cur == '\r'))
            cur++;
        if (*cur == 0)
            return(0);
        len = strcspn(cur, " \t\n\r");
        if (strcmp(type, "spec") == 0) {
            if (specTokenMatches(run, cur, len))
                return(1);
        } else if (tokenSupported(type, cur, len)) {
            return(1);
        }
        cur += len;
    }
}

/*
 * Whether all dependencies directly below node are met. A dependency
 * with satisfied="false" is met when it is not satisfied.
 */
static int
dependenciesMet(const qt3Run *run, xmlNodePtr node) {
    xmlNodePtr cur;

    for (cur = node->children; cur != NULL; cur = cur->next) {
        xmlChar *type;
        xmlChar *value;
        xmlChar *satisfied;
        int ok;

        if (!isElement(cur, "dependency"))
            continue;
        type = xmlGetProp(cur, BAD_CAST "type");
        value = xmlGetProp(cur, BAD_CAST "value");
        satisfied = xmlGetProp(cur, BAD_CAST "satisfied");
        ok = dependencySatisfied(run, type ? (const char *) type : "",
                                 value ? (const char *) value : "");
        if (xmlStrEqual(satisfied, BAD_CAST "false"))
            ok = !ok;
        xmlFree(type);
        xmlFree(value);
        xmlFree(satisfied);
        if (!ok)
            return(0);
    }
    return(1);
}

/*
 * Whether an expected result accepts the outcome of parsing: code is
 * NULL if the expression parsed, otherwise the error code.
 */
static int
accepts(xmlNodePtr expected, const xmlChar *code) {
    xmlNodePtr cur;

    if (isElement(expected, "result") || isElement(expected, "any-of")) {
        for (cur = expected->children; cur != NULL; cur = cur->next) {
            if ((cur->type == XML_ELEMENT_NODE) && accepts(cur, code))
                return(1);
        }
        return(0);
    }
    if (isElement(expected, "all-of")) {
        for (cur = expected->children; cur != NULL; cur = cur->next) {
            if ((cur->type == XML_ELEMENT_NODE) && !accepts(cur, code))
                return(0);
        }
        return(1);
    }
    /* not(assertion): the assertion has to be evaluated to be negated,
     * so the expression has to parse. The suite never puts an error
     * element inside a not. */
    if (isElement(expected, "not"))
        return(code == NULL);
    if (isElement(expected, "error")) {
        xmlChar *expectedCode = xmlGetProp(expected, BAD_CAST "code");
        int ret;

        if (code == NULL)
            ret = !xmlStrEqual(expectedCode, BAD_CAST "XPST0003");
        else
            ret = xmlStrEqual(expectedCode, BAD_CAST "*") ||
                  xmlStrEqual(expectedCode, code);
        xmlFree(expectedCode);
        return(ret);
    }
    /* Assertions about the value, including "not": the expression has
     * to parse for them to be checked. */
    return(code == NULL);
}

/* A file name relative to the directory of base. */
static char *
resolvePath(const char *base, const char *name) {
    const char *slash = strrchr(base, '/');
    const char *backslash = strrchr(base, '\\');
    size_t dirLen;
    char *ret;

    if ((backslash != NULL) && ((slash == NULL) || (backslash > slash)))
        slash = backslash;
    dirLen = (slash != NULL) ? (size_t) (slash - base) + 1 : 0;
    ret = malloc(dirLen + strlen(name) + 1);
    if (ret == NULL)
        return(NULL);
    memcpy(ret, base, dirLen);
    strcpy(ret + dirLen, name);
    return(ret);
}

/* Read a whole file. Returns NULL on failure. */
static xmlChar *
readFile(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    xmlChar *buf = NULL;
    size_t cap = 0;

    *len = 0;
    if (f == NULL)
        return(NULL);
    for (;;) {
        size_t n;

        if (cap - *len < 4096) {
            xmlChar *tmp = xmlRealloc(buf, cap + 65536);

            if (tmp == NULL) {
                xmlFree(buf);
                fclose(f);
                return(NULL);
            }
            buf = tmp;
            cap += 65536;
        }
        n = fread(buf + *len, 1, cap - *len - 1, f);
        *len += n;
        if (n == 0)
            break;
    }
    fclose(f);
    buf[*len] = 0;
    return(buf);
}

/* Print text on one line, shortened to QT3_MAX_SHOWN bytes. */
static void
printShort(const char *label, const xmlChar *text, size_t len) {
    size_t i;

    printf("    %s: ", label);
    for (i = 0; (i < len) && (i < QT3_MAX_SHOWN); i++) {
        int c = text[i];

        putchar(((c == '\n') || (c == '\r') || (c == '\t')) ? ' ' : c);
    }
    printf("%s\n", (len > QT3_MAX_SHOWN) ? "..." : "");
}

static void
printExpected(xmlNodePtr result) {
    xmlBufferPtr buf = xmlBufferCreate();
    xmlNodePtr cur;

    if (buf == NULL)
        return;
    for (cur = result->children; cur != NULL; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE)
            xmlNodeDump(buf, result->doc, cur, 0, 0);
    }
    printShort("expected", xmlBufferContent(buf),
               (size_t) xmlBufferLength(buf));
    xmlBufferFree(buf);
}

/* Parse the expression of one test case and check the outcome. */
static void
runTestCase(qt3Run *run, const char *setName, const char *setPath,
            xmlNodePtr testCase) {
    xmlNodePtr test = firstChild(testCase, "test");
    xmlNodePtr result = firstChild(testCase, "result");
    xmlChar *name = xmlGetProp(testCase, BAD_CAST "name");
    xmlChar *file;
    xmlChar *expr = NULL;
    size_t len = 0;
    char id[512];
    xp3Error *err = NULL;
    xp3Ast *ast;
    const xmlChar *code = NULL;
    int passed;
    int known;

    snprintf(id, sizeof(id), "%s/%s", setName,
             name ? (const char *) name : "?");
    xmlFree(name);

    if ((test == NULL) || (result == NULL)) {
        printf("ERROR %s: no test or result element\n", id);
        run->failed++;
        return;
    }
    file = xmlGetProp(test, BAD_CAST "file");
    if (file != NULL) {
        char *path = resolvePath(setPath, (const char *) file);

        if (path != NULL)
            expr = readFile(path, &len);
        free(path);
        xmlFree(file);
    } else {
        expr = xmlNodeGetContent(test);
        if (expr != NULL)
            len = (size_t) xmlStrlen(expr);
    }
    if (expr == NULL) {
        printf("ERROR %s: cannot read the expression\n", id);
        run->failed++;
        return;
    }

    ast = xp3Parse(run->ctx, expr, len, &err);
    if (ast == NULL)
        code = xp3ErrorCode(err);
    passed = accepts(result, code);
    known = (run->known != NULL) &&
            (xmlHashLookup(run->known, BAD_CAST id) != NULL);

    if (passed) {
        run->passed++;
        if (known) {
            printf("FIXED %s (remove it from the known failures)\n", id);
            run->fixed++;
        }
    } else {
        if (known) {
            run->knownFailed++;
        } else {
            printf("FAIL %s\n", id);
            run->failed++;
        }
        if (run->verbose && (!known || (run->verbose > 1))) {
            char got[512];

            printShort("expression", expr, len);
            printExpected(result);
            if (ast != NULL)
                snprintf(got, sizeof(got), "parsed");
            else
                snprintf(got, sizeof(got), "%s at %d:%d: %s",
                         (const char *) code, xp3ErrorLine(err),
                         xp3ErrorColumn(err),
                         (const char *) xp3ErrorMessage(err));
            printShort("got", BAD_CAST got, strlen(got));
        }
    }

    xp3AstFree(ast);
    xp3ErrorFree(err);
    xmlFree(expr);
}

static int
runTestSet(qt3Run *run, const char *catalogPath, xmlNodePtr testSet) {
    xmlChar *name = xmlGetProp(testSet, BAD_CAST "name");
    xmlChar *file = xmlGetProp(testSet, BAD_CAST "file");
    char *path = NULL;
    xmlDocPtr doc = NULL;
    xmlNodePtr root;
    xmlNodePtr cur;
    int ret = -1;
    int passed = run->passed;
    int failed = run->failed + run->knownFailed;
    int notApplicable = run->notApplicable;

    if ((name == NULL) || (file == NULL)) {
        fprintf(stderr, "qt3run: test-set without name or file\n");
        goto done;
    }
    if ((run->prefix != NULL) &&
        (strncmp((const char *) name, run->prefix, strlen(run->prefix))
         != 0)) {
        ret = 0;
        goto done;
    }

    path = resolvePath(catalogPath, (const char *) file);
    if (path != NULL)
        doc = xmlReadFile(path, NULL, XML_PARSE_NONET);
    if (doc == NULL) {
        fprintf(stderr, "qt3run: cannot read test set %s\n",
                path ? path : (const char *) file);
        goto done;
    }
    root = xmlDocGetRootElement(doc);

    for (cur = root->children; cur != NULL; cur = cur->next) {
        if (!isElement(cur, "test-case"))
            continue;
        if (!dependenciesMet(run, root) || !dependenciesMet(run, cur)) {
            run->notApplicable++;
            continue;
        }
        runTestCase(run, (const char *) name, path, cur);
    }

    if (run->verbose) {
        failed = run->failed + run->knownFailed - failed;
        if ((run->passed - passed) + failed > 0)
            printf("%s: %d passed, %d failed, %d not applicable\n",
                   (const char *) name, run->passed - passed, failed,
                   run->notApplicable - notApplicable);
    }
    ret = 0;

done:
    xmlFreeDoc(doc);
    free(path);
    xmlFree(name);
    xmlFree(file);
    return(ret);
}

/*
 * Load known failures: one "set/case" per line, # starts a comment.
 */
static int
loadKnown(qt3Run *run, const char *path) {
    size_t len;
    xmlChar *text = readFile(path, &len);
    char *line;

    if (text == NULL) {
        fprintf(stderr, "qt3run: cannot read %s\n", path);
        return(-1);
    }
    run->known = xmlHashCreate(0);
    if (run->known == NULL) {
        xmlFree(text);
        return(-1);
    }
    for (line = strtok((char *) text, "\r\n"); line != NULL;
         line = strtok(NULL, "\r\n")) {
        char *end;

        if (strchr(line, '#') != NULL)
            *strchr(line, '#') = 0;
        while ((*line == ' ') || (*line == '\t'))
            line++;
        end = line + strlen(line);
        while ((end > line) && ((end[-1] == ' ') || (end[-1] == '\t')))
            *--end = 0;
        if (*line != 0)
            xmlHashAddEntry(run->known, BAD_CAST line, (void *) 1);
    }
    xmlFree(text);
    return(0);
}

static void
usage(void) {
    fprintf(stderr,
            "Usage: qt3run [options] CATALOG\n"
            "  -s SPEC    XP20, XP30 or XP31 (default XP31)\n"
            "  -t PREFIX  only test sets whose name starts with PREFIX\n"
            "  -k FILE    known failures, one set/case per line\n"
            "  -v         show failing expressions; twice: known ones too\n");
}

int
main(int argc, char **argv) {
    qt3Run run;
    const char *catalogPath = NULL;
    const char *knownPath = NULL;
    xmlDocPtr catalog;
    xmlNodePtr cur;
    int status = 0;
    int i;

    memset(&run, 0, sizeof(run));
    run.spec = "XP31";
    for (i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-s") == 0) && (i + 1 < argc)) {
            run.spec = argv[++i];
        } else if ((strcmp(argv[i], "-t") == 0) && (i + 1 < argc)) {
            run.prefix = argv[++i];
        } else if ((strcmp(argv[i], "-k") == 0) && (i + 1 < argc)) {
            knownPath = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0) {
            run.verbose++;
        } else if ((argv[i][0] != '-') && (catalogPath == NULL)) {
            catalogPath = argv[i];
        } else {
            usage();
            return(2);
        }
    }
    if (strcmp(run.spec, "XP20") == 0)
        run.level = XP3_XPATH_20;
    else if (strcmp(run.spec, "XP30") == 0)
        run.level = XP3_XPATH_30;
    else if (strcmp(run.spec, "XP31") == 0)
        run.level = XP3_XPATH_31;
    if ((catalogPath == NULL) || (run.level == 0)) {
        usage();
        return(2);
    }

    xmlInitParser();
    if ((knownPath != NULL) && (loadKnown(&run, knownPath) < 0))
        return(2);
    run.ctx = xp3NewStaticContext();
    if ((run.ctx == NULL) ||
        (xp3SetStaticContextLanguageLevel(run.ctx,
                                          (xp3LanguageLevel) run.level) < 0)) {
        fprintf(stderr, "qt3run: cannot create the static context\n");
        return(2);
    }

    catalog = xmlReadFile(catalogPath, NULL, XML_PARSE_NONET);
    if (catalog == NULL) {
        fprintf(stderr, "qt3run: cannot read %s\n", catalogPath);
        return(2);
    }
    for (cur = xmlDocGetRootElement(catalog)->children; cur != NULL;
         cur = cur->next) {
        if (isElement(cur, "test-set") &&
            (runTestSet(&run, catalogPath, cur) < 0))
            status = 2;
    }

    printf("%s parse-only: %d passed, %d failed", run.spec, run.passed,
           run.failed + run.knownFailed);
    if (run.knownFailed > 0)
        printf(" (%d known)", run.knownFailed);
    printf(", %d not applicable (%.2f%%)\n", run.notApplicable,
           (run.passed + run.failed + run.knownFailed > 0) ?
           100.0 * run.passed /
           (run.passed + run.failed + run.knownFailed) : 0.0);
    if (run.fixed > 0)
        printf("%d known failures now pass\n", run.fixed);

    if ((status == 0) && ((run.failed > 0) || (run.fixed > 0)))
        status = 1;

    xmlFreeDoc(catalog);
    xmlHashFree(run.known, NULL);
    xp3FreeStaticContext(run.ctx);
    return(status);
}
