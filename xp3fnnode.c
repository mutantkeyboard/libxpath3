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
