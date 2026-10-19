/*
 * xp3nodes.c: XDM nodes on libxml2's tree
 *
 * XDM nodes are libxml2 nodes, borrowed and never changed (doc/DESIGN.md
 * 3 and 4.5). Where the two trees differ, navigation presents the XDM
 * one:
 *
 * - Adjacent text nodes and CDATA sections form one XDM text node. The
 *   first libxml2 node of the run with non-empty content represents it
 *   ("canonical"), and its string value is the text of the whole run.
 *   XDM has no empty text nodes, so a run without content is no node.
 * - Nodes XDM has no node for are skipped: the DTD, XInclude markers,
 *   and entity references together with their content. The content of
 *   an entity reference is shared by every reference to the entity and
 *   its parent is the declaration, so it can't be shown in place.
 *   Documents need XML_PARSE_NOENT to see it, as libxslt parses them.
 *   Text on both sides of a skipped node is one run.
 * - Namespace declarations are not attributes, and namespace nodes are
 *   not supported.
 *
 * Document order is kept in a side table that maps each node of a tree
 * to its position, built the first time a node of that tree is
 * compared. libxml2's xmlXPathOrderDocElems() writes the order into
 * the tree instead, which this library never does.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/tree.h>
#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

#include "private/xp3nodes.h"

/*
 * Kinds
 */

/* The kind a libxml2 node type stands for, wherever the node is */
static xp3NodeKind
kindOfType(xmlElementType type) {
    switch (type) {
        case XML_DOCUMENT_NODE:
        case XML_HTML_DOCUMENT_NODE:
        case XML_DOCUMENT_FRAG_NODE:
            return(XP3_NODE_DOCUMENT);
        case XML_ELEMENT_NODE:
            return(XP3_NODE_ELEMENT);
        case XML_ATTRIBUTE_NODE:
            return(XP3_NODE_ATTRIBUTE);
        case XML_TEXT_NODE:
        case XML_CDATA_SECTION_NODE:
            return(XP3_NODE_TEXT);
        case XML_COMMENT_NODE:
            return(XP3_NODE_COMMENT);
        case XML_PI_NODE:
            return(XP3_NODE_PI);
        default:
            return(XP3_NODE_NONE);
    }
}

/* Whether a node can have XDM children: a document or an element */
static int
hasChildren(const xmlNode *node) {
    xp3NodeKind kind = kindOfType(node->type);

    return((kind == XP3_NODE_DOCUMENT) || (kind == XP3_NODE_ELEMENT));
}

static int
isText(const xmlNode *node) {
    return((node->type == XML_TEXT_NODE) ||
           (node->type == XML_CDATA_SECTION_NODE));
}

static int
isEmptyText(const xmlNode *node) {
    return((node->content == NULL) || (node->content[0] == 0));
}

/* Part of a text run: text, or a node that navigation skips */
static int
inTextRun(const xmlNode *node) {
    return(isText(node) || (kindOfType(node->type) == XP3_NODE_NONE));
}

/**
 * Get the XDM kind of a libxml2 node. Nodes inside an attribute or an
 * entity declaration have none, and neither have nodes XDM doesn't
 * know, such as the DTD or an entity reference.
 *
 * @param node  the node
 * @returns the kind, XP3_NODE_NONE if XDM has no such node
 */
xp3NodeKind
xp3NodeGetKind(const xmlNode *node) {
    xp3NodeKind kind;

    if (node == NULL)
        return(XP3_NODE_NONE);
    kind = kindOfType(node->type);
    if ((kind == XP3_NODE_NONE) || (kind == XP3_NODE_DOCUMENT) ||
        (kind == XP3_NODE_ATTRIBUTE))
        return(kind);
    if ((node->parent != NULL) && (!hasChildren(node->parent)))
        return(XP3_NODE_NONE);
    return(kind);
}

/*
 * Siblings. A parentless node is the root of its tree and has none,
 * whatever its next and prev fields say.
 */

/*
 * The first XDM node at or after a libxml2 sibling: skip nodes XDM
 * doesn't have, and turn a text run into its canonical node or skip
 * it if it is empty.
 */
static xmlNodePtr
visibleFrom(xmlNodePtr node) {
    while (node != NULL) {
        if (isText(node)) {
            for (; (node != NULL) && inTextRun(node); node = node->next) {
                if (isText(node) && (!isEmptyText(node)))
                    return(node);
            }
        } else if (kindOfType(node->type) == XP3_NODE_NONE) {
            node = node->next;
        } else {
            return(node);
        }
    }
    return(NULL);
}

/*
 * The last XDM node at or before a libxml2 sibling. A text run found
 * this way is entered at its end, so its start is looked up first.
 */
static xmlNodePtr
visibleUpTo(xmlNodePtr node) {
    while (node != NULL) {
        if (isText(node)) {
            xmlNodePtr start = node;
            xmlNodePtr text;

            while ((start->prev != NULL) && inTextRun(start->prev))
                start = start->prev;
            text = visibleFrom(start);
            if ((text != NULL) && isText(text))
                return(text);
            node = start->prev;
        } else if (kindOfType(node->type) == XP3_NODE_NONE) {
            node = node->prev;
        } else {
            return(node);
        }
    }
    return(NULL);
}

static xmlNodePtr
nextSibling(xmlNodePtr node) {
    xmlNodePtr next;

    if ((node->parent == NULL) || (node->type == XML_ATTRIBUTE_NODE))
        return(NULL);
    next = node->next;
    if (isText(node)) {
        /* Skip the rest of the run */
        while ((next != NULL) && inTextRun(next))
            next = next->next;
    }
    return(visibleFrom(next));
}

static xmlNodePtr
prevSibling(xmlNodePtr node) {
    xmlNodePtr prev;

    if ((node->parent == NULL) || (node->type == XML_ATTRIBUTE_NODE))
        return(NULL);
    prev = node->prev;
    if (isText(node)) {
        /* Skip the start of the run, empty text before node included */
        while ((prev != NULL) && inTextRun(prev))
            prev = prev->prev;
    }
    return(visibleUpTo(prev));
}

static xmlNodePtr
firstChild(xmlNodePtr node) {
    if (!hasChildren(node))
        return(NULL);
    return(visibleFrom(node->children));
}

static xmlNodePtr
lastChild(xmlNodePtr node) {
    if (!hasChildren(node))
        return(NULL);
    return(visibleUpTo(node->last));
}

/**
 * Find the XDM node a libxml2 node stands for. That is the node
 * itself, except for text: every libxml2 node of a text run stands
 * for the run's canonical node, its first one with content.
 *
 * @param node  the node
 * @returns the node that represents it, or NULL if it stands for no
 *          XDM node: an empty text run, or a node of a kind XDM
 *          doesn't have
 */
xmlNodePtr
xp3NodeCanonical(xmlNodePtr node) {
    xmlNodePtr start;
    xmlNodePtr text;

    if (xp3NodeGetKind(node) == XP3_NODE_NONE)
        return(NULL);
    if (!isText(node))
        return(node);
    if (node->parent == NULL)
        return(isEmptyText(node) ? NULL : node);
    start = node;
    while ((start->prev != NULL) && inTextRun(start->prev))
        start = start->prev;
    text = visibleFrom(start);
    return(((text != NULL) && isText(text)) ? text : NULL);
}

/*
 * Tree
 */

/**
 * Get the parent of a node. An attribute's parent is its element,
 * although it is not one of the element's children.
 *
 * @param node  the node
 * @returns the parent, or NULL for the root of a tree
 */
xmlNodePtr
xp3NodeParent(xmlNodePtr node) {
    /* The type is checked first: an xmlNs cast to xmlNode, as libxml2
     * does for XML_NAMESPACE_DECL, has no fields beyond it. */
    if ((node == NULL) || (kindOfType(node->type) == XP3_NODE_NONE) ||
        (node->parent == NULL))
        return(NULL);
    if ((node->type != XML_ATTRIBUTE_NODE) && (!hasChildren(node->parent)))
        return(NULL);
    return(node->parent);
}

/**
 * Get the root of the tree a node is in: usually the document node,
 * but a node without parent is a root as well.
 *
 * @param node  the node
 * @returns the root
 */
xmlNodePtr
xp3NodeRoot(xmlNodePtr node) {
    xmlNodePtr parent;

    if (node == NULL)
        return(NULL);
    while ((parent = xp3NodeParent(node)) != NULL)
        node = parent;
    return(node);
}

/* Whether ancestor is a proper ancestor of node */
static int
isAncestor(xmlNodePtr ancestor, xmlNodePtr node) {
    for (node = xp3NodeParent(node); node != NULL;
         node = xp3NodeParent(node)) {
        if (node == ancestor)
            return(1);
    }
    return(0);
}

/*
 * The node after node in a preorder walk of top's subtree, or NULL at
 * the end of it.
 */
static xmlNodePtr
nextInSubtree(xmlNodePtr node, xmlNodePtr top) {
    xmlNodePtr next = firstChild(node);

    if (next != NULL)
        return(next);
    while ((node != NULL) && (node != top)) {
        next = nextSibling(node);
        if (next != NULL)
            return(next);
        node = xp3NodeParent(node);
    }
    return(NULL);
}

/* The first node after node's subtree in document order */
static xmlNodePtr
nextAfterSubtree(xmlNodePtr node) {
    while (node != NULL) {
        xmlNodePtr next = nextSibling(node);

        if (next != NULL)
            return(next);
        node = xp3NodeParent(node);
    }
    return(NULL);
}

/*
 * The node before node in document order, attributes left out: the
 * last node in the subtree of the previous sibling, or the parent.
 */
static xmlNodePtr
prevInDocument(xmlNodePtr node) {
    xmlNodePtr prev = prevSibling(node);
    xmlNodePtr child;

    if (prev == NULL)
        return(xp3NodeParent(node));
    while ((child = lastChild(prev)) != NULL)
        prev = child;
    return(prev);
}

/* following: everything after the context, its descendants left out.
 * After an attribute, that starts with its element's children. */
static xmlNodePtr
followingNext(xmlNodePtr context, xmlNodePtr cur) {
    xmlNodePtr next;

    if (cur != NULL) {
        next = firstChild(cur);
        return((next != NULL) ? next : nextAfterSubtree(cur));
    }
    if (context->type == XML_ATTRIBUTE_NODE) {
        context = context->parent;
        if (context == NULL)
            return(NULL);
        next = firstChild(context);
        if (next != NULL)
            return(next);
    }
    return(nextAfterSubtree(context));
}

/* preceding: everything before the context, its ancestors left out,
 * in reverse document order. For an attribute, its element's. */
static xmlNodePtr
precedingNext(xmlNodePtr context, xmlNodePtr cur) {
    xmlNodePtr node;

    if (context->type == XML_ATTRIBUTE_NODE) {
        context = context->parent;
        if (context == NULL)
            return(NULL);
    }
    node = (cur != NULL) ? cur : context;
    while (1) {
        xmlNodePtr prev = prevInDocument(node);

        if (prev == NULL)
            return(NULL);
        if ((prev != xp3NodeParent(node)) || (!isAncestor(prev, context)))
            return(prev);
        node = prev;
    }
}

/**
 * Step along an axis: get the node after cur, in the order of the
 * axis. Forward axes are in document order and reverse axes in reverse
 * document order, so the nearest ancestor or preceding sibling comes
 * first. Every call is independent, so steps need no iterator to
 * free.
 *
 * The namespace axis is always empty: static analysis rejects it
 * (XPST0010).
 *
 * @param axis     the axis
 * @param context  the context node
 * @param cur      the node returned by the previous call, or NULL for
 *                 the first node of the axis
 * @returns the next node, or NULL at the end of the axis
 */
xmlNodePtr
xp3AxisNext(xp3Axis axis, xmlNodePtr context, xmlNodePtr cur) {
    if (context == NULL)
        return(NULL);

    switch (axis) {
        case XP3_AXIS_CHILD:
            return((cur == NULL) ? firstChild(context) : nextSibling(cur));
        case XP3_AXIS_DESCENDANT:
            if (cur == NULL)
                return(firstChild(context));
            return(nextInSubtree(cur, context));
        case XP3_AXIS_DESCENDANT_OR_SELF:
            if (cur == NULL)
                return(context);
            return(nextInSubtree(cur, context));
        case XP3_AXIS_ATTRIBUTE:
            if (cur != NULL)
                return(cur->next);
            if (context->type != XML_ELEMENT_NODE)
                return(NULL);
            return((xmlNodePtr) context->properties);
        case XP3_AXIS_SELF:
            return((cur == NULL) ? context : NULL);
        case XP3_AXIS_FOLLOWING_SIBLING:
            return(nextSibling((cur == NULL) ? context : cur));
        case XP3_AXIS_FOLLOWING:
            return(followingNext(context, cur));
        case XP3_AXIS_PARENT:
            return((cur == NULL) ? xp3NodeParent(context) : NULL);
        case XP3_AXIS_ANCESTOR:
            return(xp3NodeParent((cur == NULL) ? context : cur));
        case XP3_AXIS_ANCESTOR_OR_SELF:
            return((cur == NULL) ? context : xp3NodeParent(cur));
        case XP3_AXIS_PRECEDING_SIBLING:
            return(prevSibling((cur == NULL) ? context : cur));
        case XP3_AXIS_PRECEDING:
            return(precedingNext(context, cur));
        case XP3_AXIS_NAMESPACE:
        default:
            break;
    }
    return(NULL);
}

/*
 * Names
 */

/**
 * Get the local part of a node's name: for elements and attributes,
 * and the target of a processing instruction.
 *
 * @param node  the node
 * @returns the local name, or NULL for nodes without a name
 */
const xmlChar *
xp3NodeLocalName(const xmlNode *node) {
    if (node == NULL)
        return(NULL);
    switch (node->type) {
        case XML_ELEMENT_NODE:
        case XML_ATTRIBUTE_NODE:
        case XML_PI_NODE:
            return(node->name);
        default:
            return(NULL);
    }
}

/* The namespace of an element or attribute, NULL for none */
static const xmlNs *
nodeNs(const xmlNode *node) {
    if ((node == NULL) || ((node->type != XML_ELEMENT_NODE) &&
                           (node->type != XML_ATTRIBUTE_NODE)))
        return(NULL);
    return(node->ns);
}

/**
 * Get the namespace URI of an element or attribute.
 *
 * @param node  the node
 * @returns the URI, or NULL if the name is in no namespace or the node
 *          has no name
 */
const xmlChar *
xp3NodeNamespaceUri(const xmlNode *node) {
    const xmlNs *ns = nodeNs(node);

    if ((ns == NULL) || (ns->href == NULL) || (ns->href[0] == 0))
        return(NULL);
    return(ns->href);
}

/**
 * Get the prefix of an element or attribute name.
 *
 * @param node  the node
 * @returns the prefix, or NULL if the name has none
 */
const xmlChar *
xp3NodePrefix(const xmlNode *node) {
    const xmlNs *ns = nodeNs(node);

    return((ns != NULL) ? ns->prefix : NULL);
}

/*
 * Values
 */

/*
 * The string values are built in two passes over the text, first
 * counting the bytes and then copying them, so they are allocated
 * once. out is NULL in the first pass.
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

/* The text of a text run, from its canonical node on */
static size_t
addRunText(xmlChar *out, size_t length, const xmlNode *node) {
    for (; (node != NULL) && inTextRun(node); node = node->next) {
        if (isText(node))
            length = addText(out, length, node->content);
    }
    return(length);
}

/* The text of the text nodes in a subtree, in document order */
static size_t
addSubtreeText(xmlChar *out, size_t length, const xmlNode *top) {
    const xmlNode *node = top->children;

    while (node != NULL) {
        if (isText(node)) {
            length = addText(out, length, node->content);
        } else if ((node->type == XML_ELEMENT_NODE) &&
                   (node->children != NULL)) {
            node = node->children;
            continue;
        }
        while (node->next == NULL) {
            node = node->parent;
            if (node == top)
                return(length);
        }
        node = node->next;
    }
    return(length);
}

/* The text of an attribute value; entity references are skipped */
static size_t
addAttributeText(xmlChar *out, size_t length, const xmlNode *attr) {
    const xmlNode *node;

    for (node = attr->children; node != NULL; node = node->next) {
        if (isText(node))
            length = addText(out, length, node->content);
    }
    return(length);
}

static size_t
addStringValue(xmlChar *out, const xmlNode *node) {
    switch (xp3NodeGetKind(node)) {
        case XP3_NODE_DOCUMENT:
        case XP3_NODE_ELEMENT:
            return(addSubtreeText(out, 0, node));
        case XP3_NODE_ATTRIBUTE:
            return(addAttributeText(out, 0, node));
        case XP3_NODE_TEXT:
            if (node->parent == NULL)
                return(addText(out, 0, node->content));
            node = xp3NodeCanonical((xmlNodePtr) node);
            return((node != NULL) ? addRunText(out, 0, node) : 0);
        case XP3_NODE_COMMENT:
        case XP3_NODE_PI:
            return(addText(out, 0, node->content));
        default:
            return(0);
    }
}

/**
 * Get the string value of a node (XDM 3.1, 5.12): the text of all
 * descendant text nodes of a document or element, the value of an
 * attribute, the text of a text run, and the content of a comment or
 * processing instruction.
 *
 * @param node    the node
 * @param value   set to the string, to be freed with xmlFree()
 * @param length  set to its length in bytes; may be NULL
 * @returns XP3_ERR_OK, XP3_ERR_LIMIT if the string would be longer
 *          than INT32_MAX bytes, or XP3_ERR_NO_MEMORY
 */
xp3ErrCode
xp3NodeStringValue(xmlNodePtr node, xmlChar **value, size_t *length) {
    size_t size;
    xmlChar *str;

    *value = NULL;
    if (length != NULL)
        *length = 0;
    size = addStringValue(NULL, node);
    if (size > INT32_MAX)
        return(XP3_ERR_LIMIT);
    str = xmlMalloc(size + 1);
    if (str == NULL)
        return(XP3_ERR_NO_MEMORY);
    addStringValue(str, node);
    str[size] = 0;
    *value = str;
    if (length != NULL)
        *length = size;
    return(XP3_ERR_OK);
}

/**
 * Get the typed value of a node without schema types (XDM 3.1, 5.14):
 * the string value as xs:string for comments and processing
 * instructions, and as xs:untypedAtomic for all other nodes.
 *
 * @param node   the node
 * @param value  set to the atomic value with one reference
 * @returns XP3_ERR_OK, XP3_ERR_LIMIT or XP3_ERR_NO_MEMORY
 */
xp3ErrCode
xp3NodeTypedValue(xmlNodePtr node, xp3Atomic **value) {
    xp3NodeKind kind = xp3NodeGetKind(node);
    xp3TypeId type;
    xmlChar *str;
    size_t length;
    xp3ErrCode err;

    *value = NULL;
    err = xp3NodeStringValue(node, &str, &length);
    if (err != XP3_ERR_OK)
        return(err);
    if ((kind == XP3_NODE_COMMENT) || (kind == XP3_NODE_PI))
        type = XP3_TYPE_STRING;
    else
        type = XP3_TYPE_UNTYPED_ATOMIC;
    *value = xp3AtomicNewString(type, str, length);
    xmlFree(str);
    return((*value != NULL) ? XP3_ERR_OK : XP3_ERR_NO_MEMORY);
}

/*
 * Document order
 *
 * An open-addressing hash table from node pointers to positions. Each
 * tree gets the positions after those of the trees indexed before it,
 * so nodes of different trees are in a stable order, as XDM 3.1, 2.4
 * requires. A tree is indexed completely or not at all: if memory
 * runs out on the way, the whole table is cleared, and its root being
 * in the table means the tree is.
 */

typedef struct {
    const xmlNode *node;        /* NULL for a free slot */
    size_t position;
} xp3OrderEntry;

struct _xp3DocOrder {
    xp3OrderEntry *entries;
    size_t capacity;            /* a power of two, or 0 */
    size_t used;
    size_t nextPosition;
};

#define XP3_ORDER_MIN_CAPACITY 64

/**
 * Create an empty document order table.
 *
 * @returns the table, or NULL if memory allocation failed
 */
xp3DocOrder *
xp3DocOrderNew(void) {
    xp3DocOrder *order = xmlMalloc(sizeof(*order));

    if (order == NULL)
        return(NULL);
    memset(order, 0, sizeof(*order));
    return(order);
}

static void
orderClear(xp3DocOrder *order) {
    xmlFree(order->entries);
    order->entries = NULL;
    order->capacity = 0;
    order->used = 0;
    order->nextPosition = 0;
}

/**
 * Free a document order table. The nodes are not touched.
 *
 * @param order  the table, may be NULL
 */
void
xp3DocOrderFree(xp3DocOrder *order) {
    if (order == NULL)
        return;
    orderClear(order);
    xmlFree(order);
}

/* The slot of a node: where it is, or the free slot it would go to */
static size_t
orderSlot(const xp3OrderEntry *entries, size_t capacity,
          const xmlNode *node) {
    uint64_t hash = (uint64_t) (uintptr_t) node;
    size_t mask = capacity - 1;
    size_t slot;

    /* The finalizer of MurmurHash3: pointers differ in few bits */
    hash ^= hash >> 33;
    hash *= UINT64_C(0xff51afd7ed558ccd);
    hash ^= hash >> 33;
    slot = (size_t) hash & mask;
    while ((entries[slot].node != NULL) && (entries[slot].node != node))
        slot = (slot + 1) & mask;
    return(slot);
}

static int
orderLookup(const xp3DocOrder *order, const xmlNode *node,
            size_t *position) {
    size_t slot;

    if (order->capacity == 0)
        return(0);
    slot = orderSlot(order->entries, order->capacity, node);
    if (order->entries[slot].node == NULL)
        return(0);
    *position = order->entries[slot].position;
    return(1);
}

/* Keep the table at most half full */
static xp3ErrCode
orderGrow(xp3DocOrder *order) {
    xp3OrderEntry *entries;
    size_t capacity;
    size_t i;

    if (order->used < order->capacity / 2)
        return(XP3_ERR_OK);
    if (order->capacity == 0) {
        capacity = XP3_ORDER_MIN_CAPACITY;
    } else {
        if (order->capacity > SIZE_MAX / 2 / sizeof(xp3OrderEntry))
            return(XP3_ERR_NO_MEMORY);
        capacity = order->capacity * 2;
    }
    entries = xmlMalloc(capacity * sizeof(xp3OrderEntry));
    if (entries == NULL)
        return(XP3_ERR_NO_MEMORY);
    memset(entries, 0, capacity * sizeof(xp3OrderEntry));
    for (i = 0; i < order->capacity; i++) {
        const xp3OrderEntry *entry = &order->entries[i];

        if (entry->node != NULL)
            entries[orderSlot(entries, capacity, entry->node)] = *entry;
    }
    xmlFree(order->entries);
    order->entries = entries;
    order->capacity = capacity;
    return(XP3_ERR_OK);
}

static xp3ErrCode
orderAdd(xp3DocOrder *order, const xmlNode *node) {
    xp3OrderEntry *entry;
    xp3ErrCode err;

    err = orderGrow(order);
    if (err != XP3_ERR_OK)
        return(err);
    entry = &order->entries[orderSlot(order->entries, order->capacity,
                                      node)];
    entry->node = node;
    entry->position = order->nextPosition++;
    order->used++;
    return(XP3_ERR_OK);
}

/* Give every node of the tree under root a position: each element,
 * then its attributes, then its children */
static xp3ErrCode
orderAddTree(xp3DocOrder *order, xmlNodePtr root) {
    xmlNodePtr node;

    for (node = root; node != NULL;
         node = xp3AxisNext(XP3_AXIS_DESCENDANT_OR_SELF, root, node)) {
        xmlAttrPtr attr;
        xp3ErrCode err;

        err = orderAdd(order, node);
        if (err != XP3_ERR_OK)
            return(err);
        if (node->type != XML_ELEMENT_NODE)
            continue;
        for (attr = node->properties; attr != NULL; attr = attr->next) {
            err = orderAdd(order, (xmlNodePtr) attr);
            if (err != XP3_ERR_OK)
                return(err);
        }
    }
    return(XP3_ERR_OK);
}

/*
 * The position of a node, indexing its tree first if needed, and the
 * canonical node it stands for. A node that isn't in the table
 * although its tree is, was added to the tree after it was indexed,
 * or its parent doesn't list it.
 */
static xp3ErrCode
orderPosition(xp3DocOrder *order, xmlNodePtr *node, size_t *position) {
    xmlNodePtr canonical;
    xmlNodePtr root;
    size_t rootPosition;
    xp3ErrCode err;

    if (orderLookup(order, *node, position))
        return(XP3_ERR_OK);
    canonical = xp3NodeCanonical(*node);
    if (canonical == NULL)
        return(XP3_ERR_XPTY0004);
    *node = canonical;
    if (orderLookup(order, canonical, position))
        return(XP3_ERR_OK);
    root = xp3NodeRoot(canonical);
    if (orderLookup(order, root, &rootPosition))
        return(XP3_ERR_XPTY0004);
    err = orderAddTree(order, root);
    if (err != XP3_ERR_OK) {
        orderClear(order);
        return(err);
    }
    if (!orderLookup(order, canonical, position))
        return(XP3_ERR_XPTY0004);
    return(XP3_ERR_OK);
}

/**
 * Compare two nodes in document order. Nodes of different trees are
 * in the order their trees were first seen by this table.
 *
 * @param order   the table
 * @param first   a node
 * @param second  another node
 * @param result  set to a negative number, 0 or a positive number if
 *                first is before, the same as or after second
 * @returns XP3_ERR_OK, XP3_ERR_NO_MEMORY, or XP3_ERR_XPTY0004 if a
 *          node is not an XDM node or its tree changed after the table
 *          indexed it
 */
xp3ErrCode
xp3DocOrderCompare(xp3DocOrder *order, xmlNodePtr first,
                   xmlNodePtr second, int *result) {
    size_t firstPosition;
    size_t secondPosition;
    xp3ErrCode err;

    *result = 0;
    if (first == second)
        return(XP3_ERR_OK);
    err = orderPosition(order, &first, &firstPosition);
    if (err != XP3_ERR_OK)
        return(err);
    err = orderPosition(order, &second, &secondPosition);
    if (err != XP3_ERR_OK)
        return(err);
    if (firstPosition != secondPosition)
        *result = (firstPosition < secondPosition) ? -1 : 1;
    return(XP3_ERR_OK);
}

typedef struct {
    size_t position;
    xmlNodePtr node;
} xp3OrderedNode;

static int
compareOrdered(const void *first, const void *second) {
    size_t firstPosition = ((const xp3OrderedNode *) first)->position;
    size_t secondPosition = ((const xp3OrderedNode *) second)->position;

    if (firstPosition == secondPosition)
        return(0);
    return((firstPosition < secondPosition) ? -1 : 1);
}

/**
 * Sort nodes into document order and remove duplicates, as path
 * expressions and the node set operators do. Input that is already in
 * order, as after most child steps, is not moved. The result holds
 * canonical nodes: every node of a text run becomes the run's node. A
 * single node is only canonicalized, without indexing its tree.
 *
 * @param order  the table
 * @param items  the items, all of them nodes; sorted in place
 * @param count  the number of items; set to the number left
 * @returns XP3_ERR_OK, XP3_ERR_NO_MEMORY, or XP3_ERR_XPTY0004 if an
 *          item is not an XDM node; the items are unchanged on failure
 */
xp3ErrCode
xp3DocOrderSort(xp3DocOrder *order, xp3Item *items, size_t *count) {
    xp3OrderedNode *sorted;
    size_t kept;
    size_t i;
    int inOrder = 1;

    for (i = 0; i < *count; i++) {
        if (items[i].kind != XP3_ITEM_NODE)
            return(XP3_ERR_XPTY0004);
    }
    if (*count == 0)
        return(XP3_ERR_OK);
    if (*count == 1) {
        xmlNodePtr canonical = xp3NodeCanonical(items[0].u.node);

        if (canonical == NULL)
            return(XP3_ERR_XPTY0004);
        items[0].u.node = canonical;
        return(XP3_ERR_OK);
    }

    if (*count > SIZE_MAX / sizeof(xp3OrderedNode))
        return(XP3_ERR_NO_MEMORY);
    sorted = xmlMalloc(*count * sizeof(xp3OrderedNode));
    if (sorted == NULL)
        return(XP3_ERR_NO_MEMORY);
    for (i = 0; i < *count; i++) {
        xp3ErrCode err;

        sorted[i].node = items[i].u.node;
        err = orderPosition(order, &sorted[i].node, &sorted[i].position);
        if (err != XP3_ERR_OK) {
            xmlFree(sorted);
            return(err);
        }
        if ((i > 0) && (sorted[i].position <= sorted[i - 1].position))
            inOrder = 0;
    }
    if (inOrder) {
        /* In order, but maybe not canonical */
        for (i = 0; i < *count; i++)
            items[i].u.node = sorted[i].node;
        xmlFree(sorted);
        return(XP3_ERR_OK);
    }
    qsort(sorted, *count, sizeof(xp3OrderedNode), compareOrdered);

    kept = 0;
    for (i = 0; i < *count; i++) {
        if ((kept > 0) && (sorted[i].position == sorted[i - 1].position))
            continue;
        items[kept++] = xp3ItemFromNode(sorted[i].node);
    }
    *count = kept;
    xmlFree(sorted);
    return(XP3_ERR_OK);
}
