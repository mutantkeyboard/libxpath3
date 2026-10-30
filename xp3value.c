/*
 * xp3value.c: items, atomic values and sequences
 *
 * A value is a sequence in one of four shapes: the shared empty
 * sequence, one item stored inline, an array of items, or a range of
 * consecutive integers that is never expanded. Values and atomic values
 * are reference counted and immutable once built, so they are shared
 * instead of copied (doc/DESIGN.md 4.1). Everything reads sequences
 * through the iterator, so the representation can change later
 * without touching the callers (doc/DESIGN.md 7.1).
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <libxml/tree.h>
#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

#include "private/xp3double.h"
#include "private/xp3value.h"

/* The empty sequence, shared and never freed */
static xp3Value xp3EmptyValue = { 0, XP3_SEQ_EMPTY, { { 0, { NULL } } } };

/*
 * Atomic values
 */

static xp3Atomic *
atomicAlloc(xp3TypeId type, xp3Representation rep) {
    xp3Atomic *atomic;

    if ((xp3TypeRepresentation(type) != rep) || xp3TypeIsAbstract(type))
        return(NULL);
    atomic = xmlMalloc(sizeof(*atomic));
    if (atomic == NULL)
        return(NULL);
    memset(atomic, 0, sizeof(*atomic));
    atomic->refs = 1;
    atomic->type = type;
    return(atomic);
}

/**
 * Create a string-like atomic value: xs:string or a type derived from
 * it, xs:untypedAtomic or xs:anyURI. The text is copied and not
 * checked against the type's facets; casting does that.
 *
 * @param type  the type
 * @param str   the UTF-8 text, not necessarily terminated
 * @param len   its length in bytes
 * @returns the value with one reference, or NULL if memory allocation
 *          failed or the type is not string-like
 */
xp3Atomic *
xp3AtomicNewString(xp3TypeId type, const xmlChar *str, size_t len) {
    xp3Atomic *atomic;

    if ((str == NULL) && (len > 0))
        return(NULL);
    if (len > INT32_MAX)
        return(NULL);
    atomic = atomicAlloc(type, XP3_REP_STRING);
    if (atomic == NULL)
        return(NULL);
    atomic->u.str = xmlStrndup((str != NULL) ? str : BAD_CAST "",
                               (int) len);
    if (atomic->u.str == NULL) {
        xmlFree(atomic);
        return(NULL);
    }
    return(atomic);
}

/**
 * Create an xs:boolean value.
 *
 * @param value  zero for false, anything else for true
 * @returns the value with one reference, or NULL if memory allocation
 *          failed
 */
xp3Atomic *
xp3AtomicNewBoolean(int value) {
    xp3Atomic *atomic = atomicAlloc(XP3_TYPE_BOOLEAN, XP3_REP_BOOLEAN);

    if (atomic != NULL)
        atomic->u.boolean = (value != 0);
    return(atomic);
}

/**
 * Create a decimal value: xs:decimal, xs:integer or a type derived
 * from them. Takes over the decimal, which is freed on failure.
 *
 * @param type  the type
 * @param dec   the decimal; for integer types it must be an integer
 * @returns the value with one reference, or NULL if memory allocation
 *          failed or the type does not fit
 */
xp3Atomic *
xp3AtomicNewDecimal(xp3TypeId type, xp3Decimal *dec) {
    xp3Atomic *atomic = NULL;

    if ((dec != NULL) &&
        (!xp3TypeDerivesFrom(type, XP3_TYPE_INTEGER) ||
         xp3DecimalIsInteger(dec)))
        atomic = atomicAlloc(type, XP3_REP_DECIMAL);
    if (atomic == NULL) {
        xp3DecimalFree(dec);
        return(NULL);
    }
    atomic->u.dec = dec;
    return(atomic);
}

/**
 * Create an xs:integer value from a 64-bit integer.
 *
 * @param value  the integer
 * @returns the value with one reference, or NULL if memory allocation
 *          failed
 */
xp3Atomic *
xp3AtomicNewInteger(int64_t value) {
    xp3Decimal *dec = xp3DecimalFromInt64(value);

    if (dec == NULL)
        return(NULL);
    return(xp3AtomicNewDecimal(XP3_TYPE_INTEGER, dec));
}

/**
 * Create an xs:float value.
 *
 * @param value  the float
 * @returns the value with one reference, or NULL if memory allocation
 *          failed
 */
xp3Atomic *
xp3AtomicNewFloat(float value) {
    xp3Atomic *atomic = atomicAlloc(XP3_TYPE_FLOAT, XP3_REP_FLOAT);

    if (atomic != NULL)
        atomic->u.f = value;
    return(atomic);
}

/**
 * Create an xs:double value.
 *
 * @param value  the double
 * @returns the value with one reference, or NULL if memory allocation
 *          failed
 */
xp3Atomic *
xp3AtomicNewDouble(double value) {
    xp3Atomic *atomic = atomicAlloc(XP3_TYPE_DOUBLE, XP3_REP_DOUBLE);

    if (atomic != NULL)
        atomic->u.d = value;
    return(atomic);
}

/**
 * Create an xs:hexBinary or xs:base64Binary value. The bytes are
 * copied.
 *
 * @param type  the type
 * @param data  the bytes, may be NULL if len is 0
 * @param len   the number of bytes
 * @returns the value with one reference, or NULL if memory allocation
 *          failed or the type is not binary
 */
xp3Atomic *
xp3AtomicNewBinary(xp3TypeId type, const unsigned char *data, size_t len) {
    xp3Atomic *atomic;

    if ((data == NULL) && (len > 0))
        return(NULL);
    atomic = atomicAlloc(type, XP3_REP_BINARY);
    if (atomic == NULL)
        return(NULL);
    if (len > 0) {
        atomic->u.bin.data = xmlMalloc(len);
        if (atomic->u.bin.data == NULL) {
            xmlFree(atomic);
            return(NULL);
        }
        memcpy(atomic->u.bin.data, data, len);
    }
    atomic->u.bin.len = len;
    return(atomic);
}

/**
 * Create an xs:QName value. The strings are copied.
 *
 * @param uri     the namespace URI, or NULL or "" for no namespace
 * @param prefix  the prefix, or NULL or "" for none
 * @param local   the local name
 * @returns the value with one reference, or NULL if memory allocation
 *          failed or local is NULL
 */
xp3Atomic *
xp3AtomicNewQName(const xmlChar *uri, const xmlChar *prefix,
                  const xmlChar *local) {
    xp3Atomic *atomic;

    if (local == NULL)
        return(NULL);
    atomic = atomicAlloc(XP3_TYPE_QNAME, XP3_REP_QNAME);
    if (atomic == NULL)
        return(NULL);
    atomic->u.qname.local = xmlStrdup(local);
    if ((uri != NULL) && (*uri != 0))
        atomic->u.qname.uri = xmlStrdup(uri);
    if ((prefix != NULL) && (*prefix != 0))
        atomic->u.qname.prefix = xmlStrdup(prefix);
    if ((atomic->u.qname.local == NULL) ||
        ((uri != NULL) && (*uri != 0) && (atomic->u.qname.uri == NULL)) ||
        ((prefix != NULL) && (*prefix != 0) &&
         (atomic->u.qname.prefix == NULL))) {
        xp3AtomicUnref(atomic);
        return(NULL);
    }
    return(atomic);
}

/**
 * Add a reference to an atomic value.
 *
 * @param atomic  the value, may be NULL
 * @returns atomic
 */
xp3Atomic *
xp3AtomicRef(xp3Atomic *atomic) {
    if ((atomic != NULL) && (atomic->refs >= 0))
        atomic->refs++;
    return(atomic);
}

static void
atomicFree(xp3Atomic *atomic);

/* The length of a string, counted up to just past max */
static size_t
boundedLength(const xmlChar *str, size_t max) {
    size_t len = 0;

    if (str == NULL)
        return(0);
    while ((len <= max) && (str[len] != 0))
        len++;
    return(len);
}

/**
 * Measure the payload of an atomic value: the bytes of a string, a
 * binary value or a QName, and the digits of a decimal. Values of the
 * other types have a fixed size and measure 0. Strings are counted
 * only up to max, so measuring one costs at most max + 1 steps.
 *
 * @param atomic  the value
 * @param max     the size that matters
 * @returns the size, or a size above max if it is larger
 */
size_t
xp3AtomicSize(const xp3Atomic *atomic, size_t max) {
    size_t size;

    switch (xp3TypeRepresentation(atomic->type)) {
        case XP3_REP_STRING:
            return(boundedLength(atomic->u.str, max));
        case XP3_REP_DECIMAL:
            return(xp3DecimalDigits(atomic->u.dec));
        case XP3_REP_BINARY:
            return(atomic->u.bin.len);
        case XP3_REP_QNAME:
            size = boundedLength(atomic->u.qname.uri, max);
            if (size <= max)
                size += boundedLength(atomic->u.qname.prefix, max - size);
            if (size <= max)
                size += boundedLength(atomic->u.qname.local, max - size);
            return(size);
        default:
            return(0);
    }
}

/**
 * Release a reference to an atomic value, freeing it with the last
 * one. NULL is ignored.
 *
 * @param atomic  the value
 */
void
xp3AtomicUnref(xp3Atomic *atomic) {
    if ((atomic == NULL) || (atomic->refs < 0) || (--atomic->refs > 0))
        return;
    atomicFree(atomic);
}

static void
atomicFree(xp3Atomic *atomic) {
    switch (xp3TypeRepresentation(atomic->type)) {
        case XP3_REP_STRING:
            xmlFree(atomic->u.str);
            break;
        case XP3_REP_DECIMAL:
            xp3DecimalFree(atomic->u.dec);
            break;
        case XP3_REP_BINARY:
            xmlFree(atomic->u.bin.data);
            break;
        case XP3_REP_QNAME:
            xmlFree(atomic->u.qname.uri);
            xmlFree(atomic->u.qname.prefix);
            xmlFree(atomic->u.qname.local);
            break;
        default:
            break;
    }
    xmlFree(atomic);
}

/*
 * Items
 */

/**
 * Make an item for a node. Nodes are borrowed: the document must
 * outlive the item.
 *
 * @param node  the node
 * @returns the item
 */
xp3Item
xp3ItemFromNode(xmlNodePtr node) {
    xp3Item item;

    item.kind = XP3_ITEM_NODE;
    item.u.node = node;
    return(item);
}

/**
 * Make an item for an atomic value. Takes over the reference.
 *
 * @param atomic  the value
 * @returns the item
 */
xp3Item
xp3ItemFromAtomic(xp3Atomic *atomic) {
    xp3Item item;

    item.kind = XP3_ITEM_ATOMIC;
    item.u.atomic = atomic;
    return(item);
}

/**
 * Add a reference to what an item holds.
 *
 * @param item  the item
 * @returns item
 */
xp3Item
xp3ItemRef(xp3Item item) {
    if (item.kind == XP3_ITEM_ATOMIC)
        xp3AtomicRef(item.u.atomic);
    return(item);
}

/**
 * Release the reference an item holds.
 *
 * @param item  the item
 */
void
xp3ItemUnref(xp3Item item) {
    if (item.kind == XP3_ITEM_ATOMIC)
        xp3AtomicUnref(item.u.atomic);
}

/*
 * Sequences
 */

static xp3Value *
valueAlloc(xp3SeqShape shape) {
    xp3Value *value = xmlMalloc(sizeof(*value));

    if (value == NULL)
        return(NULL);
    memset(value, 0, sizeof(*value));
    value->refs = 1;
    value->shape = shape;
    return(value);
}

/**
 * Get the empty sequence. It is shared, and references to it are not
 * counted, so getting it never fails.
 *
 * @returns the empty sequence
 */
xp3Value *
xp3ValueEmpty(void) {
    return(&xp3EmptyValue);
}

/**
 * Make a sequence of one item. Takes over the item's reference, also
 * on failure.
 *
 * @param item  the item
 * @returns the sequence with one reference, or NULL if memory
 *          allocation failed
 */
xp3Value *
xp3ValueSingle(xp3Item item) {
    xp3Value *value = valueAlloc(XP3_SEQ_SINGLE);

    if (value == NULL) {
        xp3ItemUnref(item);
        return(NULL);
    }
    value->u.single = item;
    return(value);
}

/**
 * Make the sequence of integers from first to last, as the to
 * operator does, without expanding it. Bounds beyond 64 bits are the
 * caller's to reject with XP3_ERR_LIMIT (doc/DESIGN.md 4.1).
 *
 * @param first  the first integer
 * @param last   the last integer; the sequence is empty if it is less
 *               than first
 * @param res    where to store the sequence
 * @returns XP3_ERR_OK, XP3_ERR_LIMIT if the sequence has more than
 *          2^64 - 1 items, or XP3_ERR_NO_MEMORY
 */
xp3ErrCode
xp3ValueRange(int64_t first, int64_t last, xp3Value **res) {
    xp3Value *value;

    *res = NULL;
    if (last < first) {
        *res = xp3ValueEmpty();
        return(XP3_ERR_OK);
    }
    /* The count must fit in 64 bits. */
    if ((uint64_t) last - (uint64_t) first == UINT64_MAX)
        return(XP3_ERR_LIMIT);
    value = valueAlloc(XP3_SEQ_RANGE);
    if (value == NULL)
        return(XP3_ERR_NO_MEMORY);
    value->u.range.first = first;
    value->u.range.last = last;
    *res = value;
    return(XP3_ERR_OK);
}

/**
 * Add a reference to a sequence.
 *
 * @param value  the sequence, may be NULL
 * @returns value
 */
xp3Value *
xp3ValueRef(xp3Value *value) {
    if ((value != NULL) && (value != &xp3EmptyValue) && (value->refs >= 0))
        value->refs++;
    return(value);
}

/**
 * Release a reference to a sequence, freeing it and releasing its
 * items with the last one. NULL is ignored.
 *
 * @param value  the sequence
 */
void
xp3ValueUnref(xp3Value *value) {
    size_t i;

    if ((value == NULL) || (value == &xp3EmptyValue) || (value->refs < 0) ||
        (--value->refs > 0))
        return;
    switch (value->shape) {
        case XP3_SEQ_SINGLE:
            xp3ItemUnref(value->u.single);
            break;
        case XP3_SEQ_ARRAY:
            for (i = 0; i < value->u.array.count; i++)
                xp3ItemUnref(value->u.array.items[i]);
            xmlFree(value->u.array.items);
            break;
        default:
            break;
    }
    xmlFree(value);
}

/**
 * Get the number of items in a sequence.
 *
 * @param value  the sequence
 * @returns the number of items, 0 for NULL
 */
uint64_t
xp3ValueCount(const xp3Value *value) {
    if (value == NULL)
        return(0);
    switch (value->shape) {
        case XP3_SEQ_SINGLE:
            return(1);
        case XP3_SEQ_ARRAY:
            return(value->u.array.count);
        case XP3_SEQ_RANGE:
            return((uint64_t) value->u.range.last -
                   (uint64_t) value->u.range.first + 1);
        default:
            return(0);
    }
}

/**
 * Get an item of a sequence by position. Items of a range are created
 * on demand.
 *
 * @param value  the sequence
 * @param index  0-based position
 * @param item   where to store the item, with a reference the caller
 *               releases
 * @returns 1 if there is an item, 0 if index is past the end, -1 if
 *          memory allocation failed
 */
int
xp3ValueItemAt(const xp3Value *value, uint64_t index, xp3Item *item) {
    xp3Atomic *atomic;

    if (index >= xp3ValueCount(value))
        return(0);
    switch (value->shape) {
        case XP3_SEQ_SINGLE:
            *item = xp3ItemRef(value->u.single);
            return(1);
        case XP3_SEQ_ARRAY:
            *item = xp3ItemRef(value->u.array.items[index]);
            return(1);
        case XP3_SEQ_RANGE:
            atomic = xp3AtomicNewInteger(
                (int64_t) ((uint64_t) value->u.range.first + index));
            if (atomic == NULL)
                return(-1);
            *item = xp3ItemFromAtomic(atomic);
            return(1);
        default:
            return(0);
    }
}

/**
 * Start iterating over a sequence. The iterator holds no reference,
 * so the sequence must stay alive while it is used.
 *
 * @param iter   the iterator
 * @param value  the sequence
 */
void
xp3IterInit(xp3Iter *iter, const xp3Value *value) {
    iter->value = value;
    iter->pos = 0;
}

/**
 * Get the next item.
 *
 * @param iter  the iterator
 * @param item  where to store the item, with a reference the caller
 *              releases
 * @returns 1 if there was an item, 0 at the end, -1 if memory
 *          allocation failed; the same item is tried again next time
 */
int
xp3IterNext(xp3Iter *iter, xp3Item *item) {
    int ret = xp3ValueItemAt(iter->value, iter->pos, item);

    if (ret > 0)
        iter->pos++;
    return(ret);
}

/*
 * Builder
 */

/**
 * Start building a sequence of at most XP3_MAX_SEQUENCE_ITEMS items.
 * Setting builder->max afterwards changes the limit.
 *
 * @param builder  the builder
 */
void
xp3SeqBuilderInit(xp3SeqBuilder *builder) {
    memset(builder, 0, sizeof(*builder));
    builder->max = XP3_MAX_SEQUENCE_ITEMS;
}

/**
 * Add an item to a sequence being built. Takes over the item's
 * reference, also on failure.
 *
 * @param builder  the builder
 * @param item     the item
 * @returns XP3_ERR_OK, XP3_ERR_LIMIT past builder->max items or
 *          builder->maxBytes bytes, or XP3_ERR_NO_MEMORY; the builder
 *          stays usable after a failure
 */
xp3ErrCode
xp3SeqBuilderAdd(xp3SeqBuilder *builder, xp3Item item) {
    if (builder->count >= builder->max) {
        xp3ItemUnref(item);
        return(XP3_ERR_LIMIT);
    }
    if ((builder->maxBytes > 0) && (item.kind == XP3_ITEM_ATOMIC)) {
        size_t size = xp3AtomicSize(item.u.atomic,
                                    builder->maxBytes - builder->bytes);

        if (size > builder->maxBytes - builder->bytes) {
            xp3ItemUnref(item);
            return(XP3_ERR_LIMIT);
        }
        builder->bytes += size;
    }
    if (builder->count >= builder->cap) {
        size_t cap = (builder->cap == 0) ? 8 : builder->cap * 2;
        xp3Item *items;

        if ((cap > builder->max) || (cap < builder->cap))
            cap = builder->max;
        items = xmlRealloc(builder->items, cap * sizeof(xp3Item));
        if (items == NULL) {
            xp3ItemUnref(item);
            return(XP3_ERR_NO_MEMORY);
        }
        builder->items = items;
        builder->cap = cap;
    }
    builder->items[builder->count++] = item;
    return(XP3_ERR_OK);
}

/**
 * Add all items of a sequence to a sequence being built, with new
 * references. Ranges are expanded.
 *
 * @param builder  the builder
 * @param value    the sequence
 * @returns XP3_ERR_OK, XP3_ERR_LIMIT or XP3_ERR_NO_MEMORY; on failure
 *          some of the items may have been added
 */
xp3ErrCode
xp3SeqBuilderAddValue(xp3SeqBuilder *builder, const xp3Value *value) {
    xp3Iter iter;
    xp3Item item;
    int ret;

    if (xp3ValueCount(value) > builder->max - builder->count)
        return(XP3_ERR_LIMIT);
    xp3IterInit(&iter, value);
    while ((ret = xp3IterNext(&iter, &item)) > 0) {
        int err = xp3SeqBuilderAdd(builder, item);

        if (err != XP3_ERR_OK)
            return(err);
    }
    return((ret < 0) ? XP3_ERR_NO_MEMORY : XP3_ERR_OK);
}

/**
 * Turn the collected items into a sequence: empty, single or array.
 * The builder is empty afterwards, also on failure.
 *
 * @param builder  the builder
 * @returns the sequence with one reference, or NULL if memory
 *          allocation failed
 */
xp3Value *
xp3SeqBuilderFinish(xp3SeqBuilder *builder) {
    xp3Value *value;

    if (builder->count == 0) {
        xp3SeqBuilderClear(builder);
        return(xp3ValueEmpty());
    }
    if (builder->count == 1) {
        value = xp3ValueSingle(builder->items[0]);
        xmlFree(builder->items);
        xp3SeqBuilderInit(builder);
        return(value);
    }
    value = valueAlloc(XP3_SEQ_ARRAY);
    if (value == NULL) {
        xp3SeqBuilderClear(builder);
        return(NULL);
    }
    value->u.array.items = builder->items;
    value->u.array.count = builder->count;
    xp3SeqBuilderInit(builder);
    return(value);
}

/**
 * Abandon a sequence being built, releasing its items.
 *
 * @param builder  the builder
 */
void
xp3SeqBuilderClear(xp3SeqBuilder *builder) {
    size_t i;

    for (i = 0; i < builder->count; i++)
        xp3ItemUnref(builder->items[i]);
    xmlFree(builder->items);
    xp3SeqBuilderInit(builder);
}

/*
 * Strings
 */

static const char xp3HexDigits[] = "0123456789ABCDEF";
static const char xp3Base64Digits[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* xs:hexBinary with uppercase digits, xs:base64Binary with padding */
static xmlChar *
binaryToString(const xp3Atomic *atomic) {
    const unsigned char *data = atomic->u.bin.data;
    size_t len = atomic->u.bin.len;
    xmlChar *out;
    size_t pos = 0;
    size_t i;

    if (atomic->type == XP3_TYPE_HEX_BINARY) {
        if (len > (SIZE_MAX - 1) / 2)
            return(NULL);
        out = xmlMalloc(len * 2 + 1);
        if (out == NULL)
            return(NULL);
        for (i = 0; i < len; i++) {
            out[pos++] = xp3HexDigits[data[i] >> 4];
            out[pos++] = xp3HexDigits[data[i] & 15];
        }
    } else {
        if (len / 3 > (SIZE_MAX - 5) / 4)
            return(NULL);
        out = xmlMalloc((len + 2) / 3 * 4 + 1);
        if (out == NULL)
            return(NULL);
        for (i = 0; i + 2 < len; i += 3) {
            unsigned long bits = ((unsigned long) data[i] << 16) |
                                 ((unsigned long) data[i + 1] << 8) |
                                 data[i + 2];

            out[pos++] = xp3Base64Digits[(bits >> 18) & 63];
            out[pos++] = xp3Base64Digits[(bits >> 12) & 63];
            out[pos++] = xp3Base64Digits[(bits >> 6) & 63];
            out[pos++] = xp3Base64Digits[bits & 63];
        }
        if (i < len) {
            unsigned long bits = (unsigned long) data[i] << 16;

            if (i + 1 < len)
                bits |= (unsigned long) data[i + 1] << 8;
            out[pos++] = xp3Base64Digits[(bits >> 18) & 63];
            out[pos++] = xp3Base64Digits[(bits >> 12) & 63];
            out[pos++] = (i + 1 < len) ? xp3Base64Digits[(bits >> 6) & 63] :
                                         '=';
            out[pos++] = '=';
        }
    }
    out[pos] = 0;
    return(out);
}

/* "prefix:local", or "local" without a prefix */
static xmlChar *
qnameToString(const xp3Atomic *atomic) {
    const xmlChar *prefix = atomic->u.qname.prefix;
    const xmlChar *local = atomic->u.qname.local;
    size_t prefixLen;
    size_t localLen;
    xmlChar *out;

    if (prefix == NULL)
        return(xmlStrdup(local));
    prefixLen = strlen((const char *) prefix);
    localLen = strlen((const char *) local);
    out = xmlMalloc(prefixLen + localLen + 2);
    if (out == NULL)
        return(NULL);
    memcpy(out, prefix, prefixLen);
    out[prefixLen] = ':';
    memcpy(out + prefixLen + 1, local, localLen + 1);
    return(out);
}

/**
 * Convert an atomic value to its canonical string, as casting to
 * xs:string does (F&O 3.1, 19.1.2): "true" or "false", the shortest
 * form of numbers, uppercase hex digits, padded base64, and
 * "prefix:local" for QNames.
 *
 * @param atomic  the value
 * @returns the string, to be freed with xmlFree(), or NULL if memory
 *          allocation failed or the type has no strings yet (dates,
 *          times and durations come with Part 10)
 */
xmlChar *
xp3AtomicToString(const xp3Atomic *atomic) {
    if (atomic == NULL)
        return(NULL);
    switch (xp3TypeRepresentation(atomic->type)) {
        case XP3_REP_STRING:
            return(xmlStrdup(atomic->u.str));
        case XP3_REP_BOOLEAN:
            return(xmlStrdup(BAD_CAST (atomic->u.boolean ? "true" :
                                                           "false")));
        case XP3_REP_DECIMAL:
            return(xp3DecimalToString(atomic->u.dec));
        case XP3_REP_FLOAT:
            return(xp3FloatToString(atomic->u.f));
        case XP3_REP_DOUBLE:
            return(xp3DoubleToString(atomic->u.d));
        case XP3_REP_BINARY:
            return(binaryToString(atomic));
        case XP3_REP_QNAME:
            return(qnameToString(atomic));
        default:
            return(NULL);
    }
}

/*
 * Constants
 *
 * A constant is a value that compiled expressions keep and that several
 * threads may evaluate at once. Its count and those of its atomic
 * values are XP3_REFS_CONSTANT, which the reference functions leave
 * alone, so using a constant writes no memory. Only
 * xp3ValueFreeConstant() frees it.
 */

/* A copy of an atomic value with a count of its own */
static xp3Atomic *
atomicCopy(const xp3Atomic *atomic) {
    xp3Decimal *dec;

    switch (xp3TypeRepresentation(atomic->type)) {
        case XP3_REP_STRING:
            return(xp3AtomicNewString(atomic->type, atomic->u.str,
                                      strlen((const char *) atomic->u.str)));
        case XP3_REP_BOOLEAN:
            return(xp3AtomicNewBoolean(atomic->u.boolean));
        case XP3_REP_DECIMAL:
            if (xp3DecimalCopy(atomic->u.dec, &dec) != XP3_ERR_OK)
                return(NULL);
            return(xp3AtomicNewDecimal(atomic->type, dec));
        case XP3_REP_FLOAT:
            return(xp3AtomicNewFloat(atomic->u.f));
        case XP3_REP_DOUBLE:
            return(xp3AtomicNewDouble(atomic->u.d));
        case XP3_REP_BINARY:
            return(xp3AtomicNewBinary(atomic->type, atomic->u.bin.data,
                                      atomic->u.bin.len));
        case XP3_REP_QNAME:
            return(xp3AtomicNewQName(atomic->u.qname.uri,
                                     atomic->u.qname.prefix,
                                     atomic->u.qname.local));
        default:
            return(NULL);
    }
}

/* An item of a constant: an atomic value copied and made constant */
static int
constantItem(xp3Item item, xp3Item *copy) {
    xp3Atomic *atomic;

    if (item.kind != XP3_ITEM_ATOMIC)
        return(-1);
    atomic = atomicCopy(item.u.atomic);
    if (atomic == NULL)
        return(-1);
    atomic->refs = XP3_REFS_CONSTANT;
    *copy = xp3ItemFromAtomic(atomic);
    return(0);
}

/**
 * Whether a sequence holds atomic values only, which
 * xp3ValueMakeConstant() needs: nodes, functions, maps and arrays
 * can't be constants.
 *
 * @param value  the sequence
 * @returns 1 if every item is atomic, also for the empty sequence and
 *          ranges, otherwise 0
 */
int
xp3ValueIsAtomic(const xp3Value *value) {
    size_t i;

    switch (value->shape) {
        case XP3_SEQ_SINGLE:
            return(value->u.single.kind == XP3_ITEM_ATOMIC);
        case XP3_SEQ_ARRAY:
            for (i = 0; i < value->u.array.count; i++) {
                if (value->u.array.items[i].kind != XP3_ITEM_ATOMIC)
                    return(0);
            }
            return(1);
        default:
            return(1);
    }
}

/**
 * Make a constant from a value: a deep copy whose atomic values belong
 * to it alone, and whose counts never change, so that threads can
 * share it. Values with nodes, functions, maps or arrays can't be
 * constants (xp3ValueIsAtomic()).
 *
 * @param value  the value, atomic values only
 * @returns the constant, to be freed with xp3ValueFreeConstant(), or
 *          NULL if the value has other items or memory allocation
 *          failed
 */
xp3Value *
xp3ValueMakeConstant(const xp3Value *value) {
    xp3Value *copy;
    size_t i;

    if (value == NULL)
        return(NULL);
    if (value->shape == XP3_SEQ_EMPTY)
        return(&xp3EmptyValue);
    copy = valueAlloc(value->shape);
    if (copy == NULL)
        return(NULL);
    copy->refs = XP3_REFS_CONSTANT;
    switch (value->shape) {
        case XP3_SEQ_SINGLE:
            if (constantItem(value->u.single, &copy->u.single) < 0) {
                xmlFree(copy);
                return(NULL);
            }
            break;
        case XP3_SEQ_ARRAY:
            copy->u.array.items = xmlMalloc(value->u.array.count *
                                            sizeof(xp3Item));
            if (copy->u.array.items == NULL) {
                xmlFree(copy);
                return(NULL);
            }
            for (i = 0; i < value->u.array.count; i++) {
                if (constantItem(value->u.array.items[i],
                                 &copy->u.array.items[i]) < 0) {
                    copy->u.array.count = i;
                    xp3ValueFreeConstant(copy);
                    return(NULL);
                }
            }
            copy->u.array.count = value->u.array.count;
            break;
        default:
            copy->u.range = value->u.range;
            break;
    }
    return(copy);
}

/* Whether an item is an atomic value of a constant */
static int
isConstantItem(xp3Item item) {
    return((item.kind == XP3_ITEM_ATOMIC) &&
           (item.u.atomic->refs == XP3_REFS_CONSTANT));
}

/* An item with its own count: a constant atomic value copied, any
 * other item with a new reference */
static int
detachItem(xp3Item item, xp3Item *copy) {
    xp3Atomic *atomic;

    if (!isConstantItem(item)) {
        *copy = xp3ItemRef(item);
        return(0);
    }
    atomic = atomicCopy(item.u.atomic);
    if (atomic == NULL)
        return(-1);
    *copy = xp3ItemFromAtomic(atomic);
    return(0);
}

/**
 * Make a value independent of the constants of a compiled expression,
 * so that it can outlive the expression: constants, and atomic values
 * of constants, are replaced by copies. Takes over the reference.
 *
 * @param value  the value
 * @returns the value itself if it has no constant parts, otherwise a
 *          copy with one reference; NULL if memory allocation failed
 */
xp3Value *
xp3ValueDetach(xp3Value *value) {
    xp3Value *copy;
    size_t count;
    size_t i;
    int found = (value->refs == XP3_REFS_CONSTANT);

    switch (value->shape) {
        case XP3_SEQ_SINGLE:
            found |= isConstantItem(value->u.single);
            break;
        case XP3_SEQ_ARRAY:
            for (i = 0; (i < value->u.array.count) && !found; i++)
                found = isConstantItem(value->u.array.items[i]);
            break;
        default:
            break;
    }
    if (!found)
        return(value);

    copy = valueAlloc(value->shape);
    if (copy == NULL) {
        xp3ValueUnref(value);
        return(NULL);
    }
    switch (value->shape) {
        case XP3_SEQ_SINGLE:
            if (detachItem(value->u.single, &copy->u.single) < 0) {
                xmlFree(copy);
                copy = NULL;
            }
            break;
        case XP3_SEQ_ARRAY:
            count = value->u.array.count;
            copy->u.array.items = xmlMalloc(count * sizeof(xp3Item));
            if (copy->u.array.items == NULL) {
                xmlFree(copy);
                copy = NULL;
                break;
            }
            for (i = 0; i < count; i++) {
                if (detachItem(value->u.array.items[i],
                               &copy->u.array.items[i]) < 0) {
                    copy->u.array.count = i;
                    xp3ValueUnref(copy);
                    copy = NULL;
                    break;
                }
            }
            if (copy != NULL)
                copy->u.array.count = count;
            break;
        default:
            copy->u.range = value->u.range;
            break;
    }
    xp3ValueUnref(value);
    return(copy);
}

/**
 * Free a constant made by xp3ValueMakeConstant(). NULL and the shared
 * empty sequence are ignored.
 *
 * @param value  the constant
 */
void
xp3ValueFreeConstant(xp3Value *value) {
    size_t i;

    if ((value == NULL) || (value == &xp3EmptyValue))
        return;
    switch (value->shape) {
        case XP3_SEQ_SINGLE:
            atomicFree(value->u.single.u.atomic);
            break;
        case XP3_SEQ_ARRAY:
            for (i = 0; i < value->u.array.count; i++)
                atomicFree(value->u.array.items[i].u.atomic);
            xmlFree(value->u.array.items);
            break;
        default:
            break;
    }
    xmlFree(value);
}
