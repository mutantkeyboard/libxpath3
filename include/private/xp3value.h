/**
 * @file
 *
 * @brief Items, atomic values and sequences (internal)
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XP3VALUE_H
#define XP3_XP3VALUE_H

#include <stddef.h>
#include <stdint.h>

#include <libxml/tree.h>
#include <libxml/xmlstring.h>

#include <libxpath3/xpath3.h>
#include "private/xp3decimal.h"
#include "private/xp3error.h"
#include "private/xp3types.h"

/*
 * Ownership. Atomic values and sequences are reference counted and
 * immutable once built (doc/DESIGN.md 4.1). An xp3Item that holds an
 * atomic value owns one reference to it. Functions document whether
 * they take over a reference ("takes") or add one. Nodes are borrowed
 * from libxml2 and never counted. Counts are not atomic: a value
 * belongs to one evaluation at a time, as libxml2's objects do.
 */

typedef struct _xp3Atomic xp3Atomic;
typedef struct _xp3Value xp3Value;

/* An item, passed by value. Its kind is xp3ItemKind from xpath3.h:
 * nodes are borrowed xmlNode pointers and atomic values are counted;
 * functions, maps and arrays come with Part 12. */
typedef struct {
    xp3ItemKind kind;
    union {
        xmlNodePtr node;
        xp3Atomic *atomic;
        void *reserved;
    } u;
} xp3Item;

/*
 * An atomic value: a type and a payload, stored as the type's
 * representation says (xp3TypeRepresentation()). Durations and dates
 * come with Part 10.
 */
struct _xp3Atomic {
    int refs;
    xp3TypeId type;
    union {
        xmlChar *str;           /* XP3_REP_STRING */
        int boolean;            /* XP3_REP_BOOLEAN */
        xp3Decimal *dec;        /* XP3_REP_DECIMAL */
        float f;                /* XP3_REP_FLOAT */
        double d;               /* XP3_REP_DOUBLE */
        struct {
            unsigned char *data;
            size_t len;
        } bin;                  /* XP3_REP_BINARY */
        struct {
            xmlChar *uri;       /* NULL for no namespace */
            xmlChar *prefix;    /* NULL for no prefix */
            xmlChar *local;
        } qname;                /* XP3_REP_QNAME */
    } u;
};

/* Shapes of a sequence (doc/DESIGN.md 4.1) */
typedef enum {
    XP3_SEQ_EMPTY = 0,          /* the shared empty sequence */
    XP3_SEQ_SINGLE,             /* one item, stored inline */
    XP3_SEQ_ARRAY,              /* two or more items */
    XP3_SEQ_RANGE               /* consecutive integers, not expanded */
} xp3SeqShape;

/* A value, which in XDM is always a sequence */
struct _xp3Value {
    int refs;                   /* ignored for the shared empty value */
    xp3SeqShape shape;
    union {
        xp3Item single;
        struct {
            xp3Item *items;
            size_t count;
        } array;
        struct {
            int64_t first;
            int64_t last;       /* last >= first */
        } range;
    } u;
};

/* Iterator over a sequence. It does not hold a reference: the caller
 * keeps the value alive while iterating. */
typedef struct {
    const xp3Value *value;
    uint64_t pos;
} xp3Iter;

/*
 * Default limit on the number of items a builder collects, the same as
 * libxml2's limit on node-sets (XPATH_MAX_NODESET_LENGTH). Dynamic
 * contexts can change it. Ranges are not limited, because they are
 * never expanded.
 */
#define XP3_MAX_SEQUENCE_ITEMS 10000000

/* Collects items into a new sequence */
typedef struct {
    xp3Item *items;
    size_t count;
    size_t cap;
    size_t max;                 /* most items allowed, see
                                 * xp3SeqBuilderInit() */
} xp3SeqBuilder;

/*
 * Atomic values
 */
xp3Atomic *
xp3AtomicNewString(xp3TypeId type, const xmlChar *str, size_t len);

xp3Atomic *
xp3AtomicNewBoolean(int value);

xp3Atomic *
xp3AtomicNewDecimal(xp3TypeId type, xp3Decimal *dec);

xp3Atomic *
xp3AtomicNewInteger(int64_t value);

xp3Atomic *
xp3AtomicNewFloat(float value);

xp3Atomic *
xp3AtomicNewDouble(double value);

xp3Atomic *
xp3AtomicNewBinary(xp3TypeId type, const unsigned char *data, size_t len);

xp3Atomic *
xp3AtomicNewQName(const xmlChar *uri, const xmlChar *prefix,
                  const xmlChar *local);

xp3Atomic *
xp3AtomicRef(xp3Atomic *atomic);

void
xp3AtomicUnref(xp3Atomic *atomic);

/*
 * Items
 */
xp3Item
xp3ItemFromNode(xmlNodePtr node);

xp3Item
xp3ItemFromAtomic(xp3Atomic *atomic);

xp3Item
xp3ItemRef(xp3Item item);

void
xp3ItemUnref(xp3Item item);

/*
 * Sequences
 */
xp3Value *
xp3ValueEmpty(void);

xp3Value *
xp3ValueSingle(xp3Item item);

xp3ErrCode
xp3ValueRange(int64_t first, int64_t last, xp3Value **res);

xp3Value *
xp3ValueRef(xp3Value *value);

xmlChar *
xp3AtomicToString(const xp3Atomic *atomic);

void
xp3ValueUnref(xp3Value *value);

uint64_t
xp3ValueCount(const xp3Value *value);

int
xp3ValueItemAt(const xp3Value *value, uint64_t index, xp3Item *item);

void
xp3IterInit(xp3Iter *iter, const xp3Value *value);

int
xp3IterNext(xp3Iter *iter, xp3Item *item);

void
xp3SeqBuilderInit(xp3SeqBuilder *builder);

xp3ErrCode
xp3SeqBuilderAdd(xp3SeqBuilder *builder, xp3Item item);

xp3ErrCode
xp3SeqBuilderAddValue(xp3SeqBuilder *builder, const xp3Value *value);

xp3Value *
xp3SeqBuilderFinish(xp3SeqBuilder *builder);

void
xp3SeqBuilderClear(xp3SeqBuilder *builder);

#endif /* XP3_XP3VALUE_H */
