/*
 * xp3error.c: error objects
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/xmlerror.h>
#include <libxml/xmlstring.h>

#include "private/xp3error.h"

/*
 * Returned whenever an error object cannot be allocated, so reporting
 * an out-of-memory condition never needs memory itself.
 */
static xp3Error xp3NoMemoryError = {
    XP3_ERR_NO_MEMORY,              /* code */
    NULL,                           /* codeNs */
    NULL,                           /* codeLocal */
    (xmlChar *) "Out of memory",    /* message */
    0,                              /* line */
    0,                              /* column */
    1                               /* isStatic */
};

/* Names of the built-in codes, indexed by xp3ErrCode. */
static const char *const xp3ErrCodeNames[] = {
    [XP3_ERR_OK]        = "OK",
    [XP3_ERR_NO_MEMORY] = "NO_MEMORY",
    [XP3_ERR_LIMIT]     = "LIMIT",
    [XP3_ERR_UNSUPPORTED] = "UNSUPPORTED",
    [XP3_ERR_XPST0003]  = "XPST0003",
    [XP3_ERR_XPST0008]  = "XPST0008",
    [XP3_ERR_XPST0010]  = "XPST0010",
    [XP3_ERR_XPST0017]  = "XPST0017",
    [XP3_ERR_XPST0051]  = "XPST0051",
    [XP3_ERR_XPST0080]  = "XPST0080",
    [XP3_ERR_XPST0081]  = "XPST0081",
    [XP3_ERR_XQST0039]  = "XQST0039",
    [XP3_ERR_XQST0052]  = "XQST0052",
    [XP3_ERR_XQST0070]  = "XQST0070",
    [XP3_ERR_XPDY0002]  = "XPDY0002",
    [XP3_ERR_XPDY0050]  = "XPDY0050",
    [XP3_ERR_XPTY0004]  = "XPTY0004",
    [XP3_ERR_XPTY0018]  = "XPTY0018",
    [XP3_ERR_XPTY0019]  = "XPTY0019",
    [XP3_ERR_XPTY0020]  = "XPTY0020",
    [XP3_ERR_XPTY0117]  = "XPTY0117",
    [XP3_ERR_FOAR0001]  = "FOAR0001",
    [XP3_ERR_FOAR0002]  = "FOAR0002",
    [XP3_ERR_FORG0001]  = "FORG0001",
    [XP3_ERR_FORG0003]  = "FORG0003",
    [XP3_ERR_FORG0004]  = "FORG0004",
    [XP3_ERR_FORG0005]  = "FORG0005",
    [XP3_ERR_FORG0006]  = "FORG0006",
    [XP3_ERR_FOCA0001]  = "FOCA0001",
    [XP3_ERR_FOCA0002]  = "FOCA0002",
    [XP3_ERR_FOCA0003]  = "FOCA0003",
    [XP3_ERR_FOTY0013]  = "FOTY0013",
    [XP3_ERR_FOTY0014]  = "FOTY0014",
    [XP3_ERR_USER]      = NULL      /* the QName is stored in the object */
};

/**
 * Get the name of a built-in error code.
 *
 * @param code  the error code
 * @returns the name, for example "XPST0003", or NULL for XP3_ERR_USER
 *          and unknown codes
 */
const char *
xp3ErrCodeName(xp3ErrCode code) {
    if ((unsigned) code >=
        sizeof(xp3ErrCodeNames) / sizeof(xp3ErrCodeNames[0]))
        return(NULL);
    return(xp3ErrCodeNames[code]);
}

/**
 * Get the preallocated out-of-memory error.
 *
 * The object is shared and never freed. xp3ErrorFree() ignores it.
 *
 * @returns the out-of-memory error
 */
xp3Error *
xp3ErrorNoMemory(void) {
    return(&xp3NoMemoryError);
}

/**
 * Create an error with a printf-style message, taking a va_list.
 *
 * @param code    the error code
 * @param line    1-based line of the source position, 0 if unknown
 * @param column  1-based column of the source position, 0 if unknown
 * @param format  printf-style format of the message, or NULL
 * @param args    arguments for the format
 * @returns the new error, or the out-of-memory error if allocation
 *          failed. Never NULL.
 */
xp3Error *
xp3ErrorNewV(xp3ErrCode code, int line, int column, const char *format,
             va_list args) {
    xp3Error *err;

    err = xmlMalloc(sizeof(*err));
    if (err == NULL)
        return(xp3ErrorNoMemory());
    memset(err, 0, sizeof(*err));
    err->code = code;
    err->line = line;
    err->column = column;

    if (format != NULL) {
        va_list copy;
        int len;

        va_copy(copy, args);
        len = vsnprintf(NULL, 0, format, copy);
        va_end(copy);

        if (len >= 0) {
            err->message = xmlMalloc((size_t) len + 1);
            if (err->message == NULL) {
                xmlFree(err);
                return(xp3ErrorNoMemory());
            }
            vsnprintf((char *) err->message, (size_t) len + 1, format, args);
        }
    }

    return(err);
}

/**
 * Create an error with a printf-style message.
 *
 * @param code    the error code
 * @param line    1-based line of the source position, 0 if unknown
 * @param column  1-based column of the source position, 0 if unknown
 * @param format  printf-style format of the message, or NULL
 * @param ...     arguments for the format
 * @returns the new error, or the out-of-memory error if allocation
 *          failed. Never NULL.
 */
xp3Error *
xp3ErrorNew(xp3ErrCode code, int line, int column, const char *format, ...) {
    va_list args;
    xp3Error *err;

    va_start(args, format);
    err = xp3ErrorNewV(code, line, column, format, args);
    va_end(args);

    return(err);
}

/**
 * Create a user-defined error, as raised by fn:error.
 *
 * If codeLocal is NULL, the error gets the default code err:FOER0000,
 * as fn:error does without arguments.
 *
 * @param codeNs     namespace URI of the error code, or NULL
 * @param codeLocal  local name of the error code, or NULL
 * @param message    the message, or NULL
 * @returns the new error, or the out-of-memory error if allocation
 *          failed. Never NULL.
 */
xp3Error *
xp3ErrorNewUser(const xmlChar *codeNs, const xmlChar *codeLocal,
                const xmlChar *message) {
    xp3Error *err;

    if (codeLocal == NULL) {
        codeNs = BAD_CAST XP3_ERR_NS;
        codeLocal = BAD_CAST "FOER0000";
    }

    err = xmlMalloc(sizeof(*err));
    if (err == NULL)
        return(xp3ErrorNoMemory());
    memset(err, 0, sizeof(*err));
    err->code = XP3_ERR_USER;

    err->codeLocal = xmlStrdup(codeLocal);
    if (err->codeLocal == NULL)
        goto oom;
    if (codeNs != NULL) {
        err->codeNs = xmlStrdup(codeNs);
        if (err->codeNs == NULL)
            goto oom;
    }
    if (message != NULL) {
        err->message = xmlStrdup(message);
        if (err->message == NULL)
            goto oom;
    }

    return(err);

oom:
    xp3ErrorFree(err);
    return(xp3ErrorNoMemory());
}

/**
 * Free an error. NULL and the shared out-of-memory error are ignored.
 *
 * @param error  the error
 */
void
xp3ErrorFree(xp3Error *error) {
    if ((error == NULL) || (error->isStatic))
        return;
    xmlFree(error->codeNs);
    xmlFree(error->codeLocal);
    xmlFree(error->message);
    xmlFree(error);
}

/**
 * Get the error code, for example "XPST0003".
 *
 * Errors raised by fn:error return the local name of their code.
 * Out-of-memory and resource-limit errors return "NO_MEMORY" and
 * "LIMIT", which are not W3C codes.
 *
 * @param error  the error
 * @returns the code, or NULL if error is NULL
 */
const xmlChar *
xp3ErrorCode(const xp3Error *error) {
    if (error == NULL)
        return(NULL);
    if (error->code == XP3_ERR_USER)
        return(error->codeLocal);
    return(BAD_CAST xp3ErrCodeName(error->code));
}

/**
 * Get the namespace URI of the error code.
 *
 * W3C codes are in "http://www.w3.org/2005/xqt-errors". Out-of-memory,
 * resource-limit and not-implemented errors are not W3C codes and have
 * no namespace.
 *
 * @param error  the error
 * @returns the namespace URI, or NULL
 */
const xmlChar *
xp3ErrorNamespace(const xp3Error *error) {
    if (error == NULL)
        return(NULL);
    switch (error->code) {
        case XP3_ERR_USER:
            return(error->codeNs);
        case XP3_ERR_OK:
        case XP3_ERR_NO_MEMORY:
        case XP3_ERR_LIMIT:
        case XP3_ERR_UNSUPPORTED:
            return(NULL);
        default:
            return(BAD_CAST XP3_ERR_NS);
    }
}

/**
 * Get the error message.
 *
 * @param error  the error
 * @returns the message, or NULL if there is none
 */
const xmlChar *
xp3ErrorMessage(const xp3Error *error) {
    if (error == NULL)
        return(NULL);
    return(error->message);
}

/**
 * Get the line of the source position.
 *
 * @param error  the error
 * @returns the 1-based line, or 0 if unknown
 */
int
xp3ErrorLine(const xp3Error *error) {
    if (error == NULL)
        return(0);
    return(error->line);
}

/**
 * Get the column of the source position.
 *
 * @param error  the error
 * @returns the 1-based column, or 0 if unknown
 */
int
xp3ErrorColumn(const xp3Error *error) {
    if (error == NULL)
        return(0);
    return(error->column);
}

/**
 * Pass an error to a libxml2 structured error handler.
 *
 * The error is reported in the XML_FROM_XPATH domain. The error code
 * goes in str1, its namespace in str2 and the column in int2.
 *
 * @param error     the error
 * @param handler   the libxml2 handler
 * @param userData  passed to the handler
 */
void
xp3ErrorReport(const xp3Error *error, xmlStructuredErrorFunc handler,
               void *userData) {
    xmlError xerr;

    if ((error == NULL) || (handler == NULL))
        return;

    memset(&xerr, 0, sizeof(xerr));
    xerr.domain = XML_FROM_XPATH;
    xerr.code = (error->code == XP3_ERR_NO_MEMORY) ?
                XML_ERR_NO_MEMORY : XML_XPATH_EXPR_ERROR;
    xerr.level = XML_ERR_ERROR;
    xerr.message = (char *) error->message;
    xerr.line = error->line;
    xerr.int2 = error->column;
    xerr.str1 = (char *) xp3ErrorCode(error);
    xerr.str2 = (char *) xp3ErrorNamespace(error);

    handler(userData, &xerr);
}
