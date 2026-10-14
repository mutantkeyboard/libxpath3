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

/*
 * Built-in atomic types of XML Schema 1.1 and XDM 3.1, in an order
 * where every type comes after its parent. The comment names the
 * parent.
 */
typedef enum {
    XP3_TYPE_ANY_ATOMIC = 0,    /* xs:anyAtomicType, the root */
    XP3_TYPE_UNTYPED_ATOMIC,    /* anyAtomicType */

    XP3_TYPE_STRING,            /* anyAtomicType */
    XP3_TYPE_NORMALIZED_STRING, /* string */
    XP3_TYPE_TOKEN,             /* normalizedString */
    XP3_TYPE_LANGUAGE,          /* token */
    XP3_TYPE_NMTOKEN,           /* token */
    XP3_TYPE_NAME,              /* token */
    XP3_TYPE_NCNAME,            /* Name */
    XP3_TYPE_ID,                /* NCName */
    XP3_TYPE_IDREF,             /* NCName */
    XP3_TYPE_ENTITY,            /* NCName */

    XP3_TYPE_BOOLEAN,           /* anyAtomicType */

    XP3_TYPE_DECIMAL,           /* anyAtomicType */
    XP3_TYPE_INTEGER,           /* decimal */
    XP3_TYPE_NON_POSITIVE_INTEGER, /* integer */
    XP3_TYPE_NEGATIVE_INTEGER,  /* nonPositiveInteger */
    XP3_TYPE_LONG,              /* integer */
    XP3_TYPE_INT,               /* long */
    XP3_TYPE_SHORT,             /* int */
    XP3_TYPE_BYTE,              /* short */
    XP3_TYPE_NON_NEGATIVE_INTEGER, /* integer */
    XP3_TYPE_UNSIGNED_LONG,     /* nonNegativeInteger */
    XP3_TYPE_UNSIGNED_INT,      /* unsignedLong */
    XP3_TYPE_UNSIGNED_SHORT,    /* unsignedInt */
    XP3_TYPE_UNSIGNED_BYTE,     /* unsignedShort */
    XP3_TYPE_POSITIVE_INTEGER,  /* nonNegativeInteger */

    XP3_TYPE_FLOAT,             /* anyAtomicType */
    XP3_TYPE_DOUBLE,            /* anyAtomicType */

    XP3_TYPE_DURATION,          /* anyAtomicType */
    XP3_TYPE_YEAR_MONTH_DURATION, /* duration */
    XP3_TYPE_DAY_TIME_DURATION, /* duration */

    XP3_TYPE_DATE_TIME,         /* anyAtomicType */
    XP3_TYPE_DATE_TIME_STAMP,   /* dateTime */
    XP3_TYPE_TIME,              /* anyAtomicType */
    XP3_TYPE_DATE,              /* anyAtomicType */
    XP3_TYPE_G_YEAR_MONTH,      /* anyAtomicType */
    XP3_TYPE_G_YEAR,            /* anyAtomicType */
    XP3_TYPE_G_MONTH_DAY,       /* anyAtomicType */
    XP3_TYPE_G_DAY,             /* anyAtomicType */
    XP3_TYPE_G_MONTH,           /* anyAtomicType */

    XP3_TYPE_HEX_BINARY,        /* anyAtomicType */
    XP3_TYPE_BASE64_BINARY,     /* anyAtomicType */
    XP3_TYPE_ANY_URI,           /* anyAtomicType */
    XP3_TYPE_QNAME,             /* anyAtomicType */
    XP3_TYPE_NOTATION,          /* anyAtomicType */

    XP3_TYPE_COUNT              /* number of types, not a type */
} xp3TypeId;

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
