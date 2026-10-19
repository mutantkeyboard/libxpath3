/**
 * @file
 *
 * @brief XDM nodes on libxml2's tree (internal)
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XP3NODES_H
#define XP3_XP3NODES_H

#include <stddef.h>

#include <libxml/tree.h>
#include <libxml/xmlstring.h>

#include "private/xp3ast.h"
#include "private/xp3error.h"
#include "private/xp3value.h"

/*
 * Kinds of XDM nodes. Namespace nodes are not supported (doc/DESIGN.md
 * 4.5). XP3_NODE_NONE is a libxml2 node that XDM has no node for, such
 * as a DTD, an entity reference or a text node inside an attribute.
 */
typedef enum {
    XP3_NODE_NONE = 0,
    XP3_NODE_DOCUMENT,          /* also a document fragment */
    XP3_NODE_ELEMENT,
    XP3_NODE_ATTRIBUTE,         /* xmlAttr, cast to xmlNode */
    XP3_NODE_TEXT,              /* text or CDATA section */
    XP3_NODE_COMMENT,
    XP3_NODE_PI
} xp3NodeKind;

/* Document order of the nodes of one or more trees, built on demand */
typedef struct _xp3DocOrder xp3DocOrder;

/*
 * Nodes. Apart from xp3NodeGetKind() and xp3NodeCanonical(), these
 * take the libxml2 node that represents an XDM node, as
 * xp3NodeCanonical() returns it, and only ever return such nodes.
 */
xp3NodeKind
xp3NodeGetKind(const xmlNode *node);

xmlNodePtr
xp3NodeCanonical(xmlNodePtr node);

xmlNodePtr
xp3NodeParent(xmlNodePtr node);

xmlNodePtr
xp3NodeRoot(xmlNodePtr node);

xmlNodePtr
xp3AxisNext(xp3Axis axis, xmlNodePtr context, xmlNodePtr cur);

const xmlChar *
xp3NodeLocalName(const xmlNode *node);

const xmlChar *
xp3NodeNamespaceUri(const xmlNode *node);

const xmlChar *
xp3NodePrefix(const xmlNode *node);

xp3ErrCode
xp3NodeStringValue(xmlNodePtr node, xmlChar **value, size_t *length);

xp3ErrCode
xp3NodeTypedValue(xmlNodePtr node, xp3Atomic **value);

/*
 * Document order
 */
xp3DocOrder *
xp3DocOrderNew(void);

void
xp3DocOrderFree(xp3DocOrder *order);

xp3ErrCode
xp3DocOrderCompare(xp3DocOrder *order, xmlNodePtr first,
                   xmlNodePtr second, int *result);

xp3ErrCode
xp3DocOrderSort(xp3DocOrder *order, xp3Item *items, size_t *count);

#endif /* XP3_XP3NODES_H */
