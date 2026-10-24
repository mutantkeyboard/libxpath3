/*
 * testnodes.c: tests for XDM navigation over libxml2's tree
 *
 * Most tests use a tree built by hand, because libxml2's own functions
 * merge adjacent text nodes, and the point is to see how navigation
 * handles them: an empty text node, a text node followed by a CDATA
 * section, an entity reference between text, and an XInclude marker.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/entities.h>
#include <libxml/xmlmemory.h>

#include "private/xp3nodes.h"
#include "private/xp3value.h"
#include "testalloc.h"

static int failures = 0;

static void
check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

/*
 * The test tree
 *
 *   document
 *     DTD with the entity e
 *     comment "top"
 *     r a="1" p:b="2" xmlns:p="urn:p"
 *       text "", text "a", CDATA "b", &e;, text "c"     one text "abc"
 *       x id="x1"
 *         text "inner"
 *       comment "note"
 *       text "", XInclude start marker                no node
 *       PI go "now"
 *       y v="x&e;y"
 *         z
 *       text "tail"
 *     PI end
 */
typedef struct {
    xmlDocPtr doc;
    xmlNodePtr dtd;
    xmlNodePtr top;
    xmlNodePtr r;
    xmlNodePtr attrA;
    xmlNodePtr attrB;
    xmlNodePtr empty;           /* the empty text before "a" */
    xmlNodePtr textA;
    xmlNodePtr cdataB;
    xmlNodePtr entityRef;
    xmlNodePtr textC;
    xmlNodePtr x;
    xmlNodePtr attrId;
    xmlNodePtr inner;
    xmlNodePtr note;
    xmlNodePtr lonelyEmpty;     /* the empty text before the marker */
    xmlNodePtr marker;
    xmlNodePtr go;
    xmlNodePtr y;
    xmlNodePtr attrV;
    xmlNodePtr attrText;        /* the text "x" inside v */
    xmlNodePtr z;
    xmlNodePtr tail;
    xmlNodePtr end;
    xmlDocPtr wideDoc;          /* a second tree: one element with */
    xmlNodePtr wide;            /* XP3_WIDE_ATTRS attributes */
} xp3TestTree;

#define XP3_WIDE_ATTRS 40

/* Append a node to a parent without merging it with text before it,
 * as xmlAddChild() would. */
static xmlNodePtr
appendRaw(xmlNodePtr parent, xmlNodePtr node) {
    node->parent = parent;
    node->doc = parent->doc;
    node->prev = parent->last;
    node->next = NULL;
    if (parent->last != NULL)
        parent->last->next = node;
    else
        parent->children = node;
    parent->last = node;
    return(node);
}

static void
buildTree(xp3TestTree *tree) {
    xmlDocPtr doc;
    xmlNsPtr ns;
    xmlNodePtr root;
    int i;

    memset(tree, 0, sizeof(*tree));
    doc = xmlNewDoc(BAD_CAST "1.0");
    tree->doc = doc;
    root = (xmlNodePtr) doc;
    tree->dtd = (xmlNodePtr) xmlCreateIntSubset(doc, BAD_CAST "r", NULL,
                                                NULL);
    xmlAddDocEntity(doc, BAD_CAST "e", XML_INTERNAL_GENERAL_ENTITY, NULL,
                    NULL, BAD_CAST "E");
    tree->top = appendRaw(root, xmlNewDocComment(doc, BAD_CAST "top"));

    tree->r = appendRaw(root, xmlNewDocNode(doc, NULL, BAD_CAST "r", NULL));
    ns = xmlNewNs(tree->r, BAD_CAST "urn:p", BAD_CAST "p");
    tree->attrA = (xmlNodePtr) xmlNewProp(tree->r, BAD_CAST "a",
                                          BAD_CAST "1");
    tree->attrB = (xmlNodePtr) xmlNewNsProp(tree->r, ns, BAD_CAST "b",
                                            BAD_CAST "2");
    tree->empty = appendRaw(tree->r, xmlNewDocText(doc, BAD_CAST ""));
    tree->textA = appendRaw(tree->r, xmlNewDocText(doc, BAD_CAST "a"));
    tree->cdataB = appendRaw(tree->r,
                             xmlNewCDataBlock(doc, BAD_CAST "b", 1));
    tree->entityRef = appendRaw(tree->r,
                                xmlNewReference(doc, BAD_CAST "&e;"));
    tree->textC = appendRaw(tree->r, xmlNewDocText(doc, BAD_CAST "c"));

    tree->x = appendRaw(tree->r, xmlNewDocNode(doc, NULL, BAD_CAST "x",
                                               NULL));
    tree->attrId = (xmlNodePtr) xmlNewProp(tree->x, BAD_CAST "id",
                                           BAD_CAST "x1");
    tree->inner = appendRaw(tree->x, xmlNewDocText(doc, BAD_CAST "inner"));
    tree->note = appendRaw(tree->r, xmlNewDocComment(doc, BAD_CAST "note"));
    tree->lonelyEmpty = appendRaw(tree->r,
                                  xmlNewDocText(doc, BAD_CAST ""));
    tree->marker = appendRaw(tree->r, xmlNewDocNode(doc, NULL,
                                                    BAD_CAST "include",
                                                    NULL));
    tree->marker->type = XML_XINCLUDE_START;
    tree->go = appendRaw(tree->r, xmlNewDocPI(doc, BAD_CAST "go",
                                              BAD_CAST "now"));

    tree->y = appendRaw(tree->r, xmlNewDocNode(doc, NULL, BAD_CAST "y",
                                               NULL));
    tree->attrV = (xmlNodePtr) xmlNewProp(tree->y, BAD_CAST "v", NULL);
    tree->attrText = appendRaw(tree->attrV,
                               xmlNewDocText(doc, BAD_CAST "x"));
    appendRaw(tree->attrV, xmlNewReference(doc, BAD_CAST "&e;"));
    appendRaw(tree->attrV, xmlNewDocText(doc, BAD_CAST "y"));
    tree->z = appendRaw(tree->y, xmlNewDocNode(doc, NULL, BAD_CAST "z",
                                               NULL));
    tree->tail = appendRaw(tree->r, xmlNewDocText(doc, BAD_CAST "tail"));
    tree->end = appendRaw(root, xmlNewDocPI(doc, BAD_CAST "end", NULL));

    tree->wideDoc = xmlNewDoc(BAD_CAST "1.0");
    tree->wide = xmlNewDocNode(tree->wideDoc, NULL, BAD_CAST "w", NULL);
    xmlDocSetRootElement(tree->wideDoc, tree->wide);
    for (i = 0; i < XP3_WIDE_ATTRS; i++) {
        char name[16];

        snprintf(name, sizeof(name), "a%d", i);
        xmlNewProp(tree->wide, BAD_CAST name, BAD_CAST "v");
    }
}

/*
 * Describing nodes: "d" for a document, "e:name", "a:name", "t:text",
 * "c:text" and "p:target".
 */
static void
describeNode(xmlNodePtr node, char *out, size_t size) {
    xmlChar *value = NULL;

    switch (xp3NodeGetKind(node)) {
        case XP3_NODE_DOCUMENT:
            snprintf(out, size, "d");
            break;
        case XP3_NODE_ELEMENT:
            snprintf(out, size, "e:%s", (const char *) node->name);
            break;
        case XP3_NODE_ATTRIBUTE:
            snprintf(out, size, "a:%s", (const char *) node->name);
            break;
        case XP3_NODE_TEXT:
        case XP3_NODE_COMMENT:
            if (xp3NodeStringValue(node, &value, NULL) != XP3_ERR_OK) {
                snprintf(out, size, "?");
                break;
            }
            snprintf(out, size, "%c:%s",
                     (xp3NodeGetKind(node) == XP3_NODE_TEXT) ? 't' : 'c',
                     (const char *) value);
            xmlFree(value);
            break;
        case XP3_NODE_PI:
            snprintf(out, size, "p:%s", (const char *) node->name);
            break;
        default:
            snprintf(out, size, "none");
            break;
    }
}

/* The nodes of an axis, described and separated by spaces */
static const char *
axisString(xp3Axis axis, xmlNodePtr context) {
    static char out[512];
    xmlNodePtr node = NULL;
    size_t used = 0;
    int count = 0;

    out[0] = 0;
    while ((node = xp3AxisNext(axis, context, node)) != NULL) {
        char item[64];

        describeNode(node, item, sizeof(item));
        used += snprintf(out + used, sizeof(out) - used, "%s%s",
                         (used > 0) ? " " : "", item);
        if ((used >= sizeof(out)) || (++count > 50))
            break;
    }
    return(out);
}

static void
checkAxis(xp3Axis axis, xmlNodePtr context, const char *expected,
          const char *what) {
    const char *actual = axisString(axis, context);

    if (strcmp(actual, expected) != 0) {
        fprintf(stderr, "FAIL: %s\n  expected: %s\n  actual:   %s\n",
                what, expected, actual);
        failures++;
    }
}

static void
checkStringValue(xmlNodePtr node, const char *expected, const char *what) {
    xmlChar *value = NULL;
    size_t length = 99;
    xp3ErrCode err;

    err = xp3NodeStringValue(node, &value, &length);
    if ((err != XP3_ERR_OK) || (value == NULL) ||
        (strcmp((const char *) value, expected) != 0) ||
        (length != strlen(expected))) {
        fprintf(stderr, "FAIL: %s\n  expected: %s\n  actual:   %s\n",
                what, expected, (value != NULL) ? (char *) value : "NULL");
        failures++;
    }
    xmlFree(value);
}

/*
 * Kinds and canonical nodes
 */

static void
testKinds(const xp3TestTree *tree) {
    check(xp3NodeGetKind((xmlNodePtr) tree->doc) == XP3_NODE_DOCUMENT,
          "kind of a document");
    check(xp3NodeGetKind(tree->r) == XP3_NODE_ELEMENT, "kind of an element");
    check(xp3NodeGetKind(tree->attrB) == XP3_NODE_ATTRIBUTE,
          "kind of an attribute");
    check(xp3NodeGetKind(tree->textA) == XP3_NODE_TEXT, "kind of text");
    check(xp3NodeGetKind(tree->cdataB) == XP3_NODE_TEXT, "kind of CDATA");
    check(xp3NodeGetKind(tree->note) == XP3_NODE_COMMENT,
          "kind of a comment");
    check(xp3NodeGetKind(tree->go) == XP3_NODE_PI, "kind of a PI");
    check(xp3NodeGetKind(tree->dtd) == XP3_NODE_NONE, "a DTD is no node");
    check(xp3NodeGetKind(tree->entityRef) == XP3_NODE_NONE,
          "an entity reference is no node");
    check(xp3NodeGetKind(tree->marker) == XP3_NODE_NONE,
          "an XInclude marker is no node");
    check(xp3NodeGetKind(tree->attrText) == XP3_NODE_NONE,
          "text inside an attribute is no node");
    check(xp3NodeGetKind((xmlNodePtr) tree->r->nsDef) == XP3_NODE_NONE,
          "a namespace declaration is no node");
    check(xp3NodeGetKind(NULL) == XP3_NODE_NONE, "NULL is no node");

    check(xp3NodeCanonical(tree->textA) == tree->textA,
          "the first text with content represents its run");
    check(xp3NodeCanonical(tree->empty) == tree->textA,
          "empty text at the start of a run");
    check(xp3NodeCanonical(tree->cdataB) == tree->textA, "CDATA in a run");
    check(xp3NodeCanonical(tree->textC) == tree->textA,
          "text after an entity reference is in the same run");
    check(xp3NodeCanonical(tree->lonelyEmpty) == NULL,
          "a run without content is no node");
    check(xp3NodeCanonical(tree->entityRef) == NULL,
          "an entity reference has no canonical node");
    check(xp3NodeCanonical(tree->attrText) == NULL,
          "text inside an attribute has no canonical node");
    check(xp3NodeCanonical(tree->x) == tree->x,
          "an element represents itself");
    check(xp3NodeCanonical(tree->attrA) == tree->attrA,
          "an attribute represents itself");
    check(xp3NodeParent(tree->attrText) == NULL,
          "text inside an attribute has no XDM parent");
    check(xp3NodeParent(tree->entityRef) == NULL,
          "an entity reference has no XDM parent");
    check(xp3NodeParent((xmlNodePtr) tree->r->nsDef) == NULL,
          "a namespace declaration has no XDM parent");
    check(xp3NodeRoot(tree->entityRef) == tree->entityRef,
          "and is its own root");
}

/*
 * Axes
 */

static void
testAxes(const xp3TestTree *tree) {
    xmlNodePtr doc = (xmlNodePtr) tree->doc;

    checkAxis(XP3_AXIS_CHILD, doc, "c:top e:r p:end", "child of document");
    checkAxis(XP3_AXIS_CHILD, tree->r,
              "t:abc e:x c:note p:go e:y t:tail", "child of r");
    checkAxis(XP3_AXIS_CHILD, tree->attrA, "", "child of an attribute");
    checkAxis(XP3_AXIS_CHILD, tree->textA, "", "child of text");
    checkAxis(XP3_AXIS_DESCENDANT, tree->r,
              "t:abc e:x t:inner c:note p:go e:y e:z t:tail",
              "descendant of r");
    checkAxis(XP3_AXIS_DESCENDANT, doc,
              "c:top e:r t:abc e:x t:inner c:note p:go e:y e:z t:tail p:end",
              "descendant of document");
    checkAxis(XP3_AXIS_DESCENDANT, tree->z, "", "descendant of a leaf");
    checkAxis(XP3_AXIS_DESCENDANT_OR_SELF, tree->x, "e:x t:inner",
              "descendant-or-self of x");
    checkAxis(XP3_AXIS_DESCENDANT_OR_SELF, tree->attrId, "a:id",
              "descendant-or-self of an attribute");
    checkAxis(XP3_AXIS_ATTRIBUTE, tree->r, "a:a a:b", "attribute of r");
    checkAxis(XP3_AXIS_ATTRIBUTE, tree->z, "", "attribute of z");
    checkAxis(XP3_AXIS_ATTRIBUTE, tree->attrA, "",
              "attribute of an attribute");
    checkAxis(XP3_AXIS_SELF, tree->textA, "t:abc", "self of text");
    checkAxis(XP3_AXIS_PARENT, tree->textA, "e:r", "parent of text");
    checkAxis(XP3_AXIS_PARENT, tree->attrA, "e:r", "parent of an attribute");
    checkAxis(XP3_AXIS_PARENT, tree->r, "d", "parent of r");
    checkAxis(XP3_AXIS_PARENT, doc, "", "parent of the document");
    checkAxis(XP3_AXIS_ANCESTOR, tree->z, "e:y e:r d", "ancestor of z");
    checkAxis(XP3_AXIS_ANCESTOR, tree->attrId, "e:x e:r d",
              "ancestor of an attribute");
    checkAxis(XP3_AXIS_ANCESTOR_OR_SELF, tree->z, "e:z e:y e:r d",
              "ancestor-or-self of z");
    checkAxis(XP3_AXIS_FOLLOWING_SIBLING, tree->x,
              "c:note p:go e:y t:tail", "following-sibling of x");
    checkAxis(XP3_AXIS_FOLLOWING_SIBLING, tree->textA,
              "e:x c:note p:go e:y t:tail", "following-sibling of text");
    checkAxis(XP3_AXIS_FOLLOWING_SIBLING, tree->tail, "",
              "following-sibling of the last child");
    checkAxis(XP3_AXIS_FOLLOWING_SIBLING, tree->attrA, "",
              "following-sibling of an attribute");
    checkAxis(XP3_AXIS_FOLLOWING_SIBLING, doc, "",
              "following-sibling of the document");
    checkAxis(XP3_AXIS_PRECEDING_SIBLING, tree->y,
              "p:go c:note e:x t:abc", "preceding-sibling of y");
    checkAxis(XP3_AXIS_PRECEDING_SIBLING, tree->go, "c:note e:x t:abc",
              "preceding-sibling skips a run without content");
    checkAxis(XP3_AXIS_PRECEDING_SIBLING, tree->textA, "",
              "preceding-sibling of the first child");
    checkAxis(XP3_AXIS_PRECEDING_SIBLING, tree->attrB, "",
              "preceding-sibling of an attribute");
    checkAxis(XP3_AXIS_FOLLOWING, tree->x,
              "c:note p:go e:y e:z t:tail p:end", "following of x");
    checkAxis(XP3_AXIS_FOLLOWING, tree->attrA,
              "t:abc e:x t:inner c:note p:go e:y e:z t:tail p:end",
              "following of an attribute starts with its element's "
              "children");
    checkAxis(XP3_AXIS_FOLLOWING, tree->attrId,
              "t:inner c:note p:go e:y e:z t:tail p:end",
              "following of x's attribute");
    checkAxis(XP3_AXIS_FOLLOWING, tree->end, "", "following of the end");
    checkAxis(XP3_AXIS_FOLLOWING, doc, "", "following of the document");
    checkAxis(XP3_AXIS_PRECEDING, tree->z,
              "p:go c:note t:inner e:x t:abc c:top", "preceding of z");
    checkAxis(XP3_AXIS_PRECEDING, tree->attrId, "t:abc c:top",
              "preceding of an attribute is its element's");
    checkAxis(XP3_AXIS_PRECEDING, tree->end,
              "t:tail e:z e:y p:go c:note t:inner e:x t:abc e:r c:top",
              "preceding of the last node");
    checkAxis(XP3_AXIS_PRECEDING, doc, "", "preceding of the document");
    checkAxis(XP3_AXIS_NAMESPACE, tree->r, "", "namespace axis is empty");
    checkAxis(XP3_AXIS_CHILD, NULL, "", "no context node");
}

static void
testParentless(void) {
    xmlNodePtr top = xmlNewNode(NULL, BAD_CAST "top");
    xmlNodePtr child = xmlNewChild(top, NULL, BAD_CAST "child", NULL);
    xmlNodePtr text = xmlNewText(BAD_CAST "loose");
    xmlNodePtr emptyText = xmlNewText(BAD_CAST "");
    xmlAttrPtr attr = xmlNewProp(NULL, BAD_CAST "loose", BAD_CAST "v");
    xmlNodePtr sibling = xmlNewNode(NULL, BAD_CAST "sibling");

    /* Siblings of a root don't count, whatever next says */
    top->next = sibling;
    check(xp3NodeRoot(child) == top, "root of a tree without document");
    checkAxis(XP3_AXIS_ANCESTOR_OR_SELF, child, "e:child e:top",
              "ancestors up to a parentless root");
    checkAxis(XP3_AXIS_FOLLOWING_SIBLING, top, "",
              "a root has no siblings");
    checkAxis(XP3_AXIS_FOLLOWING, child, "", "following ends at the root");
    check(xp3NodeCanonical(text) == text, "a parentless text node");
    check(xp3NodeCanonical(emptyText) == NULL,
          "a parentless empty text node is no node");
    checkStringValue(text, "loose", "string value of parentless text");
    check(xp3NodeRoot((xmlNodePtr) attr) == (xmlNodePtr) attr,
          "a parentless attribute is a root");
    checkAxis(XP3_AXIS_FOLLOWING, (xmlNodePtr) attr, "",
              "following of a parentless attribute");
    checkAxis(XP3_AXIS_PRECEDING, (xmlNodePtr) attr, "",
              "preceding of a parentless attribute");
    check(xp3NodeRoot(NULL) == NULL, "root of NULL");
    check(xp3NodeParent(NULL) == NULL, "parent of NULL");
    top->next = NULL;

    xmlFreeNode(sibling);
    xmlFreeProp(attr);
    xmlFreeNode(emptyText);
    xmlFreeNode(text);
    xmlFreeNode(top);
}

/*
 * Names and values
 */

static void
testNames(const xp3TestTree *tree) {
    check(xmlStrEqual(xp3NodeLocalName(tree->x), BAD_CAST "x"),
          "local name of an element");
    check(xp3NodeNamespaceUri(tree->x) == NULL,
          "an element in no namespace");
    check(xp3NodePrefix(tree->x) == NULL, "an element without prefix");
    check(xmlStrEqual(xp3NodeLocalName(tree->attrB), BAD_CAST "b"),
          "local name of an attribute");
    check(xmlStrEqual(xp3NodeNamespaceUri(tree->attrB), BAD_CAST "urn:p"),
          "namespace of an attribute");
    check(xmlStrEqual(xp3NodePrefix(tree->attrB), BAD_CAST "p"),
          "prefix of an attribute");
    check(xmlStrEqual(xp3NodeLocalName(tree->go), BAD_CAST "go"),
          "the name of a PI is its target");
    check(xp3NodeNamespaceUri(tree->go) == NULL, "a PI has no namespace");
    check(xp3NodeLocalName(tree->textA) == NULL, "text has no name");
    check(xp3NodeLocalName((xmlNodePtr) tree->doc) == NULL,
          "a document has no name");
    check(xp3NodeLocalName(NULL) == NULL, "NULL has no name");
    check(xp3NodeNamespaceUri(NULL) == NULL, "NULL has no namespace");
}

static void
checkTypedValue(xmlNodePtr node, xp3TypeId type, const char *expected,
                const char *what) {
    xp3Atomic *value = NULL;

    if ((xp3NodeTypedValue(node, &value) != XP3_ERR_OK) || (value == NULL)) {
        check(0, what);
        return;
    }
    check((value->type == type) &&
          xmlStrEqual(value->u.str, BAD_CAST expected), what);
    xp3AtomicUnref(value);
}

static void
testValues(const xp3TestTree *tree) {
    checkStringValue((xmlNodePtr) tree->doc, "abcinnertail",
                     "string value of the document");
    checkStringValue(tree->r, "abcinnertail",
                     "string value of r: text only, entities skipped");
    checkStringValue(tree->x, "inner", "string value of x");
    checkStringValue(tree->z, "", "string value of an empty element");
    checkStringValue(tree->textA, "abc", "string value of a text run");
    checkStringValue(tree->textC, "abc",
                     "string value of a later node of the run");
    checkStringValue(tree->empty, "abc",
                     "string value of an empty node of the run");
    checkStringValue(tree->lonelyEmpty, "", "string value of an empty run");
    checkStringValue(tree->attrB, "2", "string value of an attribute");
    checkStringValue(tree->attrV, "xy",
                     "entity references in attributes are skipped");
    checkStringValue(tree->note, "note", "string value of a comment");
    checkStringValue(tree->go, "now", "string value of a PI");
    checkStringValue(tree->end, "", "string value of an empty PI");
    checkStringValue(tree->entityRef, "", "string value of no node");

    checkTypedValue(tree->x, XP3_TYPE_UNTYPED_ATOMIC, "inner",
                    "typed value of an element");
    checkTypedValue(tree->attrA, XP3_TYPE_UNTYPED_ATOMIC, "1",
                    "typed value of an attribute");
    checkTypedValue(tree->textA, XP3_TYPE_UNTYPED_ATOMIC, "abc",
                    "typed value of text");
    checkTypedValue((xmlNodePtr) tree->doc, XP3_TYPE_UNTYPED_ATOMIC,
                    "abcinnertail", "typed value of the document");
    checkTypedValue(tree->note, XP3_TYPE_STRING, "note",
                    "typed value of a comment");
    checkTypedValue(tree->go, XP3_TYPE_STRING, "now",
                    "typed value of a PI");
}

/*
 * Parsed documents: what libxml2's parser produces
 */

static xmlNodePtr
parseRoot(const char *xml, int options, xmlDocPtr *doc) {
    *doc = xmlReadMemory(xml, (int) strlen(xml), "test.xml", NULL, options);
    check(*doc != NULL, xml);
    return((*doc != NULL) ? xmlDocGetRootElement(*doc) : NULL);
}

static void
testParsed(void) {
    xmlDocPtr doc;
    xmlNodePtr root;

    root = parseRoot("<r>a<![CDATA[b]]>c<x/></r>", 0, &doc);
    if (root != NULL) {
        checkAxis(XP3_AXIS_CHILD, root, "t:abc e:x",
                  "parsed text and CDATA are one text node");
        checkAxis(XP3_AXIS_PRECEDING_SIBLING, root->last, "t:abc",
                  "preceding-sibling of x is the whole run");
    }
    xmlFreeDoc(doc);

    root = parseRoot("<!DOCTYPE r [<!ENTITY e 'E'>]><r>a&e;b</r>", 0, &doc);
    if (root != NULL) {
        checkAxis(XP3_AXIS_CHILD, root, "t:ab",
                  "without XML_PARSE_NOENT, entity content is skipped");
        checkStringValue(root, "ab", "string value without NOENT");
        checkAxis(XP3_AXIS_CHILD, (xmlNodePtr) doc, "e:r",
                  "the DTD is no child of the document");
    }
    xmlFreeDoc(doc);

    root = parseRoot("<!DOCTYPE r [<!ENTITY e 'E'>]><r>a&e;b</r>",
                     XML_PARSE_NOENT, &doc);
    if (root != NULL) {
        checkAxis(XP3_AXIS_CHILD, root, "t:aEb",
                  "with XML_PARSE_NOENT, entity content is text");
        checkStringValue(root, "aEb", "string value with NOENT");
    }
    xmlFreeDoc(doc);
}

/*
 * Document order
 */

/* All nodes of the tree in document order, as the test expects them */
static const char *const expectedOrder =
    "d c:top e:r a:a a:b t:abc e:x a:id t:inner c:note p:go e:y a:v e:z "
    "t:tail p:end";

static size_t
collectNodes(const xp3TestTree *tree, xp3Item *items, size_t size) {
    xmlNodePtr root = (xmlNodePtr) tree->doc;
    xmlNodePtr node = NULL;
    size_t count = 0;

    while (((node = xp3AxisNext(XP3_AXIS_DESCENDANT_OR_SELF, root,
                                node)) != NULL) && (count < size)) {
        xmlNodePtr attr = NULL;

        items[count++] = xp3ItemFromNode(node);
        while (((attr = xp3AxisNext(XP3_AXIS_ATTRIBUTE, node, attr)) !=
                NULL) && (count < size))
            items[count++] = xp3ItemFromNode(attr);
    }
    return(count);
}

static const char *
itemsString(const xp3Item *items, size_t count) {
    static char out[512];
    size_t used = 0;
    size_t i;

    out[0] = 0;
    for (i = 0; (i < count) && (used < sizeof(out)); i++) {
        char item[64];

        describeNode(items[i].u.node, item, sizeof(item));
        used += snprintf(out + used, sizeof(out) - used, "%s%s",
                         (i > 0) ? " " : "", item);
    }
    return(out);
}

static void
testOrder(const xp3TestTree *tree) {
    xp3DocOrder *order = xp3DocOrderNew();
    xp3Item items[64];
    xp3Item reversed[64];
    size_t count;
    size_t total;
    size_t i;
    int result = 99;

    if (order == NULL) {
        check(0, "document order table created");
        return;
    }
    count = collectNodes(tree, items, 32);
    check(strcmp(itemsString(items, count), expectedOrder) == 0,
          "every node of the tree collected");

    /* Reverse, then add every node once more */
    for (i = 0; i < count; i++)
        reversed[i] = items[count - 1 - i];
    for (i = 0; i < count; i++)
        reversed[count + i] = items[(i * 7) % count];
    total = count * 2;
    check(xp3DocOrderSort(order, reversed, &total) == XP3_ERR_OK,
          "sort succeeds");
    check(total == count, "duplicates removed");
    check(strcmp(itemsString(reversed, total), expectedOrder) == 0,
          "sorted into document order");

    check(xp3DocOrderCompare(order, tree->r, tree->attrA, &result) ==
          XP3_ERR_OK && result < 0, "an element precedes its attributes");
    check(xp3DocOrderCompare(order, tree->attrB, tree->textA, &result) ==
          XP3_ERR_OK && result < 0, "attributes precede children");
    check(xp3DocOrderCompare(order, tree->attrB, tree->attrA, &result) ==
          XP3_ERR_OK && result > 0, "attributes in a stable order");
    check(xp3DocOrderCompare(order, tree->x, tree->x, &result) ==
          XP3_ERR_OK && result == 0, "a node equals itself");
    check(xp3DocOrderCompare(order, tree->cdataB, tree->textA, &result) ==
          XP3_ERR_OK && result == 0,
          "every node of a run is the same XDM node");
    check(xp3DocOrderCompare(order, tree->entityRef, tree->x, &result) ==
          XP3_ERR_XPTY0004, "an entity reference can't be ordered");
    check(xp3DocOrderCompare(order, tree->lonelyEmpty, tree->x, &result) ==
          XP3_ERR_XPTY0004, "an empty run can't be ordered");
    check(xp3DocOrderCompare(order, tree->x, tree->entityRef, &result) ==
          XP3_ERR_XPTY0004, "nor can a second node XDM doesn't have");

    items[0] = xp3ItemFromNode(tree->cdataB);
    items[1] = xp3ItemFromNode(tree->textA);
    items[2] = xp3ItemFromNode(tree->empty);
    count = 3;
    check(xp3DocOrderSort(order, items, &count) == XP3_ERR_OK &&
          count == 1 && items[0].u.node == tree->textA,
          "nodes of one run sort into one, the canonical one");
    items[0] = xp3ItemFromNode(tree->x);
    items[1] = xp3ItemFromNode(tree->textC);
    count = 2;
    check(xp3DocOrderSort(order, items, &count) == XP3_ERR_OK &&
          count == 2 && items[0].u.node == tree->textA &&
          items[1].u.node == tree->x, "a run's later node sorts as the run");
    items[0] = xp3ItemFromNode(tree->textA);
    items[1] = xp3ItemFromNode(tree->cdataB);
    items[2] = xp3ItemFromNode(tree->x);
    count = 3;
    check(xp3DocOrderSort(order, items, &count) == XP3_ERR_OK &&
          count == 2 && items[1].u.node == tree->x,
          "a run's nodes in order are duplicates");
    items[0] = xp3ItemFromNode(tree->cdataB);
    count = 1;
    check(xp3DocOrderSort(order, items, &count) == XP3_ERR_OK &&
          count == 1 && items[0].u.node == tree->textA,
          "a single node is canonicalized");
    items[0] = xp3ItemFromNode(tree->entityRef);
    count = 1;
    check(xp3DocOrderSort(order, items, &count) == XP3_ERR_XPTY0004,
          "a single node XDM doesn't have is rejected");
    items[0] = xp3ItemFromNode(tree->x);
    items[1] = xp3ItemFromNode(tree->textC);
    items[2] = xp3ItemFromNode(tree->x);
    count = 3;
    check(xp3DocOrderSort(order, items, &count) == XP3_ERR_OK &&
          count == 2 && items[0].u.node == tree->textA &&
          items[1].u.node == tree->x, "duplicates around a run node");
    check(xp3DocOrderCompare(order, tree->textC, tree->x, &result) ==
          XP3_ERR_OK && result < 0, "comparing a later node of a run");
    check(xp3DocOrderCompare(order, tree->textC, tree->cdataB, &result) ==
          XP3_ERR_OK && result == 0, "two later nodes of a run are equal");

    items[0] = xp3ItemFromNode(tree->x);
    items[1] = xp3ItemFromNode(tree->r);
    items[2] = xp3ItemFromNode(tree->attrText);
    count = 3;
    check(xp3DocOrderSort(order, items, &count) == XP3_ERR_XPTY0004 &&
          count == 3 && items[0].u.node == tree->x,
          "a node XDM doesn't have can't be sorted, items unchanged");
    count = 0;
    check(xp3DocOrderSort(order, items, &count) == XP3_ERR_OK &&
          count == 0, "an empty sequence sorts");
    xp3DocOrderFree(order);
    xp3DocOrderFree(NULL);
}

static void
testOrderAtomic(const xp3TestTree *tree) {
    xp3DocOrder *order = xp3DocOrderNew();
    xp3Atomic *atomic = xp3AtomicNewInteger(1);
    xp3Item items[2];
    size_t count = 2;

    if ((order == NULL) || (atomic == NULL)) {
        check(0, "table and atomic value created");
        xp3DocOrderFree(order);
        xp3AtomicUnref(atomic);
        return;
    }
    items[0] = xp3ItemFromNode(tree->x);
    items[1] = xp3ItemFromAtomic(atomic);
    check(xp3DocOrderSort(order, items, &count) == XP3_ERR_XPTY0004,
          "an atomic value can't be sorted");
    count = 1;
    check(xp3DocOrderSort(order, items + 1, &count) == XP3_ERR_XPTY0004,
          "not even alone");
    count = 1;
    check(xp3DocOrderSort(order, items, &count) == XP3_ERR_OK &&
          count == 1, "a single node sorts");
    xp3AtomicUnref(atomic);
    xp3DocOrderFree(order);
}

static void
testOrderTrees(const xp3TestTree *tree) {
    xp3DocOrder *order = xp3DocOrderNew();
    xmlDocPtr other = NULL;
    xmlNodePtr otherRoot = parseRoot("<o><p/></o>", 0, &other);
    xmlNodePtr loose = xmlNewNode(NULL, BAD_CAST "loose");
    xmlNodePtr looseChild = xmlNewChild(loose, NULL, BAD_CAST "c", NULL);
    int result = 99;

    if ((order == NULL) || (otherRoot == NULL)) {
        check(0, "second tree created");
    } else {
        /* The tree seen first comes first, and stays first */
        check(xp3DocOrderCompare(order, otherRoot->children, tree->r,
                                 &result) == XP3_ERR_OK && result < 0,
              "the first tree seen comes first");
        check(xp3DocOrderCompare(order, tree->z, otherRoot, &result) ==
              XP3_ERR_OK && result > 0, "and stays first");
        check(xp3DocOrderCompare(order, looseChild, loose, &result) ==
              XP3_ERR_OK && result > 0, "a tree without document");
        check(xp3DocOrderCompare(order, loose, tree->r, &result) ==
              XP3_ERR_OK && result > 0, "comes after the trees before it");
    }
    xmlFreeNode(loose);
    xmlFreeDoc(other);
    xp3DocOrderFree(order);
}

/* Trees that changed after they were indexed */
static void
testOrderChanged(void) {
    xp3DocOrder *order = xp3DocOrderNew();
    xmlDocPtr doc = NULL;
    xmlNodePtr root = parseRoot("<r><a/></r>", 0, &doc);
    xmlNodePtr added;
    xmlNodePtr unlisted;
    int result = 99;

    if ((order == NULL) || (root == NULL)) {
        check(0, "changed tree set up");
        goto done;
    }
    check(xp3DocOrderCompare(order, root->children, root, &result) ==
          XP3_ERR_OK && result > 0, "tree indexed");
    added = xmlNewChild(root, NULL, BAD_CAST "b", NULL);
    check(xp3DocOrderCompare(order, added, root, &result) ==
          XP3_ERR_XPTY0004, "a node added after indexing isn't found");
    xp3DocOrderFree(order);

    /* A node whose parent doesn't list it */
    order = xp3DocOrderNew();
    unlisted = xmlNewDocNode(doc, NULL, BAD_CAST "u", NULL);
    if ((order == NULL) || (unlisted == NULL)) {
        check(0, "unlisted node set up");
        xmlFreeNode(unlisted);
        goto done;
    }
    unlisted->parent = root;
    check(xp3DocOrderCompare(order, unlisted, root, &result) ==
          XP3_ERR_XPTY0004, "a node its parent doesn't list isn't found");
    unlisted->parent = NULL;
    xmlFreeNode(unlisted);

done:
    xp3DocOrderFree(order);
    xmlFreeDoc(doc);
}

/* A wide tree, sorted from reverse order */
static void
testOrderLarge(void) {
    enum { CHILDREN = 20000 };
    xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
    xmlNodePtr root = xmlNewDocNode(doc, NULL, BAD_CAST "r", NULL);
    xmlNodePtr *nodes = xmlMalloc(CHILDREN * sizeof(xmlNodePtr));
    xp3Item *items = xmlMalloc(CHILDREN * 2 * sizeof(xp3Item));
    xp3DocOrder *order = xp3DocOrderNew();
    size_t count = CHILDREN * 2;
    size_t i;
    int sorted = 1;

    if ((nodes == NULL) || (items == NULL) || (order == NULL)) {
        check(0, "large test set up");
        goto done;
    }
    xmlDocSetRootElement(doc, root);
    for (i = 0; i < CHILDREN; i++)
        nodes[i] = xmlNewChild(root, NULL, BAD_CAST "c", NULL);
    for (i = 0; i < CHILDREN; i++) {
        items[i] = xp3ItemFromNode(nodes[CHILDREN - 1 - i]);
        items[CHILDREN + i] = xp3ItemFromNode(nodes[i]);
    }
    check(xp3DocOrderSort(order, items, &count) == XP3_ERR_OK,
          "large sort succeeds");
    check(count == CHILDREN, "large sort removes duplicates");
    for (i = 0; (i < count) && sorted; i++)
        sorted = (items[i].u.node == nodes[i]);
    check(sorted, "large sort in document order");

    /* Sorted input is only checked */
    check(xp3DocOrderSort(order, items, &count) == XP3_ERR_OK &&
          count == CHILDREN && items[0].u.node == nodes[0],
          "sorted input stays");

done:
    xp3DocOrderFree(order);
    xmlFree(items);
    xmlFree(nodes);
    xmlFreeDoc(doc);
}

/*
 * Random trees, checked against a model
 *
 * A second, deliberately naive implementation of the rules: walk the
 * libxml2 children and turn each run of text, CDATA and skipped nodes
 * into one node with the run's text, or into nothing if the run has
 * no content. Every axis of every node must list the model's nodes in
 * the model's order, and the sort must agree with it too. The random
 * trees mix all node kinds, with empty text and CDATA, so runs of
 * several libxml2 nodes and empty runs are common.
 */

#define XP3_RANDOM_TREES 200
#define XP3_RANDOM_MAX_NODES 512
#define XP3_RANDOM_TEXT 1024

typedef struct {
    xmlNodePtr node;            /* the canonical node */
    int parent;                 /* index of the parent, -1 for the root */
    int isAttribute;
    char text[XP3_RANDOM_TEXT]; /* the expected string value */
} xp3ModelNode;

static xp3ModelNode modelNodes[XP3_RANDOM_MAX_NODES];
static int modelCount;
/* Knuth's MMIX generator, for the same reasons as in testeval.c: the
 * same trees on every platform, a full period of 2^64, and the high
 * bits used because the low ones repeat quickly */
static uint64_t randomState;
static int randomNodesLeft;

static unsigned
randomBelow(unsigned limit) {
    randomState = randomState * UINT64_C(6364136223846793005) +
                  UINT64_C(1442695040888963407);
    return((unsigned) (randomState >> 33) % limit);
}

static int
modelIsText(const xmlNode *node) {
    return((node->type == XML_TEXT_NODE) ||
           (node->type == XML_CDATA_SECTION_NODE));
}

static int
modelIsSkipped(const xmlNode *node) {
    return((node->type == XML_ENTITY_REF_NODE) ||
           (node->type == XML_XINCLUDE_START) ||
           (node->type == XML_XINCLUDE_END) ||
           (node->type == XML_DTD_NODE));
}

static void
buildRandomChildren(xmlDocPtr doc, xmlNodePtr parent, int depth) {
    unsigned count = randomBelow(6);
    unsigned i;

    for (i = 0; (i < count) && (randomNodesLeft > 0); i++) {
        unsigned kind = randomBelow(9);
        const char *content;
        char name[16];
        xmlNodePtr node;

        randomNodesLeft--;
        snprintf(name, sizeof(name), "n%d", randomNodesLeft);
        content = (randomBelow(3) == 0) ? "" : name;
        if ((kind <= 2) && (depth < 5)) {
            unsigned attributes = randomBelow(3);
            unsigned j;

            node = appendRaw(parent, xmlNewDocNode(doc, NULL, BAD_CAST "e",
                                                   NULL));
            for (j = 0; j < attributes; j++) {
                char attrName[8];

                snprintf(attrName, sizeof(attrName), "a%u", j);
                xmlNewProp(node, BAD_CAST attrName, BAD_CAST content);
            }
            buildRandomChildren(doc, node, depth + 1);
        } else if (kind == 4) {
            appendRaw(parent, xmlNewCDataBlock(doc, BAD_CAST content,
                                               (int) strlen(content)));
        } else if (kind == 5) {
            appendRaw(parent, xmlNewDocComment(doc, BAD_CAST name));
        } else if (kind == 6) {
            appendRaw(parent, xmlNewDocPI(doc, BAD_CAST "pi",
                                          BAD_CAST name));
        } else if (kind == 7) {
            appendRaw(parent, xmlNewReference(doc, BAD_CAST "e"));
        } else if (kind == 8) {
            node = appendRaw(parent, xmlNewDocNode(doc, NULL,
                                                   BAD_CAST "include",
                                                   NULL));
            node->type = (randomBelow(2) == 0) ? XML_XINCLUDE_START :
                                                 XML_XINCLUDE_END;
        } else {
            appendRaw(parent, xmlNewDocText(doc, BAD_CAST content));
        }
    }
}

static xmlDocPtr
buildRandomTree(void) {
    xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
    xmlNodePtr root = (xmlNodePtr) doc;
    xmlNodePtr element;

    xmlCreateIntSubset(doc, BAD_CAST "r", NULL, NULL);
    xmlAddDocEntity(doc, BAD_CAST "e", XML_INTERNAL_GENERAL_ENTITY, NULL,
                    NULL, BAD_CAST "E");
    if (randomBelow(2) == 0)
        appendRaw(root, xmlNewDocComment(doc, BAD_CAST "top"));
    element = appendRaw(root, xmlNewDocNode(doc, NULL, BAD_CAST "r", NULL));
    if (randomBelow(2) == 0)
        xmlNewProp(element, BAD_CAST "a", BAD_CAST "1");
    randomNodesLeft = 150;
    buildRandomChildren(doc, element, 0);
    if (randomBelow(3) == 0)
        appendRaw(root, xmlNewDocPI(doc, BAD_CAST "end", NULL));
    return(doc);
}

static int
modelAdd(xmlNodePtr node, int parent, int isAttribute) {
    xp3ModelNode *model;

    if (modelCount >= XP3_RANDOM_MAX_NODES) {
        check(0, "random tree fits the model");
        return(-1);
    }
    model = &modelNodes[modelCount];
    model->node = node;
    model->parent = parent;
    model->isAttribute = isAttribute;
    model->text[0] = 0;
    return(modelCount++);
}

static void
modelAddText(int index, const xmlChar *text) {
    char *out = modelNodes[index].text;

    if ((text == NULL) || (index < 0))
        return;
    if (strlen(out) + strlen((const char *) text) >= XP3_RANDOM_TEXT) {
        check(0, "random text fits the model");
        return;
    }
    strcat(out, (const char *) text);
}

static void
modelChildren(xmlNodePtr parent, int parentIndex) {
    xmlNodePtr node = parent->children;

    while ((node != NULL) && (parentIndex >= 0)) {
        int index;

        if (modelIsText(node) || modelIsSkipped(node)) {
            index = -1;
            for (; (node != NULL) &&
                   (modelIsText(node) || modelIsSkipped(node));
                 node = node->next) {
                if ((!modelIsText(node)) || (node->content == NULL) ||
                    (node->content[0] == 0))
                    continue;
                if (index < 0)
                    index = modelAdd(node, parentIndex, 0);
                modelAddText(index, node->content);
            }
            continue;
        }
        index = modelAdd(node, parentIndex, 0);
        if (node->type == XML_ELEMENT_NODE) {
            xmlAttrPtr attr;

            for (attr = node->properties; attr != NULL; attr = attr->next) {
                int attrIndex = modelAdd((xmlNodePtr) attr, index, 1);

                if (attrIndex >= 0)
                    modelAddText(attrIndex, attr->children->content);
            }
            modelChildren(node, index);
        } else if (index >= 0) {
            modelAddText(index, node->content);
        }
        node = node->next;
    }
}

static int
modelIsAncestor(int ancestor, int index) {
    int parent;

    for (parent = modelNodes[index].parent; parent >= 0;
         parent = modelNodes[parent].parent) {
        if (parent == ancestor)
            return(1);
    }
    return(0);
}

/* The string value of documents and elements: their descendant text */
static void
modelSubtreeText(void) {
    int index;
    int other;

    for (index = 0; index < modelCount; index++) {
        xp3NodeKind kind = xp3NodeGetKind(modelNodes[index].node);

        if ((kind != XP3_NODE_DOCUMENT) && (kind != XP3_NODE_ELEMENT))
            continue;
        for (other = index + 1; other < modelCount; other++) {
            if (modelIsAncestor(index, other) &&
                modelIsText(modelNodes[other].node))
                modelAddText(index, BAD_CAST modelNodes[other].text);
        }
    }
}

static int
modelInAxis(xp3Axis axis, int context, int other) {
    const xp3ModelNode *contextNode = &modelNodes[context];
    const xp3ModelNode *otherNode = &modelNodes[other];
    int siblings = (!contextNode->isAttribute) &&
                   (!otherNode->isAttribute) &&
                   (contextNode->parent >= 0) &&
                   (otherNode->parent == contextNode->parent);

    switch (axis) {
        case XP3_AXIS_SELF:
            return(other == context);
        case XP3_AXIS_CHILD:
            return((otherNode->parent == context) &&
                   (!otherNode->isAttribute));
        case XP3_AXIS_ATTRIBUTE:
            return((otherNode->parent == context) && otherNode->isAttribute);
        case XP3_AXIS_PARENT:
            return(contextNode->parent == other);
        case XP3_AXIS_DESCENDANT:
            return(modelIsAncestor(context, other) &&
                   (!otherNode->isAttribute));
        case XP3_AXIS_DESCENDANT_OR_SELF:
            return((other == context) ||
                   (modelIsAncestor(context, other) &&
                    (!otherNode->isAttribute)));
        case XP3_AXIS_ANCESTOR:
            return(modelIsAncestor(other, context));
        case XP3_AXIS_ANCESTOR_OR_SELF:
            return((other == context) || modelIsAncestor(other, context));
        case XP3_AXIS_FOLLOWING_SIBLING:
            return(siblings && (other > context));
        case XP3_AXIS_PRECEDING_SIBLING:
            return(siblings && (other < context));
        case XP3_AXIS_FOLLOWING:
            return((other > context) && (!modelIsAncestor(context, other)) &&
                   (!otherNode->isAttribute));
        case XP3_AXIS_PRECEDING:
            return((other < context) && (!modelIsAncestor(other, context)) &&
                   (!otherNode->isAttribute));
        default:
            return(0);
    }
}

static int
modelIndexOf(xmlNodePtr node) {
    int index;

    for (index = 0; index < modelCount; index++) {
        if (modelNodes[index].node == node)
            return(index);
    }
    return(-1);
}

static void
checkRandomAxes(int tree) {
    static const xp3Axis axes[] = {
        XP3_AXIS_CHILD, XP3_AXIS_DESCENDANT, XP3_AXIS_ATTRIBUTE,
        XP3_AXIS_SELF, XP3_AXIS_DESCENDANT_OR_SELF,
        XP3_AXIS_FOLLOWING_SIBLING, XP3_AXIS_FOLLOWING, XP3_AXIS_PARENT,
        XP3_AXIS_ANCESTOR, XP3_AXIS_PRECEDING_SIBLING, XP3_AXIS_PRECEDING,
        XP3_AXIS_ANCESTOR_OR_SELF
    };
    int context;
    size_t axisIndex;

    for (context = 0; context < modelCount; context++) {
        for (axisIndex = 0; axisIndex < sizeof(axes) / sizeof(axes[0]);
             axisIndex++) {
            xp3Axis axis = axes[axisIndex];
            int reverse = (axis == XP3_AXIS_PARENT) ||
                          (axis == XP3_AXIS_ANCESTOR) ||
                          (axis == XP3_AXIS_ANCESTOR_OR_SELF) ||
                          (axis == XP3_AXIS_PRECEDING_SIBLING) ||
                          (axis == XP3_AXIS_PRECEDING);
            xmlNodePtr node = NULL;
            int expected = reverse ? modelCount : -1;
            int position = 0;

            while (1) {
                /* The next model node on the axis, in axis order */
                do {
                    expected += reverse ? -1 : 1;
                } while ((expected >= 0) && (expected < modelCount) &&
                         (!modelInAxis(axis, context, expected)));
                if ((expected < 0) || (expected >= modelCount))
                    expected = -1;
                node = xp3AxisNext(axis, modelNodes[context].node, node);
                if ((node == NULL) && (expected < 0))
                    break;
                if ((node == NULL) || (expected < 0) ||
                    (modelNodes[expected].node != node)) {
                    fprintf(stderr, "FAIL: random tree %d, axis %d of node "
                            "%d: item %d is node %d, expected %d\n", tree,
                            (int) axis, context, position,
                            (node != NULL) ? modelIndexOf(node) : -1,
                            expected);
                    failures++;
                    break;
                }
                position++;
            }
        }
    }
}

static void
checkRandomValues(int tree) {
    int index;

    for (index = 0; index < modelCount; index++) {
        xmlChar *value = NULL;

        if ((xp3NodeStringValue(modelNodes[index].node, &value, NULL) !=
             XP3_ERR_OK) ||
            (strcmp((const char *) value, modelNodes[index].text) != 0)) {
            fprintf(stderr, "FAIL: random tree %d, string value of node "
                    "%d: \"%s\", expected \"%s\"\n", tree, index,
                    (value != NULL) ? (const char *) value : "NULL",
                    modelNodes[index].text);
            failures++;
        }
        xmlFree(value);
    }
}

/* Any libxml2 node of a model node's run, chosen at random */
static xmlNodePtr
randomRunMember(xmlNodePtr node) {
    xmlNodePtr member;

    if (!modelIsText(node))
        return(node);
    for (member = node->next;
         (member != NULL) && (modelIsText(member) ||
                              modelIsSkipped(member));
         member = member->next) {
        if (modelIsText(member) && (randomBelow(2) == 0))
            return(member);
    }
    return(node);
}

static void
checkRandomOrder(int tree) {
    static xp3Item items[XP3_RANDOM_MAX_NODES * 2];
    static char picked[XP3_RANDOM_MAX_NODES];
    xp3DocOrder *order = xp3DocOrderNew();
    size_t count = 0;
    size_t kept = 0;
    int index;
    int pair;

    if (order == NULL) {
        check(0, "random order table created");
        return;
    }
    memset(picked, 0, sizeof(picked));
    while (count < (size_t) modelCount * 2) {
        index = (int) randomBelow((unsigned) modelCount);
        picked[index] = 1;
        items[count++] = xp3ItemFromNode(
            randomRunMember(modelNodes[index].node));
    }
    if (xp3DocOrderSort(order, items, &count) != XP3_ERR_OK) {
        check(0, "random sort succeeds");
        xp3DocOrderFree(order);
        return;
    }
    for (index = 0; index < modelCount; index++) {
        if (!picked[index])
            continue;
        if ((kept >= count) || (items[kept].u.node !=
                                modelNodes[index].node)) {
            fprintf(stderr, "FAIL: random tree %d, sorted item %d is not "
                    "node %d\n", tree, (int) kept, index);
            failures++;
            break;
        }
        kept++;
    }
    check(kept == count, "random sort keeps each node once");

    for (pair = 0; pair < 50; pair++) {
        int first = (int) randomBelow((unsigned) modelCount);
        int second = (int) randomBelow((unsigned) modelCount);
        int result = 99;

        if ((xp3DocOrderCompare(order, modelNodes[first].node,
                                modelNodes[second].node, &result) !=
             XP3_ERR_OK) ||
            ((first < second) != (result < 0)) ||
            ((first > second) != (result > 0))) {
            fprintf(stderr, "FAIL: random tree %d, comparing nodes %d and "
                    "%d gave %d\n", tree, first, second, result);
            failures++;
        }
    }
    xp3DocOrderFree(order);
}

static void
testRandomTrees(void) {
    int tree;

    for (tree = 0; (tree < XP3_RANDOM_TREES) && (failures < 20); tree++) {
        xmlDocPtr doc;
        int root;

        randomState = (uint64_t) tree + 1;
        doc = buildRandomTree();
        modelCount = 0;
        root = modelAdd((xmlNodePtr) doc, -1, 0);
        modelChildren((xmlNodePtr) doc, root);
        modelSubtreeText();

        checkRandomAxes(tree);
        checkRandomValues(tree);
        checkRandomOrder(tree);
        xmlFreeDoc(doc);
    }
}

/*
 * Allocation failures
 */

/* Run everything that allocates; returns -1 on failure */
static int
allocatingWork(const xp3TestTree *tree, char *out, size_t size) {
    xp3DocOrder *order;
    xp3Item items[64];
    xp3Item reversed[64];
    xp3Atomic *value;
    xmlChar *str;
    size_t count;
    size_t i;
    int ret = -1;

    out[0] = 0;
    if (xp3NodeStringValue(tree->r, &str, NULL) != XP3_ERR_OK)
        return(-1);
    xmlFree(str);
    if (xp3NodeTypedValue(tree->go, &value) != XP3_ERR_OK)
        return(-1);
    xp3AtomicUnref(value);

    order = xp3DocOrderNew();
    if (order == NULL)
        return(-1);
    count = collectNodes(tree, items, 32);
    for (i = 0; i < count; i++)
        reversed[i] = items[count - 1 - i];
    if (xp3DocOrderSort(order, reversed, &count) == XP3_ERR_OK) {
        /* Compare pointers: describing nodes would allocate too */
        for (i = 0; (i < count) && (reversed[i].u.node == items[i].u.node);
             i++)
            ;
        snprintf(out, size, "%s", (i == count) ? "sorted" : "not sorted");
        ret = 0;
    } else if (reversed[0].u.node != items[count - 1].u.node) {
        snprintf(out, size, "items changed on failure");
    }

    /* The second tree makes the table grow while adding attributes */
    if (ret == 0) {
        xmlNodePtr first = (xmlNodePtr) tree->wide->properties;
        xmlNodePtr last = first;

        while (last->next != NULL)
            last = last->next;
        items[0] = xp3ItemFromNode(last);
        items[1] = xp3ItemFromNode(first);
        count = 2;
        if (xp3DocOrderSort(order, items, &count) != XP3_ERR_OK) {
            out[0] = 0;
            ret = -1;
        } else if ((count != 2) || (items[0].u.node != first)) {
            snprintf(out, size, "attributes not sorted");
        }
    }
    xp3DocOrderFree(order);
    return(ret);
}

static void
testAllocationFailures(const xp3TestTree *tree) {
    char actual[512];
    int i;

    check(allocatingWork(tree, actual, sizeof(actual)) == 0 &&
          strcmp(actual, "sorted") == 0, "reference work done");

    if (!xp3TestAllocInstall()) {
        printf("testnodes: allocation-failure tests skipped: this "
               "libxml2 ignores xmlMemSetup()\n");
        xp3TestAllocRestore();
        return;
    }
    for (i = 0; i < 1000; i++) {
        int ret;

        xp3TestAllocFailAt(i);
        ret = allocatingWork(tree, actual, sizeof(actual));
        if (!xp3TestAllocFailed()) {
            check(ret == 0 && strcmp(actual, "sorted") == 0,
                  "work complete after allocation failures");
            xp3TestAllocFailAt(-1);
            break;
        }
        xp3TestAllocFailAt(-1);
        check(ret == -1 && actual[0] == 0,
              "a failed allocation fails cleanly");
    }
    check(i > 0 && i < 1000, "allocation failure points exercised");
    xp3TestAllocRestore();
}

int
main(void) {
    xp3TestTree tree;

    xmlInitParser();
    buildTree(&tree);

    testKinds(&tree);
    testAxes(&tree);
    testParentless();
    testNames(&tree);
    testValues(&tree);
    testParsed();
    testOrder(&tree);
    testOrderAtomic(&tree);
    testOrderTrees(&tree);
    testOrderChanged();
    testOrderLarge();
    testRandomTrees();
    testAllocationFailures(&tree);

    xmlFreeDoc(tree.wideDoc);
    xmlFreeDoc(tree.doc);
    if (failures == 0)
        printf("testnodes: OK\n");
    return(failures == 0 ? 0 : 1);
}
