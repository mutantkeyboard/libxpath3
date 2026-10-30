/**
 * @file
 *
 * @brief Error objects (internal)
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XP3ERROR_H
#define XP3_XP3ERROR_H

#include <stdarg.h>

#include <libxml/xmlerror.h>
#include <libxml/xmlstring.h>

#include <libxpath3/xpath3.h>

/** Namespace of the W3C error codes */
#define XP3_ERR_NS "http://www.w3.org/2005/xqt-errors"

/**
 * Built-in error codes. Raising an error uses the enum, and the API
 * exposes the QName (for example "XPTY0004") and the namespace
 * (XP3_ERR_NS) for the error.
 */

typedef enum
{
    XP3_ERR_OK = 0,
    XP3_ERR_NO_MEMORY, /* not a W3C code: allocation failed */
    XP3_ERR_LIMIT,     /* not a W3C code: resource limit hit */
    XP3_ERR_UNSUPPORTED, /* not a W3C code: not implemented yet */
    XP3_ERR_XPST0003,  /* syntax error */
    XP3_ERR_XPST0008,  /* undeclared variable */
    XP3_ERR_XPST0010,  /* namespace axis not supported */
    XP3_ERR_XPST0017,  /* unknown function or wrong arity */
    XP3_ERR_XPST0051,  /* sequence type with a non-atomic type */
    XP3_ERR_XPST0080,  /* cast to an abstract type */
    XP3_ERR_XPST0081,  /* unbound namespace prefix */
    XP3_ERR_XQST0039,  /* duplicate parameter name */
    XP3_ERR_XQST0052,  /* cast to an unknown or complex type */
    XP3_ERR_XQST0070,  /* the xmlns namespace in a name */
    XP3_ERR_XPDY0002,  /* context item or variable value absent */
    XP3_ERR_XPDY0050,  /* the root of the context node is no document */
    XP3_ERR_XPTY0004,  /* type error */
    XP3_ERR_XPTY0018,  /* last step mixes nodes and other items */
    XP3_ERR_XPTY0019,  /* non-node on the left of a path step */
    XP3_ERR_XPTY0020,  /* the context item of an axis step is no node */
    XP3_ERR_XPTY0117,  /* untyped value for a namespace-sensitive type */
    XP3_ERR_FOAR0001,  /* division by zero */
    XP3_ERR_FOAR0002,  /* number too large, such as an arity */
    XP3_ERR_FORG0001,  /* invalid value for cast */
    XP3_ERR_FORG0003,  /* fn:zero-or-one with more than one item */
    XP3_ERR_FORG0004,  /* fn:one-or-more with the empty sequence */
    XP3_ERR_FORG0005,  /* fn:exactly-one without exactly one item */
    XP3_ERR_FORG0006,  /* no effective boolean value */
    XP3_ERR_FOCA0001,  /* value too large for xs:decimal */
    XP3_ERR_FOCA0002,  /* invalid value: NaN or INF to a decimal type */
    XP3_ERR_FOCA0003,  /* value too large for xs:integer */
    XP3_ERR_FOTY0013,  /* atomizing a function item */
    XP3_ERR_FOTY0014,  /* the string value of a function item */
    XP3_ERR_USER       /* raised by fn:error, QName stored in the object */
} xp3ErrCode;

struct _xp3Error
{
    xp3ErrCode code;
    xmlChar *codeNs;    /* only for XP3_ERR_USER, otherwise NULL */
    xmlChar *codeLocal; /* only for XP3_ERR_USER, otherwise NULL */
    xmlChar *message;   /* may be NULL */
    int line;
    int column;
    int isStatic; /* if true, the error object is static and should not
                   * be freed */
};

xp3Error *xp3ErrorNew(xp3ErrCode code, int line, int column,
                      const char *format, ...)
#ifdef __GNUC__
__attribute__((format(printf, 4, 5)))
#endif
;

xp3Error *xp3ErrorNewV(xp3ErrCode code, int line, int column,
                       const char *format, va_list args);
xp3Error *xp3ErrorNewUser(const xmlChar *codeNs, const xmlChar *codeLocal,
                          const xmlChar *message);
xp3Error *xp3ErrorNoMemory(void);
void xp3ErrorReport(const xp3Error *error, xmlStructuredErrorFunc handler,
                    void *userData);
const char *xp3ErrCodeName(xp3ErrCode code);

#endif /* XP3_XP3ERROR_H */