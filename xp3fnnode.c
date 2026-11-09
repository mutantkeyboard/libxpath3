/*
 * xp3fnnode.c: the functions on nodes
 *
 * Implementations of the node functions of F&O 3.1,
 * https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/, bound
 * to their signatures in xp3func.c. The evaluator has applied the
 * function conversion rules to the arguments already, so a node()?
 * argument is the empty sequence or one node.
 *
 * Nodes are read through the XDM view of xp3nodes.c, which has no
 * namespace nodes (doc/DESIGN.md 4.5). The forms without an argument
 * take the context item, which must be a node.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libxml/tree.h>
#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

#include "private/xp3context.h"
#include "private/xp3func.h"
#include "private/xp3nodes.h"
#include "private/xp3types.h"
#include "private/xp3value.h"

/*
 * Helpers
 */

/*
 * The node of a call: its node()? argument, or the context item for
 * the form without one. Returns 1, 0 for the empty sequence, or -1
 * after recording an error.
 */
static int
argNode(xp3FuncCall *call, xp3Value **args, int count, xmlNodePtr *node) {
    xp3Item item;

    *node = NULL;
    if (count == 0) {
        if (xp3FuncFocus(call, &item, NULL, NULL) < 0)
            return(-1);
        if (item.kind != XP3_ITEM_NODE) {
            xp3FuncError(call, XP3_ERR_XPTY0004, "the context item is not "
                         "a node");
            return(-1);
        }
    } else if (xp3ValueCount(args[0]) == 0) {
        return(0);
    } else if (xp3ValueItemAt(args[0], 0, &item) <= 0) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(-1);
    }
    /* A node item holds no reference */
    *node = item.u.node;
    return(1);
}

/* A new string-like value as the result: checked against the byte
 * limit and counted as made */
static xp3Value *
newString(xp3FuncCall *call, xp3TypeId type, const xmlChar *str) {
    size_t len = (str != NULL) ? strlen((const char *) str) : 0;

    if ((xp3FuncStringFits(call, len) < 0) || (xp3FuncBytes(call, len) < 0))
        return(NULL);
    return(xp3FuncAtomic(call, xp3AtomicNewString(type, str, len)));
}

/*
 * Names
 */

/**
 * fn:node-name#0 and #1 (F&O 3.1, 2.1): the name of an element or
 * attribute with its prefix, or the target of a processing
 * instruction in no namespace. Other nodes and the empty sequence
 * give the empty sequence.
 *
 * @param call   the call
 * @param args   the node, node()?
 * @param count  0 or 1
 * @returns an xs:QName or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnNodeName(xp3FuncCall *call, xp3Value **args, int count) {
    xmlNodePtr node;
    int ret = argNode(call, args, count, &node);

    if (ret < 0)
        return(NULL);
    if ((ret == 0) || (xp3NodeLocalName(node) == NULL))
        return(xp3ValueEmpty());
    return(xp3FuncAtomic(call, xp3AtomicNewQName(xp3NodeNamespaceUri(node),
                                                 xp3NodePrefix(node),
                                                 xp3NodeLocalName(node))));
}

/**
 * fn:name#0 and #1 (F&O 3.1, 13.1): the name of a node as it is
 * written, "prefix:local" or just the local name without a prefix.
 * Nodes without a name and the empty sequence give "".
 *
 * @param call   the call
 * @param args   the node, node()?
 * @param count  0 or 1
 * @returns an xs:string, or NULL on error
 */
xp3Value *
xp3FnName(xp3FuncCall *call, xp3Value **args, int count) {
    const xmlChar *prefix;
    const xmlChar *local;
    xmlNodePtr node;
    xp3Value *result;
    xmlChar *name;
    size_t prefixLen;
    size_t localLen;
    int ret = argNode(call, args, count, &node);

    if (ret < 0)
        return(NULL);
    if (ret == 0)
        return(newString(call, XP3_TYPE_STRING, NULL));
    local = xp3NodeLocalName(node);
    prefix = xp3NodePrefix(node);
    if ((local == NULL) || (prefix == NULL) || (prefix[0] == 0))
        return(newString(call, XP3_TYPE_STRING, local));

    prefixLen = strlen((const char *) prefix);
    localLen = strlen((const char *) local);
    name = xmlMalloc(prefixLen + localLen + 2);
    if (name == NULL) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(NULL);
    }
    memcpy(name, prefix, prefixLen);
    name[prefixLen] = ':';
    memcpy(name + prefixLen + 1, local, localLen + 1);
    result = newString(call, XP3_TYPE_STRING, name);
    xmlFree(name);
    return(result);
}

/**
 * fn:local-name#0 and #1 (F&O 3.1, 13.2): the local part of the name
 * of a node, the target of a processing instruction. Nodes without a
 * name and the empty sequence give "".
 *
 * @param call   the call
 * @param args   the node, node()?
 * @param count  0 or 1
 * @returns an xs:string, or NULL on error
 */
xp3Value *
xp3FnLocalName(xp3FuncCall *call, xp3Value **args, int count) {
    xmlNodePtr node;
    int ret = argNode(call, args, count, &node);

    if (ret < 0)
        return(NULL);
    return(newString(call, XP3_TYPE_STRING,
                     (ret > 0) ? xp3NodeLocalName(node) : NULL));
}

/**
 * fn:namespace-uri#0 and #1 (F&O 3.1, 13.3): the namespace URI of the
 * name of an element or attribute. Names in no namespace, other nodes
 * and the empty sequence give "".
 *
 * @param call   the call
 * @param args   the node, node()?
 * @param count  0 or 1
 * @returns an xs:anyURI, or NULL on error
 */
xp3Value *
xp3FnNamespaceUri(xp3FuncCall *call, xp3Value **args, int count) {
    xmlNodePtr node;
    int ret = argNode(call, args, count, &node);

    if (ret < 0)
        return(NULL);
    return(newString(call, XP3_TYPE_ANY_URI,
                     (ret > 0) ? xp3NodeNamespaceUri(node) : NULL));
}

/*
 * Tree shape
 */

/**
 * fn:root#0 and #1 (F&O 3.1, 13.5): the root of the tree a node is
 * in, a document node or, in a tree without one, the node without a
 * parent. The empty sequence gives the empty sequence.
 *
 * @param call   the call
 * @param args   the node, node()?
 * @param count  0 or 1
 * @returns a node or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnRoot(xp3FuncCall *call, xp3Value **args, int count) {
    xmlNodePtr node;
    xp3Value *result;
    int ret = argNode(call, args, count, &node);

    if (ret < 0)
        return(NULL);
    if (ret == 0)
        return(xp3ValueEmpty());
    result = xp3ValueSingle(xp3ItemFromNode(xp3NodeRoot(node)));
    if (result == NULL)
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
    return(result);
}

/**
 * fn:has-children#0 and #1 (F&O 3.1, 13.7): whether a node has a
 * child in XDM. Attributes are not children, and neither are the
 * libxml2 nodes XDM has no node for. The empty sequence gives false.
 *
 * @param call   the call
 * @param args   the node, node()?
 * @param count  0 or 1
 * @returns an xs:boolean, or NULL on error
 */
xp3Value *
xp3FnHasChildren(xp3FuncCall *call, xp3Value **args, int count) {
    xmlNodePtr node;
    int ret = argNode(call, args, count, &node);

    if (ret < 0)
        return(NULL);
    return(xp3FuncAtomic(call, xp3AtomicNewBoolean(
        (ret > 0) && (xp3AxisNext(XP3_AXIS_CHILD, node, NULL) != NULL))));
}

/*
 * Whether ancestor is a proper ancestor of node, through the parents
 * of XDM, in which an attribute's parent is its element. Counts a step
 * per parent visited. Returns 1, 0, or -1 after recording LIMIT.
 */
static int
isAncestor(xp3FuncCall *call, xmlNodePtr ancestor, xmlNodePtr node) {
    for (node = xp3NodeParent(node); node != NULL;
         node = xp3NodeParent(node)) {
        if (xp3FuncSteps(call, 1) < 0)
            return(-1);
        if (node == ancestor)
            return(1);
    }
    return(0);
}

/*
 * fn:innermost or fn:outermost: the nodes in document order without
 * duplicates, then one pass. In document order, the nodes below a
 * node, its attributes included, follow it with no other node between
 * them. So if a node of the sequence has another one below it, the
 * next one is below it, and the outermost node of the sequence above
 * a node is the last one kept before it: that one is kept, and every
 * node between them is below it and so not kept.
 */
static xp3Value *
extremeNodes(xp3FuncCall *call, xp3Value *nodes, int innermost) {
    xp3SeqBuilder builder;
    xp3Iter iter;
    xp3Item item;
    size_t kept = 0;
    size_t i;
    int ret;

    if (xp3ValueCount(nodes) <= 1)
        return(xp3ValueRef(nodes));
    xp3FuncBuilderInit(call, &builder);
    xp3IterInit(&iter, nodes);
    while ((ret = xp3IterNext(&iter, &item)) > 0) {
        if (xp3FuncBuilderAdd(call, &builder, item) < 0)
            goto error;
    }
    if (ret < 0) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        goto error;
    }
    if (xp3FuncSortNodes(call, &builder) < 0)
        goto error;

    for (i = 0; i < builder.count; i++) {
        xmlNodePtr node = builder.items[i].u.node;

        if (innermost && (i + 1 < builder.count))
            ret = isAncestor(call, node, builder.items[i + 1].u.node);
        else if (!innermost && (kept > 0))
            ret = isAncestor(call, builder.items[kept - 1].u.node, node);
        else
            ret = 0;
        if (ret < 0)
            goto error;
        if (ret == 0)
            builder.items[kept++] = builder.items[i];
    }
    builder.count = kept;
    return(xp3FuncBuilderFinish(call, &builder));

error:
    xp3SeqBuilderClear(&builder);
    return(NULL);
}

/**
 * fn:innermost (F&O 3.1, 13.8): the nodes that are not an ancestor of
 * another node of the sequence, in document order without duplicates.
 *
 * @param call   the call
 * @param args   the nodes, node()*
 * @param count  1
 * @returns the nodes, or NULL on error
 */
xp3Value *
xp3FnInnermost(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(extremeNodes(call, args[0], 1));
}

/**
 * fn:outermost (F&O 3.1, 13.9): the nodes that have no ancestor in the
 * sequence, in document order without duplicates.
 *
 * @param call   the call
 * @param args   the nodes, node()*
 * @param count  1
 * @returns the nodes, or NULL on error
 */
xp3Value *
xp3FnOutermost(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(extremeNodes(call, args[0], 0));
}

/*
 * Paths
 */

/* The start of a path in a tree whose root is not a document node */
#define XP3_PATH_ROOT "Q{" XP3_NS_FN "}root()"

/* The longest string, as for every atomic value (xp3AtomicNewString()) */
#define XP3_STRING_MAX ((size_t) INT32_MAX)

/* A step of a path: a node below the root, and its position among its
 * like siblings, which an attribute doesn't have */
typedef struct {
    xmlNodePtr node;
    size_t position;
} xp3PathStep;

/*
 * Whether a sibling counts for the position of a node: a node of the
 * same kind, with the same expanded name for an element and the same
 * target for a processing instruction. Text nodes and comments have no
 * name, so each of the same kind counts.
 */
static int
isLikeSibling(xmlNodePtr node, xmlNodePtr sibling) {
    const xmlChar *localName = xp3NodeLocalName(node);
    const xmlChar *siblingLocalName = xp3NodeLocalName(sibling);
    const xmlChar *uri = xp3NodeNamespaceUri(node);
    const xmlChar *siblingUri = xp3NodeNamespaceUri(sibling);

    if (xp3NodeGetKind(sibling) != xp3NodeGetKind(node))
        return(0);
    if (!xmlStrEqual(siblingLocalName, localName))
        return(0);
    return(xmlStrEqual(siblingUri, uri));
}

/*
 * The position of a node among its like siblings, from 1. The siblings
 * are those of XDM, so a run of adjacent text nodes counts once.
 * Counts a step per preceding sibling. Returns 0, or -1 after recording
 * LIMIT.
 */
static int
stepPosition(xp3FuncCall *call, xmlNodePtr node, size_t *position) {
    xmlNodePtr sibling;

    *position = 1;
    for (sibling = xp3AxisNext(XP3_AXIS_PRECEDING_SIBLING, node, NULL);
         sibling != NULL;
         sibling = xp3AxisNext(XP3_AXIS_PRECEDING_SIBLING, node, sibling)) {
        if (xp3FuncSteps(call, 1) < 0)
            return(-1);
        if (isLikeSibling(node, sibling))
            (*position)++;
    }
    return(0);
}

/*
 * The steps of the path to a node, one for each ancestor-or-self below
 * the root, from the top down, and whether the root is a document
 * node. Counts a step per ancestor and per preceding sibling. Returns
 * 0, or -1 after recording an error. *steps is NULL when the node is
 * the root; otherwise the caller frees it.
 */
static int
pathSteps(xp3FuncCall *call, xmlNodePtr node, xp3PathStep **steps,
          size_t *count, int *underDocument) {
    xmlNodePtr root = node;
    xmlNodePtr parent;
    xmlNodePtr cur;
    xp3PathStep *list;
    size_t depth = 0;
    size_t i;

    *steps = NULL;
    *count = 0;

    /* The depth first, so that the steps are allocated once */
    for (parent = xp3NodeParent(node); parent != NULL;
         parent = xp3NodeParent(parent)) {
        if (xp3FuncSteps(call, 1) < 0)
            return(-1);
        depth++;
        root = parent;
    }
    *underDocument = (xp3NodeGetKind(root) == XP3_NODE_DOCUMENT);
    if (depth == 0)
        return(0);

    if (depth > SIZE_MAX / sizeof(xp3PathStep)) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(-1);
    }
    list = xmlMalloc(depth * sizeof(xp3PathStep));
    if (list == NULL) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(-1);
    }

    /* Then the steps from the node up, filled in from the end */
    cur = node;
    for (i = depth; i > 0; i--) {
        xp3PathStep *step = &list[i - 1];

        step->node = cur;
        step->position = 0;
        if (xp3NodeGetKind(cur) != XP3_NODE_ATTRIBUTE) {
            if (stepPosition(call, cur, &step->position) < 0) {
                xmlFree(list);
                return(-1);
            }
        }
        cur = xp3NodeParent(cur);
    }
    *steps = list;
    *count = depth;
    return(0);
}

/*
 * A path is written in two passes over its steps, first counting the
 * bytes and then copying them, so it is allocated once. out is NULL in
 * the first pass. A length that would overflow stays at SIZE_MAX.
 */
static size_t
addText(xmlChar *out, size_t length, const xmlChar *text) {
    size_t size;

    if (text == NULL)
        return(length);
    size = strlen((const char *) text);
    if (size > SIZE_MAX - length)
        return(SIZE_MAX);
    if (out != NULL)
        memcpy(out + length, text, size);
    return(length + size);
}

/* "[position]" */
static size_t
addPosition(xmlChar *out, size_t length, size_t position) {
    char text[32];

    snprintf(text, sizeof(text), "[%zu]", position);
    return(addText(out, length, BAD_CAST text));
}

/* "Q{uri}local", with an empty uri for a name in no namespace */
static size_t
addExpandedName(xmlChar *out, size_t length, const xmlNode *node) {
    length = addText(out, length, BAD_CAST "Q{");
    length = addText(out, length, xp3NodeNamespaceUri(node));
    length = addText(out, length, BAD_CAST "}");
    return(addText(out, length, xp3NodeLocalName(node)));
}

/* A step, "/" and the form F&O 3.1, 13.6 gives the kind of node */
static size_t
addStep(xmlChar *out, size_t length, const xp3PathStep *step) {
    const xmlNode *node = step->node;

    length = addText(out, length, BAD_CAST "/");
    switch (xp3NodeGetKind(node)) {
        case XP3_NODE_ELEMENT:
            length = addExpandedName(out, length, node);
            break;
        case XP3_NODE_ATTRIBUTE:
            /* No position, and a name in no namespace as it is */
            length = addText(out, length, BAD_CAST "@");
            if (xp3NodeNamespaceUri(node) == NULL)
                return(addText(out, length, xp3NodeLocalName(node)));
            return(addExpandedName(out, length, node));
        case XP3_NODE_TEXT:
            length = addText(out, length, BAD_CAST "text()");
            break;
        case XP3_NODE_COMMENT:
            length = addText(out, length, BAD_CAST "comment()");
            break;
        case XP3_NODE_PI:
            length = addText(out, length,
                             BAD_CAST "processing-instruction(");
            length = addText(out, length, xp3NodeLocalName(node));
            length = addText(out, length, BAD_CAST ")");
            break;
        default:
            /* A document node is always a root */
            break;
    }
    return(addPosition(out, length, step->position));
}

/* The whole path, with the start of a tree without a document node */
static size_t
addPath(xmlChar *out, const xp3PathStep *steps, size_t count,
        int underDocument) {
    size_t length = 0;
    size_t i;

    if (!underDocument)
        length = addText(out, length, BAD_CAST XP3_PATH_ROOT);
    for (i = 0; i < count; i++)
        length = addStep(out, length, &steps[i]);
    return(length);
}

/* The path of the steps as the result, checked against the limits
 * before it is built and counted as made */
static xp3Value *
pathString(xp3FuncCall *call, const xp3PathStep *steps, size_t count,
           int underDocument) {
    size_t length = addPath(NULL, steps, count, underDocument);
    xp3Atomic *atomic;
    xmlChar *path;

    if (length > XP3_STRING_MAX) {
        xp3FuncError(call, XP3_ERR_LIMIT, "the result of fn:path would "
                     "be longer than 2 GB");
        return(NULL);
    }
    if (xp3FuncStringFits(call, length) < 0)
        return(NULL);
    if (xp3FuncBytes(call, length) < 0)
        return(NULL);

    path = xmlMalloc(length + 1);
    if (path == NULL) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(NULL);
    }
    addPath(path, steps, count, underDocument);
    path[length] = 0;
    atomic = xp3AtomicNewString(XP3_TYPE_STRING, path, length);
    xmlFree(path);
    return(xp3FuncAtomic(call, atomic));
}

/**
 * fn:path#0 and #1 (F&O 3.1, 13.6): a path that selects a node from
 * the root of its tree, "/" for a document node. Each ancestor-or-self
 * below the root is a step, an element with its expanded name and its
 * position among the siblings of that name, an attribute with its
 * name, and other nodes with their kind test and position. In a tree
 * whose root is not a document node, the path starts with
 * Q{http://www.w3.org/2005/xpath-functions}root(), which is all of the
 * path of such a root. The empty sequence gives the empty sequence.
 *
 * @param call   the call
 * @param args   the node, node()?
 * @param count  0 or 1
 * @returns an xs:string or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnPath(xp3FuncCall *call, xp3Value **args, int count) {
    xp3PathStep *steps;
    size_t stepCount;
    int underDocument;
    xmlNodePtr node;
    xp3Value *result;
    int ret = argNode(call, args, count, &node);

    if (ret < 0)
        return(NULL);
    if (ret == 0)
        return(xp3ValueEmpty());
    if (xp3NodeGetKind(node) == XP3_NODE_DOCUMENT)
        return(newString(call, XP3_TYPE_STRING, BAD_CAST "/"));

    if (pathSteps(call, node, &steps, &stepCount, &underDocument) < 0)
        return(NULL);
    result = pathString(call, steps, stepCount, underDocument);
    xmlFree(steps);
    return(result);
}
