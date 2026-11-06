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

#include <string.h>

#include <libxml/tree.h>
#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

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
