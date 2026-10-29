/**
 * @file
 *
 * @brief Built-in atomic types (internal)
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XP3TYPES_H
#define XP3_XP3TYPES_H

#include <libxml/xmlstring.h>

#include <libxpath3/xpath3.h>

/*
 * The built-in atomic types are xp3AtomicType in xpath3.h, in an order
 * where every type comes after its parent. Internal code calls them
 * xp3TypeId.
 */
typedef xp3AtomicType xp3TypeId;

/* Number of types, one more than the last; not a type */
#define XP3_TYPE_COUNT 46

/*
 * How a value of a type is stored. Derived types share the
 * representation of the primitive type they derive from (DESIGN 4.2).
 */
typedef enum {
    XP3_REP_NONE = 0,           /* xs:anyAtomicType: no values */
    XP3_REP_STRING,             /* UTF-8 string */
    XP3_REP_BOOLEAN,
    XP3_REP_DECIMAL,            /* xp3Decimal, also for all integers */
    XP3_REP_FLOAT,
    XP3_REP_DOUBLE,
    XP3_REP_DURATION,           /* Part 10 */
    XP3_REP_DATE_TIME,          /* Part 10 */
    XP3_REP_BINARY,             /* bytes */
    XP3_REP_QNAME               /* namespace URI, prefix, local name */
} xp3Representation;

const xmlChar *
xp3TypeLocalName(xp3TypeId type);

xp3TypeId
xp3TypeParent(xp3TypeId type);

xp3TypeId
xp3TypePrimitive(xp3TypeId type);

xp3Representation
xp3TypeRepresentation(xp3TypeId type);

int
xp3TypeIsAbstract(xp3TypeId type);

int
xp3TypeDerivesFrom(xp3TypeId type, xp3TypeId ancestor);

int
xp3TypeLookup(const xmlChar *localName);

#endif /* XP3_XP3TYPES_H */
