/*
 * 01-first-query.c: parse a document, compile and evaluate expressions,
 * walk the results and report errors
 *
 * The program of doc/tutorial/01-first-query.md. Run it with the
 * directory of the example documents:
 *
 *     01-first-query examples/data
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdio.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>
#include <libxpath3/xpath3.h>

#include "common.h"

/* A query, and the error code it raises, or NULL if it succeeds */
typedef struct {
    const char *text;
    const char *expectedError;
} Query;

static const Query queries[] = {
    { "count(//book)", NULL },
    /* [1] applies to each step: the first author of each book */
    { "//author[1]", NULL },
    { "(//author)[1]", NULL },
    /* Mixed content: the title has an <i> child between two texts */
    { "//book[1]/title", NULL },
    { "//book[1]/title/text()", NULL },
    { "string(//book[1]/title)", NULL },
    /* Text with an escaped & and a CDATA section make one text node */
    { "//book[@id = 'b2']/title/text()", NULL },
    { "//book[title = 'Tables & <Figures>']/string(@id)", NULL },
    { "string(//book[@id = 'b4']/title)", NULL },
    /* The indentation between elements is text too */
    { "count(//book/text())", NULL },
    { "//book[1]/text()", NULL },
    { "//comment()", NULL },
    { "//processing-instruction()", NULL },
    /* "c. 1850" is not a number (XPath 3.1, 3.7.2) */
    { "//book[@year > 1900]/string(@id)", "FORG0001" },
    { "//book[@year castable as xs:integer]"
      "[xs:integer(@year) > 1900]/string(@id)", NULL },
    /* A quoted number is a string, so this compares strings */
    { "//book[@year > '1990']/string(@id)", NULL },
    { "//book[not(@year)]/string(@id)", NULL },
    { "//book[@year = '']/string(@id)", NULL },
    /* Three authors where one string is expected (XPath 3.1, 3.1.5.2) */
    { "string-length(//book[3]/author)", "XPTY0004" },
    { "//book[3]/author/string-length()", NULL },
    { "string-join(//book[3]/author, '; ')", NULL },
    /* An empty element, a missing one */
    { "//book[not(author)]/string(@id)", NULL },
    { "//book[isbn]/string(@id)", NULL },
    { "//book[isbn = '']/string(@id)", NULL },
    { "//book[1]/isbn", NULL },
    { "string(//book[1]/isbn)", NULL },
    /* Nodes and atomic values in one result */
    { "(//book[1]/title, count(//book[3]/author), 'done')", NULL },
    /* ... but not as the result of a path (XPath 3.1, 3.3.1.1) */
    { "//book[1]/(title, 1)", "XPTY0018" },
    { "//book[@year = '1066']", NULL },
    { "//book[\n  @year >= ]", "XPST0003" },
    { "count(//book)", NULL }
};

/*
 * Print the kind of a node as an XPath kind test, and for elements and
 * attributes the name as written in the document.
 */
static void
printNodeKind(xmlNodePtr node) {
    const xmlChar *prefix = NULL;

    if ((node->ns != NULL) && (node->ns->prefix != NULL))
        prefix = node->ns->prefix;

    switch (node->type) {
        case XML_DOCUMENT_NODE:
            printf("document-node()");
            break;
        case XML_ELEMENT_NODE:
        case XML_ATTRIBUTE_NODE:
            if (node->type == XML_ELEMENT_NODE)
                printf("element(");
            else
                printf("attribute(");
            if (prefix != NULL)
                printf("%s:", (const char *) prefix);
            printf("%s)", (const char *) node->name);
            break;
        case XML_TEXT_NODE:
        case XML_CDATA_SECTION_NODE:
            printf("text()");
            break;
        case XML_COMMENT_NODE:
            printf("comment()");
            break;
        case XML_PI_NODE:
            printf("processing-instruction(%s)", (const char *) node->name);
            break;
        default:
            printf("node()");
            break;
    }
}

/*
 * Print one item of a result: a node with its kind, an atomic value
 * with its type, and either with its string value. Strings are quoted.
 */
static void
printItem(const xp3Sequence *result, size_t index) {
    xmlChar *value;
    int quote = 0;

    if (xp3SequenceItemKind(result, index) == XP3_ITEM_NODE) {
        xmlNodePtr node = xp3SequenceNode(result, index);

        printNodeKind(node);
        if (node->type == XML_DOCUMENT_NODE)
            return;
        quote = 1;
    } else {
        xp3AtomicType type;

        type = (xp3AtomicType) xp3SequenceAtomicType(result, index);
        printf("xs:%s", (const char *) xp3AtomicTypeName(type));
        if ((type == XP3_TYPE_UNTYPED_ATOMIC) || (type == XP3_TYPE_ANY_URI) ||
            (xp3AtomicTypeDerivesFrom(type, XP3_TYPE_STRING)))
            quote = 1;
    }

    /* The string value of a node, or an atomic value cast to a string */
    value = xp3SequenceStringValue(result, index);
    if (value == NULL)
        return;
    printf(" ");
    if (quote)
        printf("\"");
    exPrintText(value);
    if (quote)
        printf("\"");
    xmlFree(value);
}

/*
 * Report an error with its code and message, and for an error in the
 * expression itself, where it is.
 */
static void
printError(const xp3Error *error, int compiling) {
    printf("ERROR %s", (const char *) xp3ErrorCode(error));
    if (compiling) {
        printf(" at line %d, column %d when compiling",
               xp3ErrorLine(error), xp3ErrorColumn(error));
    }
    if (xp3ErrorMessage(error) != NULL) {
        printf(": ");
        exPrintText(xp3ErrorMessage(error));
    }
    printf("\n");
}

/*
 * Check that a query raised the error the tutorial expects, or none.
 * Returns 0 if so, 1 if not.
 */
static int
checkOutcome(const char *query, const xp3Error *error,
             const char *expectedError) {
    const char *code = NULL;

    if (error != NULL)
        code = (const char *) xp3ErrorCode(error);
    if ((code == NULL) && (expectedError == NULL))
        return(0);
    if ((code != NULL) && (expectedError != NULL) &&
        (strcmp(code, expectedError) == 0))
        return(0);
    fprintf(stderr, "unexpected outcome of %s\n", query);
    return(1);
}

/*
 * Compile a query, evaluate it with the dynamic context, and print the
 * result on one line. Returns 0 if the outcome was the expected one.
 */
static int
runQuery(const xp3StaticContext *sctx, xp3DynamicContext *dctx,
         const char *query, const char *expectedError) {
    xp3Error *error = NULL;
    xp3Expr *expr;
    xp3Sequence *result;
    size_t count;
    size_t i;
    int ret;

    exPrintText(BAD_CAST query);
    printf(" -> ");

    expr = xp3Compile(sctx, BAD_CAST query, &error);
    if (expr == NULL) {
        printError(error, 1);
        ret = checkOutcome(query, error, expectedError);
        xp3ErrorFree(error);
        return(ret);
    }

    result = xp3Evaluate(expr, dctx, &error);
    /* The result doesn't need the compiled expression */
    xp3FreeExpr(expr);
    if (result == NULL) {
        printError(error, 0);
        ret = checkOutcome(query, error, expectedError);
        xp3ErrorFree(error);
        return(ret);
    }

    count = xp3SequenceLength(result);
    if (count == 0)
        printf("()");
    for (i = 0; i < count; i++) {
        if (i > 0)
            printf(", ");
        printItem(result, i);
    }
    printf("\n");

    xp3FreeSequence(result);
    return(checkOutcome(query, NULL, expectedError));
}

/*
 * Show how libxml2 stores the text node that XPath returns for a title
 * made of text, a character reference and a CDATA section.
 */
static int
showTextRun(const xp3StaticContext *sctx, xp3DynamicContext *dctx) {
    xp3Expr *expr;
    xp3Sequence *result;
    xmlNodePtr node;
    xmlChar *value;

    expr = xp3Compile(sctx, BAD_CAST "//book[@id = 'b2']/title/text()",
                      NULL);
    if (expr == NULL)
        return(1);
    result = xp3Evaluate(expr, dctx, NULL);
    xp3FreeExpr(expr);
    if ((result == NULL) || (xp3SequenceLength(result) != 1)) {
        xp3FreeSequence(result);
        return(1);
    }

    value = xp3SequenceStringValue(result, 0);
    if (value == NULL) {
        xp3FreeSequence(result);
        return(1);
    }
    printf("string value of the XPath text node -> \"");
    exPrintText(value);
    printf("\"\n");
    xmlFree(value);

    /* The node stands for a run of adjacent libxml2 text nodes */
    printf("libxml2 nodes from there ->");
    node = xp3SequenceNode(result, 0);
    while ((node != NULL) && ((node->type == XML_TEXT_NODE) ||
                              (node->type == XML_CDATA_SECTION_NODE))) {
        if (node->type == XML_TEXT_NODE)
            printf(" text \"");
        else
            printf(" CDATA \"");
        exPrintText(node->content);
        printf("\"");
        node = node->next;
    }
    printf("\n");

    xp3FreeSequence(result);
    return(0);
}

int
main(int argc, char **argv) {
    xmlDocPtr doc = NULL;
    xmlDocPtr rawDoc = NULL;
    xp3StaticContext *sctx = NULL;
    xp3DynamicContext *dctx = NULL;
    size_t i;
    int failures = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: %s DATA-DIRECTORY\n", argv[0]);
        return(2);
    }

    /* Substitute entities, so that &press; reads as its text */
    doc = exReadDocument(argv[1], "library.xml", XML_PARSE_NOENT);
    sctx = xp3NewStaticContext();
    dctx = xp3NewDynamicContext();
    if ((doc == NULL) || (sctx == NULL) || (dctx == NULL)) {
        failures++;
        goto done;
    }

    /* Relative paths and "/" start from the context node */
    xp3SetContextNode(dctx, (xmlNodePtr) doc);
    for (i = 0; i < sizeof(queries) / sizeof(queries[0]); i++)
        failures += runQuery(sctx, dctx, queries[i].text,
                             queries[i].expectedError);
    failures += showTextRun(sctx, dctx);

    /* libxml2's default keeps &press; as an entity reference node */
    rawDoc = exReadDocument(argv[1], "library.xml", 0);
    if (rawDoc == NULL) {
        failures++;
        goto done;
    }
    xp3SetContextNode(dctx, (xmlNodePtr) rawDoc);
    printf("Without XML_PARSE_NOENT:\n");
    failures += runQuery(sctx, dctx, "string(//book[@id = 'b4']/title)",
                         NULL);

done:
    xp3FreeDynamicContext(dctx);
    xp3FreeStaticContext(sctx);
    xmlFreeDoc(rawDoc);
    xmlFreeDoc(doc);
    if (failures > 0)
        return(1);
    return(0);
}
