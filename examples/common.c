/*
 * common.c: helpers shared by the tutorial programs
 *
 * Every line the programs print has the form "label -> result": the
 * result is "()" for the empty sequence, otherwise its items separated
 * by ", ", each with its kind or type, or "ERROR" and the error code.
 * Control characters in strings are escaped as in C, \n for a newline
 * for example, so that each result stays on one line.
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
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>
#include <libxpath3/xpath3.h>

#include "common.h"

/* The namespace of the standard error codes, such as err:FORG0001 */
#define EX_ERR_NAMESPACE "http://www.w3.org/2005/xqt-errors"

/**
 * Parse a document of the data directory.
 *
 * @param dataDir   the directory, from the command line
 * @param fileName  the file in it
 * @param options   libxml2 parser options (xmlParserOption)
 * @returns the document, or NULL if it could not be read; libxml2 has
 *          reported why on stderr
 */
xmlDocPtr
exReadDocument(const char *dataDir, const char *fileName, int options) {
    size_t dirLength = strlen(dataDir);
    size_t nameLength = strlen(fileName);
    char *path;
    xmlDocPtr doc;

    path = malloc(dirLength + 1 + nameLength + 1);
    if (path == NULL)
        return(NULL);
    memcpy(path, dataDir, dirLength);
    path[dirLength] = '/';
    memcpy(path + dirLength + 1, fileName, nameLength + 1);

    doc = xmlReadFile(path, NULL, options);
    free(path);
    return(doc);
}

/**
 * Print a string, escaping control characters, so that a newline in
 * the data shows as \n and doesn't end the line. Backslashes are left
 * as they are, which keeps regular expressions readable.
 *
 * @param text  the string in UTF-8
 */
void
exPrintText(const xmlChar *text) {
    const xmlChar *cur;

    for (cur = text; *cur != 0; cur++) {
        if (*cur == '\n')
            fputs("\\n", stdout);
        else if (*cur == '\r')
            fputs("\\r", stdout);
        else if (*cur == '\t')
            fputs("\\t", stdout);
        else if (*cur < 0x20)
            printf("\\x%02X", (unsigned) *cur);
        else
            putchar(*cur);
    }
}

/*
 * Print the name of an element or attribute as it is written in the
 * document, with its prefix.
 */
static void
exPrintNodeName(xmlNodePtr node) {
    if ((node->ns != NULL) && (node->ns->prefix != NULL))
        printf("%s:", (const char *) node->ns->prefix);
    printf("%s", (const char *) node->name);
}

/*
 * Print the kind of a node as an XPath kind test. libxml2 has two node
 * types for text, XML_TEXT_NODE and XML_CDATA_SECTION_NODE, and XPath
 * has one: a CDATA section is text like any other.
 */
static void
exPrintNodeKind(xmlNodePtr node) {
    switch (node->type) {
        case XML_DOCUMENT_NODE:
        case XML_HTML_DOCUMENT_NODE:
            printf("document-node()");
            break;
        case XML_ELEMENT_NODE:
            printf("element(");
            exPrintNodeName(node);
            printf(")");
            break;
        case XML_ATTRIBUTE_NODE:
            printf("attribute(");
            exPrintNodeName(node);
            printf(")");
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
 * Print one item: a node with its kind and string value, an atomic
 * value with its type and its value cast to a string. String types are
 * quoted, so that the empty string and spaces are visible.
 */
static void
exPrintItem(const xp3Sequence *seq, size_t index) {
    xmlChar *value;
    int kind = xp3SequenceItemKind(seq, index);
    int quote = 1;

    if (kind == XP3_ITEM_NODE) {
        xmlNodePtr node = xp3SequenceNode(seq, index);

        exPrintNodeKind(node);
        if ((node->type == XML_DOCUMENT_NODE) ||
            (node->type == XML_HTML_DOCUMENT_NODE))
            return;
        printf(" ");
    } else if (kind == XP3_ITEM_ATOMIC) {
        xp3AtomicType type;

        type = (xp3AtomicType) xp3SequenceAtomicType(seq, index);
        printf("xs:%s ", (const char *) xp3AtomicTypeName(type));
        if ((type != XP3_TYPE_UNTYPED_ATOMIC) &&
            (type != XP3_TYPE_ANY_URI) &&
            (!xp3AtomicTypeDerivesFrom(type, XP3_TYPE_STRING)))
            quote = 0;
    } else {
        printf("(a function, map or array)");
        return;
    }

    value = xp3SequenceStringValue(seq, index);
    if (value == NULL) {
        printf("(out of memory)");
        return;
    }
    if (quote)
        printf("\"");
    exPrintText(value);
    if (quote)
        printf("\"");
    xmlFree(value);
}

/**
 * Print a sequence: "()" if it is empty, otherwise its items separated
 * by ", ".
 *
 * @param seq  the sequence
 */
void
exPrintSequence(const xp3Sequence *seq) {
    size_t length = xp3SequenceLength(seq);
    size_t i;

    if (length == 0) {
        printf("()");
        return;
    }
    for (i = 0; i < length; i++) {
        if (i > 0)
            printf(", ");
        exPrintItem(seq, i);
    }
}

/**
 * Print an error as "ERROR" and its code. The standard codes are in
 * the err namespace, which is left out; any other namespace is shown
 * as Q{uri}, together with the message, because such an error comes
 * from the application, which chose the message too. The messages of
 * libxpath3's own errors are left out, since they may be reworded from
 * one version to the next, while the codes stay.
 *
 * @param error  the error, may be NULL
 */
void
exPrintError(const xp3Error *error) {
    const xmlChar *uri;

    if (error == NULL) {
        printf("ERROR without an error object");
        return;
    }
    printf("ERROR ");
    uri = xp3ErrorNamespace(error);
    if ((uri != NULL) && (!xmlStrEqual(uri, BAD_CAST EX_ERR_NAMESPACE))) {
        printf("Q{%s}%s", (const char *) uri,
               (const char *) xp3ErrorCode(error));
        if (xp3ErrorMessage(error) != NULL) {
            printf(": ");
            exPrintText(xp3ErrorMessage(error));
        }
        return;
    }
    printf("%s", (const char *) xp3ErrorCode(error));
}

/*
 * Compare the outcome of a query with what the tutorial says, and
 * explain a difference on stderr, so that stdout stays comparable.
 * Returns 0 if they agree, 1 if not.
 */
static int
exCheckOutcome(const char *label, const xp3Error *error,
               const char *expectedError) {
    const char *code = NULL;

    if (error != NULL)
        code = (const char *) xp3ErrorCode(error);

    if ((expectedError == NULL) && (code == NULL))
        return(0);
    if ((expectedError != NULL) && (code != NULL) &&
        (strcmp(expectedError, code) == 0))
        return(0);

    if (expectedError == NULL)
        fprintf(stderr, "%s: expected a result, got %s\n", label, code);
    else if (code == NULL)
        fprintf(stderr, "%s: expected %s, got a result\n", label,
                expectedError);
    else
        fprintf(stderr, "%s: expected %s, got %s\n", label, expectedError,
                code);
    return(1);
}

/**
 * Evaluate a compiled expression and print one line with a label and
 * the result or error.
 *
 * @param expr           the compiled expression
 * @param dctx           the dynamic context
 * @param label          the label to print
 * @param expectedError  the error code the evaluation should raise, or
 *                       NULL if it should succeed
 * @returns 0 if the outcome is the expected one, 1 if not
 */
int
exEvaluate(const xp3Expr *expr, xp3DynamicContext *dctx, const char *label,
           const char *expectedError) {
    xp3Error *error = NULL;
    xp3Sequence *result;
    int ret;

    exPrintText(BAD_CAST label);
    printf(" -> ");
    result = xp3Evaluate(expr, dctx, &error);
    if (result == NULL)
        exPrintError(error);
    else
        exPrintSequence(result);
    printf("\n");

    ret = exCheckOutcome(label, error, expectedError);
    xp3ErrorFree(error);
    xp3FreeSequence(result);
    return(ret);
}

/**
 * Compile a query, evaluate it and print one line with its label and
 * its result or error. Compile errors say so, since they are raised
 * before anything is evaluated.
 *
 * @param sctx           the static context
 * @param dctx           the dynamic context
 * @param label          the label to print, or NULL to print the query
 * @param query          the query
 * @param expectedError  the error code the query should raise, or NULL
 *                       if it should succeed
 * @returns 0 if the outcome is the expected one, 1 if not
 */
int
exRun(const xp3StaticContext *sctx, xp3DynamicContext *dctx,
      const char *label, const char *query, const char *expectedError) {
    xp3Error *error = NULL;
    xp3Expr *expr;
    int ret;

    if (label == NULL)
        label = query;

    expr = xp3Compile(sctx, BAD_CAST query, &error);
    if (expr == NULL) {
        exPrintText(BAD_CAST label);
        printf(" -> ");
        exPrintError(error);
        printf(" when compiling\n");
        ret = exCheckOutcome(label, error, expectedError);
        xp3ErrorFree(error);
        return(ret);
    }

    ret = exEvaluate(expr, dctx, label, expectedError);
    xp3FreeExpr(expr);
    return(ret);
}
