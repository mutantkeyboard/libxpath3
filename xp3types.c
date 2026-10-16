/*
 * xp3types.c: the built-in atomic types and their hierarchy
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stddef.h>

#include <libxml/xmlstring.h>

#include "private/xp3types.h"

/* Flags of a type */
#define XP3_TYPE_FLAG_ABSTRACT 1    /* no value has exactly this type */
#define XP3_TYPE_FLAG_PRIMITIVE 2   /* primitive in XSD 1.1, or
                                     * untypedAtomic */

typedef struct {
    const char *name;               /* local name in the xs namespace */
    xp3TypeId parent;
    xp3Representation rep;
    int flags;
} xp3TypeInfo;

#define ABSTRACT XP3_TYPE_FLAG_ABSTRACT
#define PRIMITIVE XP3_TYPE_FLAG_PRIMITIVE

/* Indexed by xp3TypeId. The root is its own parent. */
static const xp3TypeInfo xp3Types[] = {
    [XP3_TYPE_ANY_ATOMIC]    = { "anyAtomicType", XP3_TYPE_ANY_ATOMIC,
                                 XP3_REP_NONE, ABSTRACT },
    [XP3_TYPE_UNTYPED_ATOMIC] = { "untypedAtomic", XP3_TYPE_ANY_ATOMIC,
                                  XP3_REP_STRING, PRIMITIVE },

    [XP3_TYPE_STRING]        = { "string", XP3_TYPE_ANY_ATOMIC,
                                 XP3_REP_STRING, PRIMITIVE },
    [XP3_TYPE_NORMALIZED_STRING] = { "normalizedString", XP3_TYPE_STRING,
                                     XP3_REP_STRING, 0 },
    [XP3_TYPE_TOKEN]         = { "token", XP3_TYPE_NORMALIZED_STRING,
                                 XP3_REP_STRING, 0 },
    [XP3_TYPE_LANGUAGE]      = { "language", XP3_TYPE_TOKEN,
                                 XP3_REP_STRING, 0 },
    [XP3_TYPE_NMTOKEN]       = { "NMTOKEN", XP3_TYPE_TOKEN,
                                 XP3_REP_STRING, 0 },
    [XP3_TYPE_NAME]          = { "Name", XP3_TYPE_TOKEN,
                                 XP3_REP_STRING, 0 },
    [XP3_TYPE_NCNAME]        = { "NCName", XP3_TYPE_NAME,
                                 XP3_REP_STRING, 0 },
    [XP3_TYPE_ID]            = { "ID", XP3_TYPE_NCNAME,
                                 XP3_REP_STRING, 0 },
    [XP3_TYPE_IDREF]         = { "IDREF", XP3_TYPE_NCNAME,
                                 XP3_REP_STRING, 0 },
    [XP3_TYPE_ENTITY]        = { "ENTITY", XP3_TYPE_NCNAME,
                                 XP3_REP_STRING, 0 },

    [XP3_TYPE_BOOLEAN]       = { "boolean", XP3_TYPE_ANY_ATOMIC,
                                 XP3_REP_BOOLEAN, PRIMITIVE },

    [XP3_TYPE_DECIMAL]       = { "decimal", XP3_TYPE_ANY_ATOMIC,
                                 XP3_REP_DECIMAL, PRIMITIVE },
    [XP3_TYPE_INTEGER]       = { "integer", XP3_TYPE_DECIMAL,
                                 XP3_REP_DECIMAL, 0 },
    [XP3_TYPE_NON_POSITIVE_INTEGER] = { "nonPositiveInteger",
                                        XP3_TYPE_INTEGER,
                                        XP3_REP_DECIMAL, 0 },
    [XP3_TYPE_NEGATIVE_INTEGER] = { "negativeInteger",
                                    XP3_TYPE_NON_POSITIVE_INTEGER,
                                    XP3_REP_DECIMAL, 0 },
    [XP3_TYPE_LONG]          = { "long", XP3_TYPE_INTEGER,
                                 XP3_REP_DECIMAL, 0 },
    [XP3_TYPE_INT]           = { "int", XP3_TYPE_LONG,
                                 XP3_REP_DECIMAL, 0 },
    [XP3_TYPE_SHORT]         = { "short", XP3_TYPE_INT,
                                 XP3_REP_DECIMAL, 0 },
    [XP3_TYPE_BYTE]          = { "byte", XP3_TYPE_SHORT,
                                 XP3_REP_DECIMAL, 0 },
    [XP3_TYPE_NON_NEGATIVE_INTEGER] = { "nonNegativeInteger",
                                        XP3_TYPE_INTEGER,
                                        XP3_REP_DECIMAL, 0 },
    [XP3_TYPE_UNSIGNED_LONG] = { "unsignedLong",
                                 XP3_TYPE_NON_NEGATIVE_INTEGER,
                                 XP3_REP_DECIMAL, 0 },
    [XP3_TYPE_UNSIGNED_INT]  = { "unsignedInt", XP3_TYPE_UNSIGNED_LONG,
                                 XP3_REP_DECIMAL, 0 },
    [XP3_TYPE_UNSIGNED_SHORT] = { "unsignedShort", XP3_TYPE_UNSIGNED_INT,
                                  XP3_REP_DECIMAL, 0 },
    [XP3_TYPE_UNSIGNED_BYTE] = { "unsignedByte", XP3_TYPE_UNSIGNED_SHORT,
                                 XP3_REP_DECIMAL, 0 },
    [XP3_TYPE_POSITIVE_INTEGER] = { "positiveInteger",
                                    XP3_TYPE_NON_NEGATIVE_INTEGER,
                                    XP3_REP_DECIMAL, 0 },

    [XP3_TYPE_FLOAT]         = { "float", XP3_TYPE_ANY_ATOMIC,
                                 XP3_REP_FLOAT, PRIMITIVE },
    [XP3_TYPE_DOUBLE]        = { "double", XP3_TYPE_ANY_ATOMIC,
                                 XP3_REP_DOUBLE, PRIMITIVE },

    [XP3_TYPE_DURATION]      = { "duration", XP3_TYPE_ANY_ATOMIC,
                                 XP3_REP_DURATION, PRIMITIVE },
    [XP3_TYPE_YEAR_MONTH_DURATION] = { "yearMonthDuration",
                                       XP3_TYPE_DURATION,
                                       XP3_REP_DURATION, 0 },
    [XP3_TYPE_DAY_TIME_DURATION] = { "dayTimeDuration", XP3_TYPE_DURATION,
                                     XP3_REP_DURATION, 0 },

    [XP3_TYPE_DATE_TIME]     = { "dateTime", XP3_TYPE_ANY_ATOMIC,
                                 XP3_REP_DATE_TIME, PRIMITIVE },
    [XP3_TYPE_DATE_TIME_STAMP] = { "dateTimeStamp", XP3_TYPE_DATE_TIME,
                                   XP3_REP_DATE_TIME, 0 },
    [XP3_TYPE_TIME]          = { "time", XP3_TYPE_ANY_ATOMIC,
                                 XP3_REP_DATE_TIME, PRIMITIVE },
    [XP3_TYPE_DATE]          = { "date", XP3_TYPE_ANY_ATOMIC,
                                 XP3_REP_DATE_TIME, PRIMITIVE },
    [XP3_TYPE_G_YEAR_MONTH]  = { "gYearMonth", XP3_TYPE_ANY_ATOMIC,
                                 XP3_REP_DATE_TIME, PRIMITIVE },
    [XP3_TYPE_G_YEAR]        = { "gYear", XP3_TYPE_ANY_ATOMIC,
                                 XP3_REP_DATE_TIME, PRIMITIVE },
    [XP3_TYPE_G_MONTH_DAY]   = { "gMonthDay", XP3_TYPE_ANY_ATOMIC,
                                 XP3_REP_DATE_TIME, PRIMITIVE },
    [XP3_TYPE_G_DAY]         = { "gDay", XP3_TYPE_ANY_ATOMIC,
                                 XP3_REP_DATE_TIME, PRIMITIVE },
    [XP3_TYPE_G_MONTH]       = { "gMonth", XP3_TYPE_ANY_ATOMIC,
                                 XP3_REP_DATE_TIME, PRIMITIVE },

    [XP3_TYPE_HEX_BINARY]    = { "hexBinary", XP3_TYPE_ANY_ATOMIC,
                                 XP3_REP_BINARY, PRIMITIVE },
    [XP3_TYPE_BASE64_BINARY] = { "base64Binary", XP3_TYPE_ANY_ATOMIC,
                                 XP3_REP_BINARY, PRIMITIVE },
    [XP3_TYPE_ANY_URI]       = { "anyURI", XP3_TYPE_ANY_ATOMIC,
                                 XP3_REP_STRING, PRIMITIVE },
    [XP3_TYPE_QNAME]         = { "QName", XP3_TYPE_ANY_ATOMIC,
                                 XP3_REP_QNAME, PRIMITIVE },
    [XP3_TYPE_NOTATION]      = { "NOTATION", XP3_TYPE_ANY_ATOMIC,
                                 XP3_REP_QNAME, PRIMITIVE | ABSTRACT }
};

#undef ABSTRACT
#undef PRIMITIVE

#define XP3_TABLE_SIZE(t) (sizeof(t) / sizeof((t)[0]))

/* The build fails here if a type has no table entry. */
typedef char xp3TypesSize[
    (XP3_TABLE_SIZE(xp3Types) == XP3_TYPE_COUNT) ? 1 : -1];

static int
xp3TypeValid(xp3TypeId type) {
    return((unsigned) type < XP3_TYPE_COUNT);
}

/**
 * Get the local name of a type in the xs namespace.
 *
 * @param type  the type
 * @returns the name, for example "integer", or NULL for invalid types
 */
const xmlChar *
xp3TypeLocalName(xp3TypeId type) {
    if (!xp3TypeValid(type))
        return(NULL);
    return(BAD_CAST xp3Types[type].name);
}

/**
 * Get the type a type is derived from. xs:anyAtomicType is its own
 * parent.
 *
 * @param type  the type
 * @returns the parent, or XP3_TYPE_ANY_ATOMIC for invalid types
 */
xp3TypeId
xp3TypeParent(xp3TypeId type) {
    if (!xp3TypeValid(type))
        return(XP3_TYPE_ANY_ATOMIC);
    return(xp3Types[type].parent);
}

/**
 * Get the primitive type a type is derived from, for example
 * xs:decimal for xs:byte. xs:untypedAtomic counts as primitive, as in
 * XDM.
 *
 * @param type  the type
 * @returns the primitive type, or XP3_TYPE_ANY_ATOMIC for
 *          xs:anyAtomicType and invalid types
 */
xp3TypeId
xp3TypePrimitive(xp3TypeId type) {
    if (!xp3TypeValid(type))
        return(XP3_TYPE_ANY_ATOMIC);
    while (!(xp3Types[type].flags & XP3_TYPE_FLAG_PRIMITIVE) &&
           (type != XP3_TYPE_ANY_ATOMIC))
        type = xp3Types[type].parent;
    return(type);
}

/**
 * Get how values of a type are stored.
 *
 * @param type  the type
 * @returns the representation, XP3_REP_NONE for invalid types
 */
xp3Representation
xp3TypeRepresentation(xp3TypeId type) {
    if (!xp3TypeValid(type))
        return(XP3_REP_NONE);
    return(xp3Types[type].rep);
}

/**
 * Whether a type is abstract, so no value can have it as its type:
 * xs:anyAtomicType and xs:NOTATION.
 *
 * @param type  the type
 * @returns 1 if abstract or invalid, otherwise 0
 */
int
xp3TypeIsAbstract(xp3TypeId type) {
    if (!xp3TypeValid(type))
        return(1);
    return((xp3Types[type].flags & XP3_TYPE_FLAG_ABSTRACT) != 0);
}

/**
 * Whether a type is the same as or derived from another, for example
 * xs:byte from xs:integer. Every type derives from xs:anyAtomicType.
 *
 * @param type      the type
 * @param ancestor  the possible ancestor
 * @returns 1 if it is, 0 if not or if an argument is invalid
 */
int
xp3TypeDerivesFrom(xp3TypeId type, xp3TypeId ancestor) {
    if (!xp3TypeValid(type) || !xp3TypeValid(ancestor))
        return(0);
    for (;;) {
        if (type == ancestor)
            return(1);
        if (type == XP3_TYPE_ANY_ATOMIC)
            return(0);
        type = xp3Types[type].parent;
    }
}

/**
 * Find a type by its local name in the xs namespace. The caller
 * checks the namespace.
 *
 * @param localName  the local name, for example "integer"
 * @returns the type, or -1 if there is no such atomic type
 */
int
xp3TypeLookup(const xmlChar *localName) {
    int i;

    if (localName == NULL)
        return(-1);
    for (i = 0; i < XP3_TYPE_COUNT; i++) {
        if (xmlStrEqual(localName, BAD_CAST xp3Types[i].name))
            return(i);
    }
    return(-1);
}
