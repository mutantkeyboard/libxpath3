/*
 * qt3run.c: run the W3C QT3 test suite against libxpath3
 *
 * Compile-only mode, the default, compiles each expression with the
 * namespaces and variables of its environment. A test passes if it
 * compiles and the expected result allows that, or if compiling fails
 * with an error code the expected result names. Static errors (XPST,
 * XQST) must be raised by compiling; any other expected error counts
 * as "may compile", because evaluation raises it.
 *
 * Evaluation mode (-e) also evaluates the expression, with the source
 * documents and parameters of its environment, and checks the result
 * against the assertions. Assertions written in XPath (assert,
 * assert-eq, assert-deep-eq, assert-type) are evaluated with $result
 * bound to the result. A test that needs something not implemented
 * yet, in the expression, its environment or its assertions, is "not
 * judged": it neither passes nor fails. Without -k, failures in this
 * mode are reported but don't change the exit status, so it can run
 * as a report while evaluation is incomplete.
 *
 * With -c, evaluation mode also compiles each test without
 * optimizing (no constant folding or path rewrites) and evaluates it
 * again. A test that passes one way and fails the other is a "fold
 * mismatch", a bug in the optimizer, which always fails the run.
 *
 * Usage: qt3run [-e [-c]] [-s XP20|XP30|XP31] [-t PREFIX] [-k FILE] [-v]
 *               CATALOG
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
#include "private/xp3context.h"
#include "private/xp3func.h"
#include "private/xp3nodes.h"
#include "private/xp3static.h"
#include "private/xp3types.h"
#include "private/xp3value.h"

#define QT3_NS "http://www.w3.org/2010/09/qt-fots-catalog"

/* Longest part of an expression or result shown for a failure */
#define QT3_MAX_SHOWN 300

typedef struct {
    int level;                  /* 20, 30 or 31 */
    const char *spec;           /* "XP20", "XP30" or "XP31" */
    const char *prefix;         /* only test sets starting with this */
    int verbose;
    int evaluate;               /* evaluation mode */
    int compare;                /* also evaluate without optimizing */
    xmlNodePtr catalogRoot;     /* for environments shared by test sets */
    xmlHashTable *known;        /* "set/case" of known failures */
    xmlHashTable *docs;         /* source documents by path */

    int passed;
    int failed;                 /* not in the known failures */
    int knownFailed;
    int fixed;                  /* known failures that now pass */
    int notApplicable;
    int notJudged;
    int mismatches;             /* optimized and not, different outcome */
} qt3Run;

/* How a test came out */
typedef enum {
    QT3_FAIL = 0,
    QT3_PASS,
    QT3_NOT_JUDGED
} qt3Outcome;

/* The evaluation of one test case */
typedef struct {
    xmlNodePtr env;             /* its environment, or NULL */
    const char *setPath;        /* its test set file */
    xp3DynamicContext *ctx;
    xp3Sequence *result;        /* NULL if evaluation failed */
    xp3Error *err;              /* the error otherwise */
} qt3Eval;

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
 * Whether a compiled expression calls fn:error, which can raise any
 * error code when evaluated, including static ones.
 */
static int
callsError(const xp3AstNode *node) {
    const xp3AstNode *child;

    if ((node->func != NULL) &&
        (node->func->ns == XP3_FUNC_NS_FN) &&
        (strcmp(node->func->name, "error") == 0))
        return(1);
    for (child = node->first; child != NULL; child = child->next) {
        if (callsError(child))
            return(1);
    }
    return(0);
}

/* Whether an error code is a static error, which compiling raises */
static int
isStaticCode(const xmlChar *code) {
    return((code != NULL) && ((xmlStrncmp(code, BAD_CAST "XPST", 4) == 0) ||
                              (xmlStrncmp(code, BAD_CAST "XQST", 4) == 0)));
}

/*
 * Whether an expected result accepts the outcome of compiling: code is
 * NULL if the expression compiled, otherwise the error code. dynamic
 * is set if a compiled expression can raise static error codes when
 * evaluated, through fn:error.
 */
static int
accepts(xmlNodePtr expected, const xmlChar *code, int dynamic) {
    xmlNodePtr cur;

    if (isElement(expected, "result") || isElement(expected, "any-of")) {
        for (cur = expected->children; cur != NULL; cur = cur->next) {
            if ((cur->type == XML_ELEMENT_NODE) &&
                accepts(cur, code, dynamic))
                return(1);
        }
        return(0);
    }
    if (isElement(expected, "all-of")) {
        for (cur = expected->children; cur != NULL; cur = cur->next) {
            if ((cur->type == XML_ELEMENT_NODE) &&
                !accepts(cur, code, dynamic))
                return(0);
        }
        return(1);
    }
    /* not(assertion): the assertion has to be evaluated to be negated,
     * so the expression has to compile. The suite never puts an error
     * element inside a not. */
    if (isElement(expected, "not"))
        return(code == NULL);
    if (isElement(expected, "error")) {
        xmlChar *expectedCode = xmlGetProp(expected, BAD_CAST "code");
        int ret;

        if (code == NULL)
            ret = dynamic || !isStaticCode(expectedCode);
        else
            ret = xmlStrEqual(expectedCode, BAD_CAST "*") ||
                  xmlStrEqual(expectedCode, code);
        xmlFree(expectedCode);
        return(ret);
    }
    /* Assertions about the value: the expression has to compile for
     * them to be checked. */
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

/*
 * Declare the variable an environment names, as "name" or "prefix:name"
 * with the prefix bound in the catalog, as on <param name="...">, or
 * as "$name" on <source role="...">.
 */
static int
declareVariable(xp3StaticContext *ctx, xmlNodePtr node, const xmlChar *name) {
    const xmlChar *colon = xmlStrchr(name, ':');
    const xmlChar *uri = NULL;
    xmlChar *prefix;
    xmlNsPtr ns;
    int ret;

    if (colon == NULL)
        return(xp3DeclareVariable(ctx, NULL, name));
    prefix = xmlStrndup(name, (int) (colon - name));
    if (prefix == NULL)
        return(-1);
    ns = xmlSearchNs(node->doc, node, prefix);
    xmlFree(prefix);
    if (ns != NULL)
        uri = ns->href;
    ret = (uri != NULL) ? xp3DeclareVariable(ctx, uri, colon + 1) : -1;
    return(ret);
}

/*
 * Apply what an environment declares for compiling: namespaces, where
 * the empty prefix sets the default element namespace, and the
 * variables of <param> and <source role="$name">.
 */
static int
applyEnvironment(xp3StaticContext *ctx, xmlNodePtr env) {
    xmlNodePtr cur;
    int ret = 0;

    for (cur = env->children; (cur != NULL) && (ret == 0); cur = cur->next) {
        if (isElement(cur, "namespace")) {
            xmlChar *prefix = xmlGetProp(cur, BAD_CAST "prefix");
            xmlChar *uri = xmlGetProp(cur, BAD_CAST "uri");

            if ((prefix == NULL) || (*prefix == 0))
                ret = xp3SetDefaultElementNamespace(ctx, uri);
            else
                ret = xp3DeclareNamespace(ctx, prefix, uri);
            xmlFree(prefix);
            xmlFree(uri);
        } else if (isElement(cur, "param")) {
            xmlChar *name = xmlGetProp(cur, BAD_CAST "name");

            if (name != NULL)
                ret = declareVariable(ctx, cur, name);
            xmlFree(name);
        } else if (isElement(cur, "source")) {
            xmlChar *role = xmlGetProp(cur, BAD_CAST "role");

            if ((role != NULL) && (role[0] == '$'))
                ret = declareVariable(ctx, cur, role + 1);
            xmlFree(role);
        }
    }
    return(ret);
}

/* The named environment in a test set or, failing that, the catalog. */
static xmlNodePtr
findEnvironment(const qt3Run *run, xmlNodePtr setRoot, const xmlChar *name) {
    xmlNodePtr roots[2];
    xmlNodePtr cur;
    int i;

    roots[0] = setRoot;
    roots[1] = run->catalogRoot;
    for (i = 0; i < 2; i++) {
        for (cur = roots[i]->children; cur != NULL; cur = cur->next) {
            xmlChar *envName;
            int found;

            if (!isElement(cur, "environment"))
                continue;
            envName = xmlGetProp(cur, BAD_CAST "name");
            found = xmlStrEqual(envName, name);
            xmlFree(envName);
            if (found)
                return(cur);
        }
    }
    return(NULL);
}

/*
 * The static context of a test case: the language level of the run and
 * the declarations of its environment. Returns NULL on failure.
 */
/* The environment of a test case, following a reference, or NULL */
static xmlNodePtr
testEnvironment(const qt3Run *run, xmlNodePtr setRoot, xmlNodePtr testCase) {
    xmlNodePtr env = firstChild(testCase, "environment");
    xmlChar *ref;

    if (env == NULL)
        return(NULL);
    ref = xmlGetProp(env, BAD_CAST "ref");
    if (ref != NULL) {
        env = findEnvironment(run, setRoot, ref);
        xmlFree(ref);
    }
    return(env);
}

/*
 * A static context with a language level and the declarations of an
 * environment, which may be NULL. Returns NULL on failure.
 */
static xp3StaticContext *
envContext(int level, xmlNodePtr env) {
    xp3StaticContext *ctx = xp3NewStaticContext();

    if ((ctx == NULL) ||
        (xp3SetStaticContextLanguageLevel(ctx, (xp3LanguageLevel) level) < 0)
        || ((env != NULL) && (applyEnvironment(ctx, env) < 0))) {
        xp3FreeStaticContext(ctx);
        return(NULL);
    }
    return(ctx);
}

/*
 * Evaluation mode: the dynamic context
 */

static void
freeDoc(void *payload, const xmlChar *name) {
    (void) name;
    xmlFreeDoc((xmlDocPtr) payload);
}

/* A source document, parsed once per run as libxslt parses documents */
static xmlDocPtr
loadSource(qt3Run *run, const char *path) {
    xmlDocPtr doc;

    if (run->docs == NULL) {
        run->docs = xmlHashCreate(0);
        if (run->docs == NULL)
            return(NULL);
    }
    doc = xmlHashLookup(run->docs, BAD_CAST path);
    if (doc != NULL)
        return(doc);
    doc = xmlReadFile(path, NULL, XML_PARSE_NOENT | XML_PARSE_DTDLOAD |
                                  XML_PARSE_DTDATTR | XML_PARSE_NONET);
    if ((doc != NULL) &&
        (xmlHashAddEntry(run->docs, BAD_CAST path, doc) < 0)) {
        xmlFreeDoc(doc);
        return(NULL);
    }
    return(doc);
}

/*
 * Bind a value to the variable a name like "name" or "prefix:name"
 * refers to, with the prefix bound where the name is written.
 */
static int
bindName(xp3DynamicContext *ctx, xmlNodePtr node, const xmlChar *name,
         xp3Sequence *value) {
    const xmlChar *colon = xmlStrchr(name, ':');
    xmlChar *prefix;
    xmlNsPtr ns;

    if (colon == NULL)
        return(xp3BindVariable(ctx, NULL, name, value));
    prefix = xmlStrndup(name, (int) (colon - name));
    if (prefix == NULL)
        return(-1);
    ns = xmlSearchNs(node->doc, node, prefix);
    xmlFree(prefix);
    if (ns == NULL)
        return(-1);
    return(xp3BindVariable(ctx, ns->href, colon + 1, value));
}

/* <source role="." or "$name" file="...">: a document */
static qt3Outcome
applySource(qt3Run *run, xp3DynamicContext *ctx, xmlNodePtr source) {
    xmlChar *role = xmlGetProp(source, BAD_CAST "role");
    xmlChar *file = xmlGetProp(source, BAD_CAST "file");
    xmlChar *validation = xmlGetProp(source, BAD_CAST "validation");
    qt3Outcome ret = QT3_NOT_JUDGED;
    char *path = NULL;
    xmlDocPtr doc;

    /* Sources without a role are for fn:doc(), and typed ones need a
     * schema */
    if ((role == NULL) || (file == NULL) ||
        ((validation != NULL) && !xmlStrEqual(validation, BAD_CAST "skip")))
        goto done;
    path = resolvePath((const char *) source->doc->URL, (const char *) file);
    doc = (path != NULL) ? loadSource(run, path) : NULL;
    if (doc == NULL) {
        printf("ERROR: cannot read the source %s\n", (const char *) file);
        ret = QT3_FAIL;
    } else if (xmlStrEqual(role, BAD_CAST ".")) {
        if (xp3SetContextNode(ctx, (xmlNodePtr) doc) == 0)
            ret = QT3_PASS;
    } else if (role[0] == '$') {
        xmlNodePtr node = (xmlNodePtr) doc;
        xp3Sequence *value = xp3NewNodeSequence(&node, 1);

        if ((value != NULL) && (bindName(ctx, source, role + 1, value) == 0))
            ret = QT3_PASS;
        xp3FreeSequence(value);
    }

done:
    free(path);
    xmlFree(role);
    xmlFree(file);
    xmlFree(validation);
    return(ret);
}

/* <param name="..." select="...">: the value of an expression */
static qt3Outcome
applyParam(xmlNodePtr env, xp3DynamicContext *ctx, xmlNodePtr param) {
    xmlChar *name = xmlGetProp(param, BAD_CAST "name");
    xmlChar *select = xmlGetProp(param, BAD_CAST "select");
    qt3Outcome ret = QT3_NOT_JUDGED;
    xp3StaticContext *sctx = NULL;
    xp3Sequence *value = NULL;
    xp3Expr *expr = NULL;

    if ((name == NULL) || (select == NULL))
        goto done;
    sctx = envContext(XP3_XPATH_31, env);
    if (sctx != NULL)
        expr = xp3Compile(sctx, select, NULL);
    if (expr != NULL)
        value = xp3Evaluate(expr, NULL, NULL);
    if ((value != NULL) && (bindName(ctx, param, name, value) == 0))
        ret = QT3_PASS;

done:
    xp3FreeSequence(value);
    xp3FreeExpr(expr);
    xp3FreeStaticContext(sctx);
    xmlFree(name);
    xmlFree(select);
    return(ret);
}

/*
 * Set up the dynamic context of an environment: QT3_PASS when done,
 * QT3_NOT_JUDGED if it needs something not supported, such as
 * collections, resources or a typed source.
 */
static qt3Outcome
applyDynamic(qt3Run *run, xmlNodePtr env, xp3DynamicContext *ctx) {
    xmlNodePtr cur;
    qt3Outcome ret = QT3_PASS;

    for (cur = env->children; (cur != NULL) && (ret == QT3_PASS);
         cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE)
            continue;
        if (isElement(cur, "source")) {
            ret = applySource(run, ctx, cur);
        } else if (isElement(cur, "param")) {
            ret = applyParam(env, ctx, cur);
        } else if (isElement(cur, "collation")) {
            xmlChar *uri = xmlGetProp(cur, BAD_CAST "uri");

            if (!xmlStrEqual(uri, BAD_CAST "http://www.w3.org/2005/"
                             "xpath-functions/collation/codepoint"))
                ret = QT3_NOT_JUDGED;
            xmlFree(uri);
        } else if (!isElement(cur, "namespace") &&
                   !isElement(cur, "description") &&
                   !isElement(cur, "created") &&
                   !isElement(cur, "modified")) {
            /* collection, resource, context-item, decimal-format,
             * static-base-uri, schema */
            ret = QT3_NOT_JUDGED;
        }
    }
    return(ret);
}

/*
 * Evaluation mode: assertions
 */

/* The string value of every item, separated by spaces, or NULL if an
 * item has none yet */
static xmlChar *
joinStrings(const xp3Sequence *seq) {
    xmlChar *joined = xmlStrdup(BAD_CAST "");
    size_t i;

    for (i = 0; (joined != NULL) && (i < xp3SequenceLength(seq)); i++) {
        xmlChar *str = xp3SequenceStringValue(seq, i);

        if (str == NULL) {
            xmlFree(joined);
            return(NULL);
        }
        if (i > 0)
            joined = xmlStrcat(joined, BAD_CAST " ");
        if (joined != NULL)
            joined = xmlStrcat(joined, str);
        xmlFree(str);
    }
    return(joined);
}

/* Collapse whitespace to single spaces and trim it, in place */
static void
normalizeSpace(xmlChar *str) {
    xmlChar *out = str;
    const xmlChar *in = str;
    int space = 0;

    while ((*in == ' ') || (*in == '\t') || (*in == '\n') || (*in == '\r'))
        in++;
    for (; *in != 0; in++) {
        if ((*in == ' ') || (*in == '\t') || (*in == '\n') ||
            (*in == '\r')) {
            space = 1;
            continue;
        }
        if (space)
            *out++ = ' ';
        space = 0;
        *out++ = *in;
    }
    *out = 0;
}

static qt3Outcome
assertStringValue(const qt3Eval *ev, xmlNodePtr assertion) {
    xmlChar *expected = xmlNodeGetContent(assertion);
    xmlChar *normalize = xmlGetProp(assertion, BAD_CAST "normalize-space");
    xmlChar *actual = joinStrings(ev->result);
    qt3Outcome ret = QT3_NOT_JUDGED;

    if ((expected != NULL) && (actual != NULL)) {
        if (xmlStrEqual(normalize, BAD_CAST "true") ||
            xmlStrEqual(normalize, BAD_CAST "1")) {
            normalizeSpace(expected);
            normalizeSpace(actual);
        }
        ret = xmlStrEqual(expected, actual) ? QT3_PASS : QT3_FAIL;
    }
    xmlFree(expected);
    xmlFree(normalize);
    xmlFree(actual);
    return(ret);
}

/* Whether the result is a single xs:boolean of this value */
static qt3Outcome
assertBoolean(const qt3Eval *ev, int value) {
    xp3Item item;
    int ok = 0;

    if ((xp3SequenceLength(ev->result) == 1) &&
        (xp3ValueItemAt(ev->result, 0, &item) > 0)) {
        ok = (item.kind == XP3_ITEM_ATOMIC) &&
             (item.u.atomic->type == XP3_TYPE_BOOLEAN) &&
             (item.u.atomic->u.boolean == value);
        xp3ItemUnref(item);
    }
    return(ok ? QT3_PASS : QT3_FAIL);
}

static int
deepEqualNodes(xmlNodePtr first, xmlNodePtr second);

/* Whether the XDM children of two nodes are deep-equal */
static int
deepEqualChildren(xmlNodePtr first, xmlNodePtr second) {
    xmlNodePtr firstChild = NULL;
    xmlNodePtr secondChild = NULL;

    for (;;) {
        firstChild = xp3AxisNext(XP3_AXIS_CHILD, first, firstChild);
        secondChild = xp3AxisNext(XP3_AXIS_CHILD, second, secondChild);
        if ((firstChild == NULL) || (secondChild == NULL))
            return(firstChild == secondChild);
        if (!deepEqualNodes(firstChild, secondChild))
            return(0);
    }
}

/* Whether the string values of two nodes are equal */
static int
sameStringValue(xmlNodePtr first, xmlNodePtr second) {
    xmlChar *firstValue = NULL;
    xmlChar *secondValue = NULL;
    int ret;

    xp3NodeStringValue(first, &firstValue, NULL);
    xp3NodeStringValue(second, &secondValue, NULL);
    ret = (firstValue != NULL) && xmlStrEqual(firstValue, secondValue);
    xmlFree(firstValue);
    xmlFree(secondValue);
    return(ret);
}

/* Deep equality of untyped nodes, as fn:deep-equal defines it, with
 * comments and processing instructions compared too */
static int
deepEqualNodes(xmlNodePtr first, xmlNodePtr second) {
    xp3NodeKind kind = xp3NodeGetKind(first);
    xmlNodePtr attr = NULL;
    int count = 0;

    if ((kind != xp3NodeGetKind(second)) ||
        !xmlStrEqual(xp3NodeLocalName(first), xp3NodeLocalName(second)) ||
        !xmlStrEqual(xp3NodeNamespaceUri(first), xp3NodeNamespaceUri(second)))
        return(0);
    if (kind == XP3_NODE_DOCUMENT)
        return(deepEqualChildren(first, second));
    if (kind != XP3_NODE_ELEMENT)
        return(sameStringValue(first, second));

    while ((attr = xp3AxisNext(XP3_AXIS_ATTRIBUTE, first, attr)) != NULL) {
        xmlNodePtr other = NULL;

        count++;
        while (((other = xp3AxisNext(XP3_AXIS_ATTRIBUTE, second, other)) !=
                NULL) && !deepEqualNodes(attr, other))
            ;
        if (other == NULL)
            return(0);
    }
    while ((attr = xp3AxisNext(XP3_AXIS_ATTRIBUTE, second, attr)) != NULL)
        count--;
    return((count == 0) && deepEqualChildren(first, second));
}

/*
 * <assert-xml>: the result, serialized, is the XML given. Compared
 * here as trees: the result nodes, with document nodes replaced by
 * their children, against the nodes of the expected XML.
 */
static qt3Outcome
assertXml(const qt3Eval *ev, xmlNodePtr assertion) {
    xmlChar *file = xmlGetProp(assertion, BAD_CAST "file");
    xmlChar *xml = NULL;
    xmlChar *wrapped = NULL;
    xmlDocPtr doc = NULL;
    xmlNodePtr expected = NULL;
    qt3Outcome ret = QT3_NOT_JUDGED;
    size_t len;
    size_t i;

    if (file != NULL) {
        char *path = resolvePath(ev->setPath, (const char *) file);

        if (path != NULL)
            xml = readFile(path, &len);
        free(path);
    } else {
        xml = xmlNodeGetContent(assertion);
    }
    if (xml == NULL)
        goto done;
    /* Drop an XML declaration, which can't be wrapped */
    if (xmlStrncmp(xml, BAD_CAST "<?xml ", 6) == 0) {
        const xmlChar *end = xmlStrstr(xml, BAD_CAST "?>");

        if (end != NULL)
            memmove(xml, end + 2, (size_t) xmlStrlen(end + 2) + 1);
    }
    wrapped = xmlStrdup(BAD_CAST "<qt3-wrapper>");
    wrapped = xmlStrcat(wrapped, xml);
    wrapped = xmlStrcat(wrapped, BAD_CAST "</qt3-wrapper>");
    if (wrapped != NULL)
        doc = xmlReadMemory((const char *) wrapped, xmlStrlen(wrapped),
                            "expected.xml", NULL, XML_PARSE_NOENT);
    if (doc == NULL)
        goto done;

    ret = QT3_PASS;
    for (i = 0; (i < xp3SequenceLength(ev->result)) && (ret == QT3_PASS);
         i++) {
        xmlNodePtr node = xp3SequenceNode(ev->result, i);
        xmlNodePtr child = NULL;
        int isDocument;

        if ((node == NULL) ||
            (xp3NodeGetKind(node) == XP3_NODE_ATTRIBUTE)) {
            ret = QT3_NOT_JUDGED;
            break;
        }
        isDocument = (xp3NodeGetKind(node) == XP3_NODE_DOCUMENT);
        if (isDocument)
            child = xp3AxisNext(XP3_AXIS_CHILD, node, NULL);
        else
            child = node;
        for (; child != NULL;
             child = isDocument ? xp3AxisNext(XP3_AXIS_CHILD, node, child) :
                                  NULL) {
            expected = xp3AxisNext(XP3_AXIS_CHILD,
                                   xmlDocGetRootElement(doc), expected);
            if ((expected == NULL) || !deepEqualNodes(child, expected)) {
                ret = QT3_FAIL;
                break;
            }
        }
    }
    if ((ret == QT3_PASS) &&
        (xp3AxisNext(XP3_AXIS_CHILD, xmlDocGetRootElement(doc), expected) !=
         NULL))
        ret = QT3_FAIL;

done:
    xmlFreeDoc(doc);
    xmlFree(wrapped);
    xmlFree(xml);
    xmlFree(file);
    return(ret);
}

/*
 * An assertion written in XPath: before, the assertion, and after,
 * evaluated with $result bound to the result, at the highest language
 * level and with the namespaces of the environment. QT3_NOT_JUDGED if
 * it needs something not implemented yet.
 */
static qt3Outcome
assertExpression(const qt3Eval *ev, const char *before,
                 xmlNodePtr assertion, const char *after) {
    xmlChar *body = xmlNodeGetContent(assertion);
    xmlChar *text = xmlStrdup(BAD_CAST before);
    xp3StaticContext *sctx = envContext(XP3_XPATH_31, ev->env);
    qt3Outcome ret = QT3_FAIL;
    xp3Error *err = NULL;
    xp3Expr *expr = NULL;
    int value;

    text = xmlStrcat(text, body);
    text = xmlStrcat(text, BAD_CAST after);
    if ((text == NULL) || (sctx == NULL) ||
        (xp3DeclareVariable(sctx, NULL, BAD_CAST "result") < 0) ||
        (xp3BindVariable(ev->ctx, NULL, BAD_CAST "result", ev->result) < 0)) {
        ret = QT3_NOT_JUDGED;
        goto done;
    }
    expr = xp3Compile(sctx, text, &err);
    if (expr == NULL) {
        if (err != NULL)
            printf("    the assertion does not compile: %s\n",
                   (const char *) xp3ErrorMessage(err));
        goto done;
    }
    value = xp3EvaluateBoolean(expr, ev->ctx, &err);
    if ((value < 0) &&
        xmlStrEqual(xp3ErrorCode(err), BAD_CAST "UNSUPPORTED"))
        ret = QT3_NOT_JUDGED;
    else if (value > 0)
        ret = QT3_PASS;

done:
    xp3BindVariable(ev->ctx, NULL, BAD_CAST "result", NULL);
    xp3ErrorFree(err);
    xp3FreeExpr(expr);
    xp3FreeStaticContext(sctx);
    xmlFree(body);
    xmlFree(text);
    return(ret);
}

/* Whether an error code matches an <error code="..."/> */
static qt3Outcome
assertError(const qt3Eval *ev, xmlNodePtr assertion) {
    xmlChar *expected = xmlGetProp(assertion, BAD_CAST "code");
    int ok;

    if (ev->result != NULL) {
        xmlFree(expected);
        return(QT3_FAIL);
    }
    ok = xmlStrEqual(expected, BAD_CAST "*") ||
         xmlStrEqual(expected, xp3ErrorCode(ev->err));
    xmlFree(expected);
    return(ok ? QT3_PASS : QT3_FAIL);
}

/* Check an evaluation against the expected result */
static qt3Outcome
judge(const qt3Eval *ev, xmlNodePtr expected) {
    xmlNodePtr cur;
    qt3Outcome ret;
    xmlChar *text;

    if (isElement(expected, "result") || isElement(expected, "any-of")) {
        ret = QT3_FAIL;
        for (cur = expected->children; cur != NULL; cur = cur->next) {
            qt3Outcome outcome;

            if (cur->type != XML_ELEMENT_NODE)
                continue;
            outcome = judge(ev, cur);
            if (outcome == QT3_PASS)
                return(QT3_PASS);
            if (outcome == QT3_NOT_JUDGED)
                ret = QT3_NOT_JUDGED;
        }
        return(ret);
    }
    if (isElement(expected, "all-of")) {
        ret = QT3_PASS;
        for (cur = expected->children; cur != NULL; cur = cur->next) {
            qt3Outcome outcome;

            if (cur->type != XML_ELEMENT_NODE)
                continue;
            outcome = judge(ev, cur);
            if (outcome == QT3_FAIL)
                return(QT3_FAIL);
            if (outcome == QT3_NOT_JUDGED)
                ret = QT3_NOT_JUDGED;
        }
        return(ret);
    }
    if (isElement(expected, "not")) {
        for (cur = expected->children; cur != NULL; cur = cur->next) {
            if (cur->type == XML_ELEMENT_NODE)
                break;
        }
        ret = (cur != NULL) ? judge(ev, cur) : QT3_NOT_JUDGED;
        if (ret == QT3_NOT_JUDGED)
            return(ret);
        return((ret == QT3_PASS) ? QT3_FAIL : QT3_PASS);
    }
    if (isElement(expected, "error"))
        return(assertError(ev, expected));

    /* Assertions about the value */
    if (ev->result == NULL)
        return(QT3_FAIL);
    if (isElement(expected, "assert-empty"))
        return((xp3SequenceLength(ev->result) == 0) ? QT3_PASS : QT3_FAIL);
    if (isElement(expected, "assert-count")) {
        text = xmlNodeGetContent(expected);
        ret = ((text != NULL) &&
               ((size_t) atol((const char *) text) ==
                xp3SequenceLength(ev->result))) ? QT3_PASS : QT3_FAIL;
        xmlFree(text);
        return(ret);
    }
    if (isElement(expected, "assert-true"))
        return(assertBoolean(ev, 1));
    if (isElement(expected, "assert-false"))
        return(assertBoolean(ev, 0));
    if (isElement(expected, "assert-string-value"))
        return(assertStringValue(ev, expected));
    if (isElement(expected, "assert-xml"))
        return(assertXml(ev, expected));
    if (isElement(expected, "assert"))
        return(assertExpression(ev, "", expected, ""));
    if (isElement(expected, "assert-eq"))
        return(assertExpression(ev, "$result eq (", expected, ")"));
    if (isElement(expected, "assert-deep-eq"))
        return(assertExpression(ev, "deep-equal($result, (", expected,
                                "))"));
    if (isElement(expected, "assert-type"))
        return(assertExpression(ev, "$result instance of ", expected, ""));
    /* assert-permutation and the serialization assertions */
    return(QT3_NOT_JUDGED);
}

/*
 * Evaluate a compiled test and judge the outcome. The result and error
 * are left in ev for messages.
 */
static qt3Outcome
evaluateTest(qt3Run *run, xp3Expr *compiled, xmlNodePtr expected,
             qt3Eval *ev) {
    qt3Outcome ret;

    ev->ctx = xp3NewDynamicContext();
    if (ev->ctx == NULL)
        return(QT3_FAIL);
    if (ev->env != NULL) {
        ret = applyDynamic(run, ev->env, ev->ctx);
        if (ret != QT3_PASS)
            return(ret);
    }
    ev->result = xp3Evaluate(compiled, ev->ctx, &ev->err);
    if ((ev->result == NULL) &&
        xmlStrEqual(xp3ErrorCode(ev->err), BAD_CAST "UNSUPPORTED"))
        return(QT3_NOT_JUDGED);
    /* Assertions see $result, not the context item of the test */
    xp3SetContextNode(ev->ctx, NULL);
    return(judge(ev, expected));
}

static const char *
outcomeName(qt3Outcome outcome) {
    return((outcome == QT3_PASS) ? "passes" :
           (outcome == QT3_FAIL) ? "fails" : "is not judged");
}

/*
 * Compile a test again without optimizing, evaluate it, and report a
 * mismatch if one way passes and the other fails.
 */
static void
compareUnoptimized(qt3Run *run, const char *id, xp3StaticContext *ctx,
                   const xmlChar *expr, xmlNodePtr expected,
                   const qt3Eval *optimized, qt3Outcome outcome) {
    qt3Eval plain;
    qt3Outcome plainOutcome = QT3_FAIL;
    xp3Expr *compiled;

    memset(&plain, 0, sizeof(plain));
    plain.env = optimized->env;
    plain.setPath = optimized->setPath;
    ctx->noOptimize = 1;
    compiled = xp3Compile(ctx, expr, NULL);
    ctx->noOptimize = 0;
    if (compiled != NULL)
        plainOutcome = evaluateTest(run, compiled, expected, &plain);
    if ((outcome != plainOutcome) && (outcome != QT3_NOT_JUDGED) &&
        (plainOutcome != QT3_NOT_JUDGED)) {
        printf("FOLD MISMATCH %s: optimized it %s, without it %s\n", id,
               outcomeName(outcome), outcomeName(plainOutcome));
        run->mismatches++;
    } else if ((outcome != plainOutcome) && (run->verbose > 1)) {
        printf("NOTE %s: optimized it %s, without it %s\n", id,
               outcomeName(outcome), outcomeName(plainOutcome));
    }
    xp3FreeSequence(plain.result);
    xp3ErrorFree(plain.err);
    xp3FreeDynamicContext(plain.ctx);
    xp3FreeExpr(compiled);
}

/* Print what an evaluation gave, for -v */
static void
printEvaluation(const qt3Eval *ev) {
    char got[QT3_MAX_SHOWN + 64];
    xmlChar *joined;

    if (ev->result == NULL) {
        snprintf(got, sizeof(got), "%s at %d:%d: %s",
                 (const char *) xp3ErrorCode(ev->err),
                 xp3ErrorLine(ev->err), xp3ErrorColumn(ev->err),
                 (const char *) xp3ErrorMessage(ev->err));
        printShort("got", BAD_CAST got, strlen(got));
        return;
    }
    joined = joinStrings(ev->result);
    snprintf(got, sizeof(got), "%lu item(s): %s",
             (unsigned long) xp3SequenceLength(ev->result),
             (joined != NULL) ? (const char *) joined : "?");
    printShort("got", BAD_CAST got, strlen(got));
    xmlFree(joined);
}

/* Compile, and in evaluation mode evaluate, one test case and check
 * the outcome. */
static void
runTestCase(qt3Run *run, const char *setName, const char *setPath,
            xmlNodePtr setRoot, xmlNodePtr testCase) {
    xmlNodePtr test = firstChild(testCase, "test");
    xmlNodePtr result = firstChild(testCase, "result");
    xmlChar *name = xmlGetProp(testCase, BAD_CAST "name");
    xmlChar *file;
    xmlChar *expr = NULL;
    size_t len = 0;
    char id[512];
    xp3Error *err = NULL;
    xp3StaticContext *ctx;
    xp3Expr *compiled;
    const xmlChar *code = NULL;
    qt3Eval ev;
    qt3Outcome outcome;
    int known;

    snprintf(id, sizeof(id), "%s/%s", setName,
             name ? (const char *) name : "?");
    xmlFree(name);
    memset(&ev, 0, sizeof(ev));
    ev.setPath = setPath;

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

    ev.env = testEnvironment(run, setRoot, testCase);
    ctx = envContext(run->level, ev.env);
    if (ctx == NULL) {
        printf("ERROR %s: cannot set up the environment\n", id);
        run->failed++;
        xmlFree(expr);
        return;
    }
    compiled = xp3Compile(ctx, expr, &err);
    if (compiled == NULL)
        code = xp3ErrorCode(err);
    if (run->evaluate && (compiled != NULL)) {
        outcome = evaluateTest(run, compiled, result, &ev);
        if (run->compare)
            compareUnoptimized(run, id, ctx, expr, result, &ev, outcome);
    } else
        outcome = accepts(result, code, (compiled != NULL) &&
                          callsError(compiled->ast->root)) ?
                  QT3_PASS : QT3_FAIL;
    known = (run->known != NULL) &&
            (xmlHashLookup(run->known, BAD_CAST id) != NULL);

    if (outcome == QT3_NOT_JUDGED) {
        run->notJudged++;
    } else if (outcome == QT3_PASS) {
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
            if (run->evaluate && (compiled != NULL)) {
                printEvaluation(&ev);
            } else {
                if (compiled != NULL)
                    snprintf(got, sizeof(got), "compiled");
                else
                    snprintf(got, sizeof(got), "%s at %d:%d: %s",
                             (const char *) code, xp3ErrorLine(err),
                             xp3ErrorColumn(err),
                             (const char *) xp3ErrorMessage(err));
                printShort("got", BAD_CAST got, strlen(got));
            }
        }
    }

    xp3FreeSequence(ev.result);
    xp3ErrorFree(ev.err);
    xp3FreeDynamicContext(ev.ctx);
    xp3FreeExpr(compiled);
    xp3FreeStaticContext(ctx);
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
    int notJudged = run->notJudged;

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
        runTestCase(run, (const char *) name, path, root, cur);
    }

    if (run->verbose) {
        failed = run->failed + run->knownFailed - failed;
        if ((run->passed - passed) + failed > 0)
            printf("%s: %d passed, %d failed, %d not judged, "
                   "%d not applicable\n", (const char *) name,
                   run->passed - passed, failed,
                   run->notJudged - notJudged,
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
            "  -e         evaluate, not only compile\n"
            "  -c         with -e, also evaluate without optimizing and\n"
            "             report every test whose outcome differs\n"
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
        if (strcmp(argv[i], "-e") == 0) {
            run.evaluate = 1;
        } else if (strcmp(argv[i], "-c") == 0) {
            run.compare = 1;
        } else if ((strcmp(argv[i], "-s") == 0) && (i + 1 < argc)) {
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
    catalog = xmlReadFile(catalogPath, NULL, XML_PARSE_NONET);
    if (catalog == NULL) {
        fprintf(stderr, "qt3run: cannot read %s\n", catalogPath);
        return(2);
    }
    run.catalogRoot = xmlDocGetRootElement(catalog);
    for (cur = run.catalogRoot->children; cur != NULL;
         cur = cur->next) {
        if (isElement(cur, "test-set") &&
            (runTestSet(&run, catalogPath, cur) < 0))
            status = 2;
    }

    printf("%s %s: %d passed, %d failed", run.spec,
           run.evaluate ? "evaluation" : "compile-only", run.passed,
           run.failed + run.knownFailed);
    if (run.knownFailed > 0)
        printf(" (%d known)", run.knownFailed);
    if (run.evaluate)
        printf(", %d not judged", run.notJudged);
    if (run.compare)
        printf(", %d fold mismatches", run.mismatches);
    printf(", %d not applicable (%.2f%%)\n", run.notApplicable,
           (run.passed + run.failed + run.knownFailed > 0) ?
           100.0 * run.passed /
           (run.passed + run.failed + run.knownFailed) : 0.0);
    if (run.fixed > 0)
        printf("%d known failures now pass\n", run.fixed);

    /* Evaluation without known failures is a report */
    if ((status == 0) && (!run.evaluate || (run.known != NULL)) &&
        ((run.failed > 0) || (run.fixed > 0)))
        status = 1;
    /* An optimizer bug fails the run, report or not */
    if ((status == 0) && (run.mismatches > 0))
        status = 1;

    xmlFreeDoc(catalog);
    xmlHashFree(run.known, NULL);
    xmlHashFree(run.docs, freeDoc);
    return(status);
}
