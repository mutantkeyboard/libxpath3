/*
 * xp3fnstring.c: the functions on strings and URIs, and the collations
 *
 * Implementations of the string and URI functions of F&O 3.1,
 * https://www.w3.org/TR/2017/REC-xpath-functions-31-20170321/, bound
 * to their signatures in xp3func.c. The evaluator has applied the
 * function conversion rules to the arguments already, so an xs:string?
 * argument is the empty sequence or one string, with xs:untypedAtomic
 * cast and xs:anyURI promoted to xs:string.
 *
 * Strings are UTF-8, so comparing them byte by byte compares their
 * codepoints, which is the Unicode codepoint collation (F&O 3.1,
 * 5.3.2), and a substring found byte by byte starts and ends at
 * character boundaries. The HTML ASCII case-insensitive collation
 * (5.3.4) compares the bytes A to Z as a to z, bytes that UTF-8 never
 * uses inside a character of more than one byte. The URIs of the UCA
 * family (5.3.3) fall back to the codepoint collation, as the spec
 * allows; any other collation raises FOCH0002. Text that libxml2
 * stored without checking may hold bytes that are not UTF-8: each such
 * byte counts as one character, U+FFFD, and is copied unchanged.
 *
 * Results are built in one buffer that doubles as it grows, and
 * searches take linear time, so no function is quadratic in the length
 * of its arguments. A string is at most 2 GB long, as everywhere.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

#include "private/xp3chars.h"
#include "private/xp3decimal.h"
#include "private/xp3func.h"
#include "private/xp3types.h"
#include "private/xp3value.h"

/* The longest string, as for every atomic value (xp3AtomicNewString()) */
#define XP3_STRING_MAX ((size_t) INT32_MAX)

/* Searches for strings up to this long keep their table on the stack */
#define XP3_SEARCH_STACK 64

/*
 * Helpers
 */

/* Whitespace as XML 1.0 defines it (production S) */
static int
isSpace(int c) {
    return((c == 0x20) || (c == 0x9) || (c == 0xA) || (c == 0xD));
}

/* The string of an xs:string? argument, borrowed from it: the empty
 * string for the empty sequence */
static const xmlChar *
argString(const xp3Value *value) {
    if (xp3ValueCount(value) == 0)
        return(BAD_CAST "");
    return(value->u.single.u.atomic->u.str);
}

/* The value of an xs:double argument */
static double
argDouble(const xp3Value *value) {
    return(value->u.single.u.atomic->u.d);
}

/*
 * Decode the character at *pos and move past it. A byte that doesn't
 * start valid UTF-8 is one character, U+FFFD.
 */
static int
nextChar(const xmlChar *str, size_t len, size_t *pos) {
    size_t avail = len - *pos;
    int bytes = (avail < 4) ? (int) avail : 4;
    int c;

    if (str[*pos] < 0x80)
        return(str[(*pos)++]);
    c = xp3GetUTF8Char(str + *pos, &bytes);
    if (c < 0) {
        (*pos)++;
        return(0xFFFD);
    }
    *pos += (size_t) bytes;
    return(c);
}

/* Encode a code point, at most U+10FFFF, as UTF-8. Returns the number
 * of bytes. */
static int
encodeChar(int c, xmlChar *out) {
    if (c < 0x80) {
        out[0] = (xmlChar) c;
        return(1);
    }
    if (c < 0x800) {
        out[0] = (xmlChar) (0xC0 | (c >> 6));
        out[1] = (xmlChar) (0x80 | (c & 0x3F));
        return(2);
    }
    if (c < 0x10000) {
        out[0] = (xmlChar) (0xE0 | (c >> 12));
        out[1] = (xmlChar) (0x80 | ((c >> 6) & 0x3F));
        out[2] = (xmlChar) (0x80 | (c & 0x3F));
        return(3);
    }
    out[0] = (xmlChar) (0xF0 | (c >> 18));
    out[1] = (xmlChar) (0x80 | ((c >> 12) & 0x3F));
    out[2] = (xmlChar) (0x80 | ((c >> 6) & 0x3F));
    out[3] = (xmlChar) (0x80 | (c & 0x3F));
    return(4);
}

/* A new xs:string from bytes, as the result */
static xp3Value *
newString(xp3FuncCall *call, const xmlChar *str, size_t len) {
    return(xp3FuncAtomic(call, xp3AtomicNewString(XP3_TYPE_STRING, str,
                                                  len)));
}

/* The string value of the context item, as fn:string#0 gives it, for
 * the functions whose argument defaults to it */
static xp3Value *
contextString(xp3FuncCall *call) {
    return(xp3FnString(call, NULL, 0));
}

/* A string being built, in one buffer that doubles as it grows */
typedef struct {
    xmlChar *data;
    size_t len;
    size_t cap;
    const char *name;           /* the function, for messages */
} xp3StrBuf;

static void
bufInit(xp3StrBuf *buf, const char *name) {
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    buf->name = name;
}

/* Append bytes. Returns 0, or -1 after recording LIMIT or NO_MEMORY;
 * the caller then frees buf->data. */
static int
bufAppend(xp3FuncCall *call, xp3StrBuf *buf, const xmlChar *str,
          size_t len) {
    if (len > XP3_STRING_MAX - buf->len) {
        xp3FuncError(call, XP3_ERR_LIMIT, "the result of %s would be "
                     "longer than 2 GB", buf->name);
        return(-1);
    }
    if (buf->len + len >= buf->cap) {
        size_t need = buf->len + len + 1;
        size_t cap = (buf->cap > 0) ? buf->cap : 64;
        xmlChar *data;

        while (cap < need)
            cap = (cap > XP3_STRING_MAX / 2) ? XP3_STRING_MAX + 1 : cap * 2;
        data = xmlRealloc(buf->data, cap);
        if (data == NULL) {
            xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
            return(-1);
        }
        buf->data = data;
        buf->cap = cap;
    }
    if (len > 0)
        memcpy(buf->data + buf->len, str, len);
    buf->len += len;
    return(0);
}

/* Append an atomic value cast to xs:string. Returns 0 or -1. */
static int
bufAppendAtomic(xp3FuncCall *call, xp3StrBuf *buf,
                const xp3Atomic *atomic) {
    xmlChar *str;
    int ret;

    /* Casting a string type to xs:string keeps its value */
    if (xp3TypeRepresentation(atomic->type) == XP3_REP_STRING)
        return(bufAppend(call, buf, atomic->u.str,
                         strlen((const char *) atomic->u.str)));
    str = xp3AtomicToString(atomic);
    if (str == NULL) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(-1);
    }
    ret = bufAppend(call, buf, str, strlen((const char *) str));
    xmlFree(str);
    return(ret);
}

/* The string built, as the result; frees the buffer */
static xp3Value *
bufFinish(xp3FuncCall *call, xp3StrBuf *buf) {
    xp3Value *result = newString(call, (buf->data != NULL) ? buf->data :
                                                          BAD_CAST "",
                                 buf->len);

    xmlFree(buf->data);
    buf->data = NULL;
    return(result);
}

/* A byte as a collation compares it */
static int
collationByte(xp3Collation collation, xmlChar c) {
    if ((collation == XP3_COLLATION_HTML_ASCII) && (c >= 'A') && (c <= 'Z'))
        return(c - 'A' + 'a');
    return(c);
}

/* Whether two runs of bytes are equal under a collation */
static int
bytesEqual(xp3Collation collation, const xmlChar *str1,
           const xmlChar *str2, size_t len) {
    size_t i;

    if (collation == XP3_COLLATION_CODEPOINT)
        return(memcmp(str1, str2, len) == 0);
    for (i = 0; i < len; i++) {
        if (collationByte(collation, str1[i]) !=
            collationByte(collation, str2[i]))
            return(0);
    }
    return(1);
}

/*
 * Find the first occurrence of needle in haystack under a collation,
 * with the algorithm of Knuth, Morris and Pratt, which takes time
 * linear in both lengths. Sets *at to its byte offset. Returns 1 if
 * found, 0 if not, or -1 after recording NO_MEMORY.
 */
static int
findString(xp3FuncCall *call, xp3Collation collation,
           const xmlChar *haystack, size_t hayLen, const xmlChar *needle,
           size_t needleLen, size_t *at) {
    size_t fixed[XP3_SEARCH_STACK];
    size_t *border = fixed;
    size_t i;
    size_t k;
    int found = 0;

    *at = 0;
    if (needleLen == 0)
        return(1);
    if (needleLen > hayLen)
        return(0);
    if (needleLen > XP3_SEARCH_STACK) {
        if (needleLen > SIZE_MAX / sizeof(size_t))
            border = NULL;
        else
            border = xmlMalloc(needleLen * sizeof(size_t));
        if (border == NULL) {
            xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
            return(-1);
        }
    }

    /* border[i]: the length of the longest proper prefix of
     * needle[0..i] that is also a suffix of it */
    border[0] = 0;
    for (i = 1, k = 0; i < needleLen; i++) {
        int c = collationByte(collation, needle[i]);

        while ((k > 0) && (c != collationByte(collation, needle[k])))
            k = border[k - 1];
        if (c == collationByte(collation, needle[k]))
            k++;
        border[i] = k;
    }
    for (i = 0, k = 0; i < hayLen; i++) {
        int c = collationByte(collation, haystack[i]);

        while ((k > 0) && (c != collationByte(collation, needle[k])))
            k = border[k - 1];
        if (c == collationByte(collation, needle[k]))
            k++;
        if (k == needleLen) {
            *at = i + 1 - needleLen;
            found = 1;
            break;
        }
    }
    if (border != fixed)
        xmlFree(border);
    return(found);
}

/*
 * Collations
 */

/*
 * Check the rest of a URI of the UCA family (F&O 3.1, 5.3.3) after its
 * path: nothing, or a query of keyword=value parameters separated by
 * semicolons, where the last of a keyword wins. Without a UCA
 * implementation, the codepoint collation is the fallback, which the
 * spec allows unless the query says fallback=no; keywords and values
 * the spec doesn't define are ignored then. Returns NULL if the URI is
 * accepted, or why not.
 */
static const char *
ucaRejection(const xmlChar *rest) {
    int fallback = 1;

    if (*rest == 0)
        return(NULL);
    if (*rest != '?')
        return("it is not a UCA collation URI");
    rest++;
    while (*rest != 0) {
        const xmlChar *start = rest;
        const xmlChar *equals = NULL;

        while ((*rest != 0) && (*rest != ';')) {
            if ((*rest == '=') && (equals == NULL))
                equals = rest;
            rest++;
        }
        /* Empty parameters, as in "?lang=en;", are skipped */
        if (rest > start) {
            if ((equals == NULL) || (equals == start))
                return("its query is not a list of keyword=value "
                       "parameters");
            if ((equals - start == 8) &&
                (memcmp(start, "fallback", 8) == 0))
                fallback = !((rest - equals == 3) &&
                             (memcmp(equals + 1, "no", 2) == 0));
        }
        if (*rest == ';')
            rest++;
    }
    if (!fallback)
        return("it asks for the UCA without fallback, which is not "
               "implemented");
    return(NULL);
}

/**
 * Resolve the collation of a call (F&O 3.1, 5.3.5): the collation
 * argument, or the default collation, the Unicode codepoint collation,
 * if there is none. A relative URI would be resolved against the
 * static base URI, which is absent, so it raises FOCH0002 as any URI
 * that isn't supported does.
 *
 * @param call       the call
 * @param uri        the collation argument, an xs:string; NULL for the
 *                   default collation
 * @param collation  set to the collation
 * @returns 0, or -1 after recording FOCH0002
 */
int
xp3FuncCollation(xp3FuncCall *call, const xp3Value *uri,
                 xp3Collation *collation) {
    static const size_t ucaLen = sizeof(XP3_COLLATION_URI_UCA) - 1;
    const char *rejection = "it is not supported";
    const xmlChar *name;

    *collation = XP3_COLLATION_CODEPOINT;
    if (uri == NULL)
        return(0);
    name = argString(uri);
    if (xmlStrEqual(name, BAD_CAST XP3_COLLATION_URI_CODEPOINT))
        return(0);
    if (xmlStrEqual(name, BAD_CAST XP3_COLLATION_URI_HTML_ASCII)) {
        *collation = XP3_COLLATION_HTML_ASCII;
        return(0);
    }
    if (strncmp((const char *) name, XP3_COLLATION_URI_UCA, ucaLen) == 0) {
        rejection = ucaRejection(name + ucaLen);
        if (rejection == NULL)
            return(0);
    }
    xp3FuncError(call, XP3_ERR_FOCH0002, "the collation \"%.100s\" is not "
                 "supported: %s", (const char *) name, rejection);
    return(-1);
}

/**
 * Compare two strings under a collation. The HTML ASCII
 * case-insensitive collation defines only equality; strings are
 * ordered as if A to Z were a to z.
 *
 * @param collation  the collation
 * @param str1       the first string
 * @param str2       the second string
 * @returns -1, 0 or 1 as str1 is before, equal to or after str2
 */
int
xp3CollationCompare(xp3Collation collation, const xmlChar *str1,
                    const xmlChar *str2) {
    while (1) {
        int c1 = collationByte(collation, *str1++);
        int c2 = collationByte(collation, *str2++);

        if (c1 != c2)
            return((c1 < c2) ? -1 : 1);
        if (c1 == 0)
            return(0);
    }
}

/* The collation of a function whose last parameter is the optional
 * collation, after arity others */
static int
callCollation(xp3FuncCall *call, xp3Value **args, int count, int arity,
              xp3Collation *collation) {
    return(xp3FuncCollation(call, (count > arity) ? args[arity] : NULL,
                            collation));
}

/*
 * Assembling and disassembling strings
 */

/**
 * fn:codepoints-to-string#1 (F&O 3.1, 5.2.1): the characters with the
 * code points given. Each must be an XML character, or FOCH0001 is
 * raised.
 *
 * @param call   the call
 * @param args   the code points, xs:integer*
 * @param count  1
 * @returns an xs:string, or NULL on error
 */
xp3Value *
xp3FnCodepointsToString(xp3FuncCall *call, xp3Value **args, int count) {
    xp3StrBuf buf;
    xp3Iter iter;
    xp3Item item;
    int found;

    (void) count;
    if (xp3FuncSteps(call, xp3ValueCount(args[0])) < 0)
        return(NULL);
    bufInit(&buf, "fn:codepoints-to-string");
    xp3IterInit(&iter, args[0]);
    while ((found = xp3IterNext(&iter, &item)) > 0) {
        xmlChar bytes[4];
        int64_t c = 0;
        int valid;

        valid = (xp3DecimalToInt64(item.u.atomic->u.dec, &c) == 0) &&
                (c >= 0) && (c <= 0x10FFFF) && xp3IsXmlChar((int) c);
        if (!valid) {
            xmlChar *text = xp3AtomicToString(item.u.atomic);

            if (text == NULL)
                xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
            else
                xp3FuncError(call, XP3_ERR_FOCH0001, "%.40s is not the "
                             "code point of an XML character",
                             (const char *) text);
            xmlFree(text);
        }
        xp3ItemUnref(item);
        if ((!valid) ||
            (bufAppend(call, &buf, bytes, (size_t) encodeChar((int) c,
                                                              bytes)) < 0))
            goto error;
    }
    if (found < 0) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        goto error;
    }
    return(bufFinish(call, &buf));

error:
    xmlFree(buf.data);
    return(NULL);
}

/**
 * fn:string-to-codepoints#1 (F&O 3.1, 5.2.2): the code points of the
 * characters of a string, or the empty sequence for an empty string.
 *
 * @param call   the call
 * @param args   the string, xs:string?
 * @param count  1
 * @returns the xs:integer values, or NULL on error
 */
xp3Value *
xp3FnStringToCodepoints(xp3FuncCall *call, xp3Value **args, int count) {
    const xmlChar *str = argString(args[0]);
    size_t len = strlen((const char *) str);
    xp3SeqBuilder builder;
    size_t pos = 0;

    (void) count;
    xp3FuncBuilderInit(call, &builder);
    while (pos < len) {
        xp3Atomic *atomic = xp3AtomicNewInteger(nextChar(str, len, &pos));

        if (atomic == NULL) {
            xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
            goto error;
        }
        if ((xp3FuncBuilderAdd(call, &builder,
                               xp3ItemFromAtomic(atomic)) < 0) ||
            (xp3FuncSteps(call, 1) < 0))
            goto error;
    }
    return(xp3FuncBuilderFinish(call, &builder));

error:
    xp3SeqBuilderClear(&builder);
    return(NULL);
}

/*
 * Comparing strings
 */

/**
 * fn:compare#2 and #3 (F&O 3.1, 5.3.6): -1, 0 or 1 as the first string
 * is before, equal to or after the second under the collation, or the
 * empty sequence if either is empty.
 *
 * @param call   the call
 * @param args   the two strings (xs:string?) and the collation
 *               (xs:string)
 * @param count  2 or 3
 * @returns an xs:integer or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnCompare(xp3FuncCall *call, xp3Value **args, int count) {
    xp3Collation collation;

    if (callCollation(call, args, count, 2, &collation) < 0)
        return(NULL);
    if ((xp3ValueCount(args[0]) == 0) || (xp3ValueCount(args[1]) == 0))
        return(xp3ValueEmpty());
    return(xp3FuncAtomic(call, xp3AtomicNewInteger(xp3CollationCompare(
        collation, argString(args[0]), argString(args[1])))));
}

/**
 * fn:codepoint-equal#2 (F&O 3.1, 5.3.7): whether two strings have the
 * same code points, or the empty sequence if either is empty.
 *
 * @param call   the call
 * @param args   the two strings, xs:string?
 * @param count  2
 * @returns an xs:boolean or the empty sequence, or NULL on error
 */
xp3Value *
xp3FnCodepointEqual(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    if ((xp3ValueCount(args[0]) == 0) || (xp3ValueCount(args[1]) == 0))
        return(xp3ValueEmpty());
    return(xp3FuncAtomic(call, xp3AtomicNewBoolean(
        xmlStrEqual(argString(args[0]), argString(args[1])))));
}

/* Whether str holds token between whitespace, under a collation */
static int
hasToken(xp3Collation collation, const xmlChar *str, const xmlChar *token,
         size_t tokenLen) {
    while (*str != 0) {
        const xmlChar *start;

        while (isSpace(*str))
            str++;
        start = str;
        while ((*str != 0) && !isSpace(*str))
            str++;
        if (((size_t) (str - start) == tokenLen) &&
            bytesEqual(collation, start, token, tokenLen))
            return(1);
    }
    return(0);
}

/**
 * fn:contains-token#2 and #3 (F&O 3.1, 5.3.9): whether a string of the
 * input, split at whitespace, has the token, stripped of whitespace at
 * both ends. A token with whitespace inside is never found.
 *
 * @param call   the call
 * @param args   the input (xs:string*), the token (xs:string) and the
 *               collation (xs:string)
 * @param count  2 or 3
 * @returns an xs:boolean, or NULL on error
 */
xp3Value *
xp3FnContainsToken(xp3FuncCall *call, xp3Value **args, int count) {
    const xmlChar *token = argString(args[1]);
    xp3Collation collation;
    size_t tokenLen;
    xp3Iter iter;
    xp3Item item;
    int contained = 0;
    int found = 0;

    if (callCollation(call, args, count, 2, &collation) < 0)
        return(NULL);
    while (isSpace(*token))
        token++;
    tokenLen = strlen((const char *) token);
    while ((tokenLen > 0) && isSpace(token[tokenLen - 1]))
        tokenLen--;
    if (tokenLen == 0)
        return(xp3FuncAtomic(call, xp3AtomicNewBoolean(0)));

    if (xp3FuncSteps(call, xp3ValueCount(args[0])) < 0)
        return(NULL);
    xp3IterInit(&iter, args[0]);
    while ((!contained) && ((found = xp3IterNext(&iter, &item)) > 0)) {
        contained = hasToken(collation, item.u.atomic->u.str, token,
                             tokenLen);
        xp3ItemUnref(item);
    }
    if ((!contained) && (found < 0)) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(NULL);
    }
    return(xp3FuncAtomic(call, xp3AtomicNewBoolean(contained)));
}

/*
 * Functions on string values
 */

/**
 * fn:concat (F&O 3.1, 5.4.1), with two or more arguments: the
 * arguments cast to xs:string and joined, the empty sequence as the
 * empty string.
 *
 * @param call   the call
 * @param args   the arguments, xs:anyAtomicType?
 * @param count  2 or more
 * @returns an xs:string, or NULL on error
 */
xp3Value *
xp3FnConcat(xp3FuncCall *call, xp3Value **args, int count) {
    xp3StrBuf buf;
    int i;

    bufInit(&buf, "fn:concat");
    for (i = 0; i < count; i++) {
        xp3Item item;
        int ret;

        if (xp3ValueCount(args[i]) == 0)
            continue;
        if (xp3ValueItemAt(args[i], 0, &item) <= 0) {
            xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
            goto error;
        }
        ret = bufAppendAtomic(call, &buf, item.u.atomic);
        xp3ItemUnref(item);
        if (ret < 0)
            goto error;
    }
    return(bufFinish(call, &buf));

error:
    xmlFree(buf.data);
    return(NULL);
}

/**
 * fn:string-join#1 and #2 (F&O 3.1, 5.4.2): the values cast to
 * xs:string and joined, with the separator between them, or the empty
 * string for the empty sequence.
 *
 * @param call   the call
 * @param args   the values (xs:anyAtomicType*) and the separator
 *               (xs:string)
 * @param count  1 or 2
 * @returns an xs:string, or NULL on error
 */
xp3Value *
xp3FnStringJoin(xp3FuncCall *call, xp3Value **args, int count) {
    const xmlChar *separator = (count == 2) ? argString(args[1]) :
                                              BAD_CAST "";
    size_t separatorLen = strlen((const char *) separator);
    xp3StrBuf buf;
    xp3Iter iter;
    xp3Item item;
    int first = 1;
    int found;

    if (xp3FuncSteps(call, xp3ValueCount(args[0])) < 0)
        return(NULL);
    bufInit(&buf, "fn:string-join");
    xp3IterInit(&iter, args[0]);
    while ((found = xp3IterNext(&iter, &item)) > 0) {
        int ret = 0;

        if (!first)
            ret = bufAppend(call, &buf, separator, separatorLen);
        if (ret == 0)
            ret = bufAppendAtomic(call, &buf, item.u.atomic);
        xp3ItemUnref(item);
        if (ret < 0)
            goto error;
        first = 0;
    }
    if (found < 0) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        goto error;
    }
    return(bufFinish(call, &buf));

error:
    xmlFree(buf.data);
    return(NULL);
}

/* fn:round on an xs:double: the nearest whole number, halves rounded
 * up. Unlike floor(x + 0.5), x - floor(x) is exact, so the largest
 * double below 0.5 rounds to 0. NaN and the infinities stay. */
static double
roundHalfUp(double x) {
    double whole = floor(x);

    if (x - whole >= 0.5)
        whole += 1.0;
    return(whole);
}

/**
 * fn:substring#2 and #3 (F&O 3.1, 5.4.3): the characters at the
 * positions p, counted from 1, with round($start) <= p and, with a
 * length, p < round($start) + round($length), compared as doubles: NaN
 * selects nothing, and so does -INF + INF.
 *
 * @param call   the call
 * @param args   the string (xs:string?), the start and the length
 *               (xs:double)
 * @param count  2 or 3
 * @returns an xs:string, or NULL on error
 */
xp3Value *
xp3FnSubstring(xp3FuncCall *call, xp3Value **args, int count) {
    const xmlChar *str = argString(args[0]);
    size_t len = strlen((const char *) str);
    double first = roundHalfUp(argDouble(args[1]));
    double end = 0;
    double position = 1;
    size_t pos = 0;
    size_t from;

    if (count == 3) {
        end = first + roundHalfUp(argDouble(args[2]));
        if (!(first < end))
            return(newString(call, BAD_CAST "", 0));
    } else if (isnan(first)) {
        return(newString(call, BAD_CAST "", 0));
    }
    while ((pos < len) && (position < first)) {
        nextChar(str, len, &pos);
        position += 1;
    }
    from = pos;
    if (count == 3) {
        while ((pos < len) && (position < end)) {
            nextChar(str, len, &pos);
            position += 1;
        }
    } else {
        pos = len;
    }
    return(newString(call, str + from, pos - from));
}

/**
 * fn:string-length#0 and #1 (F&O 3.1, 5.4.4): the number of
 * characters of a string, or of the string value of the context item.
 *
 * @param call   the call
 * @param args   the string, xs:string?
 * @param count  0 or 1
 * @returns an xs:integer, or NULL on error
 */
xp3Value *
xp3FnStringLength(xp3FuncCall *call, xp3Value **args, int count) {
    xp3Value *value = (count == 0) ? contextString(call) :
                                     xp3ValueRef(args[0]);
    const xmlChar *str;
    size_t len;
    size_t pos = 0;
    int64_t chars = 0;

    if (value == NULL)
        return(NULL);
    str = argString(value);
    len = strlen((const char *) str);
    while (pos < len) {
        nextChar(str, len, &pos);
        chars++;
    }
    xp3ValueUnref(value);
    return(xp3FuncAtomic(call, xp3AtomicNewInteger(chars)));
}

/**
 * fn:normalize-space#0 and #1 (F&O 3.1, 5.4.5): a string, or the
 * string value of the context item, without whitespace at either end,
 * and with each run of whitespace inside replaced by one space.
 *
 * @param call   the call
 * @param args   the string, xs:string?
 * @param count  0 or 1
 * @returns an xs:string, or NULL on error
 */
xp3Value *
xp3FnNormalizeSpace(xp3FuncCall *call, xp3Value **args, int count) {
    xp3Value *value = (count == 0) ? contextString(call) :
                                     xp3ValueRef(args[0]);
    const xmlChar *in;
    xmlChar *out;
    xp3Value *result;
    size_t len = 0;
    int space = 0;

    if (value == NULL)
        return(NULL);
    in = argString(value);
    while (isSpace(*in))
        in++;
    out = xmlMalloc(strlen((const char *) in) + 1);
    if (out == NULL) {
        xp3ValueUnref(value);
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(NULL);
    }
    for (; *in != 0; in++) {
        if (isSpace(*in)) {
            space = 1;
            continue;
        }
        if (space)
            out[len++] = ' ';
        space = 0;
        out[len++] = *in;
    }
    result = newString(call, out, len);
    xmlFree(out);
    xp3ValueUnref(value);
    return(result);
}

/* A character of the map string of fn:translate */
typedef struct {
    int from;
    size_t order;               /* its position in the map string */
    const xmlChar *to;          /* its replacement, NULL to remove it */
    size_t toLen;
} xp3Translation;

/* By character, then by position */
static int
compareTranslations(const void *a, const void *b) {
    const xp3Translation *left = a;
    const xp3Translation *right = b;

    if (left->from != right->from)
        return((left->from < right->from) ? -1 : 1);
    return((left->order < right->order) ? -1 :
           (left->order > right->order) ? 1 : 0);
}

/* The translation of a character, or NULL if it has none */
static const xp3Translation *
findTranslation(const xp3Translation *map, size_t count, int c) {
    size_t low = 0;
    size_t high = count;

    while (low < high) {
        size_t mid = low + (high - low) / 2;

        if (map[mid].from == c)
            return(&map[mid]);
        if (map[mid].from < c)
            low = mid + 1;
        else
            high = mid;
    }
    return(NULL);
}

/*
 * The map string of fn:translate as a table sorted by character, each
 * with the character at the same position of the replacement string,
 * where the first position of a character counts. Sets *result to the
 * table, NULL for an empty map string, and *count to its length.
 * Returns 0, or -1 after recording NO_MEMORY.
 */
static int
translationTable(xp3FuncCall *call, const xmlChar *map,
                 const xmlChar *trans, xp3Translation **result,
                 size_t *count) {
    size_t mapLen = strlen((const char *) map);
    size_t transLen = strlen((const char *) trans);
    xp3Translation *table;
    size_t pos = 0;
    size_t transPos = 0;
    size_t chars = 0;
    size_t kept;
    size_t i;

    *result = NULL;
    *count = 0;
    while (pos < mapLen) {
        nextChar(map, mapLen, &pos);
        chars++;
    }
    if (chars == 0)
        return(0);
    if (chars > SIZE_MAX / sizeof(xp3Translation))
        table = NULL;
    else
        table = xmlMalloc(chars * sizeof(xp3Translation));
    if (table == NULL) {
        xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
        return(-1);
    }
    for (i = 0, pos = 0; i < chars; i++) {
        table[i].from = nextChar(map, mapLen, &pos);
        table[i].order = i;
        table[i].to = NULL;
        table[i].toLen = 0;
        if (transPos < transLen) {
            size_t start = transPos;

            nextChar(trans, transLen, &transPos);
            table[i].to = trans + start;
            table[i].toLen = transPos - start;
        }
    }
    qsort(table, chars, sizeof(xp3Translation), compareTranslations);
    for (i = 1, kept = 1; i < chars; i++) {
        if (table[i].from != table[kept - 1].from)
            table[kept++] = table[i];
    }
    *result = table;
    *count = kept;
    return(0);
}

/**
 * fn:translate#3 (F&O 3.1, 5.4.9): each character of a string that is
 * in the map string replaced by the character at the same position of
 * the replacement string, or removed if that is shorter. Characters
 * are looked up in a sorted table, so the time is linear in the string
 * and log-linear in the map.
 *
 * @param call   the call
 * @param args   the string (xs:string?), the map string and the
 *               replacement string (xs:string)
 * @param count  3
 * @returns an xs:string, or NULL on error
 */
xp3Value *
xp3FnTranslate(xp3FuncCall *call, xp3Value **args, int count) {
    const xmlChar *str = argString(args[0]);
    size_t len = strlen((const char *) str);
    xp3Translation *table;
    size_t entries;
    xp3StrBuf buf;
    size_t pos = 0;

    (void) count;
    if (translationTable(call, argString(args[1]), argString(args[2]),
                         &table, &entries) < 0)
        return(NULL);
    if (table == NULL)
        return(newString(call, str, len));
    bufInit(&buf, "fn:translate");
    while (pos < len) {
        size_t start = pos;
        const xp3Translation *entry;
        int ret;

        entry = findTranslation(table, entries, nextChar(str, len, &pos));
        if (entry == NULL)
            ret = bufAppend(call, &buf, str + start, pos - start);
        else
            ret = bufAppend(call, &buf, entry->to, entry->toLen);
        if (ret < 0) {
            xmlFree(buf.data);
            xmlFree(table);
            return(NULL);
        }
    }
    xmlFree(table);
    return(bufFinish(call, &buf));
}

/*
 * Substring matching
 */

/*
 * Find the second string in the first, after resolving the collation,
 * for the functions of F&O 3.1, 5.5. The empty sequence is the empty
 * string, which every string contains at its start. Sets *at to the
 * byte offset of the match. Returns 1 if found, 0 if not, or -1 after
 * recording an error.
 */
static int
findArgument(xp3FuncCall *call, xp3Value **args, int count, size_t *at) {
    const xmlChar *haystack = argString(args[0]);
    const xmlChar *needle = argString(args[1]);
    xp3Collation collation;

    if (callCollation(call, args, count, 2, &collation) < 0)
        return(-1);
    return(findString(call, collation, haystack,
                      strlen((const char *) haystack), needle,
                      strlen((const char *) needle), at));
}

/**
 * fn:contains#2 and #3 (F&O 3.1, 5.5.1): whether the first string
 * contains the second. Every string contains the empty string.
 *
 * @param call   the call
 * @param args   the two strings (xs:string?) and the collation
 *               (xs:string)
 * @param count  2 or 3
 * @returns an xs:boolean, or NULL on error
 */
xp3Value *
xp3FnContains(xp3FuncCall *call, xp3Value **args, int count) {
    size_t at;
    int found = findArgument(call, args, count, &at);

    if (found < 0)
        return(NULL);
    return(xp3FuncAtomic(call, xp3AtomicNewBoolean(found)));
}

/**
 * fn:starts-with#2 and #3 (F&O 3.1, 5.5.2): whether the first string
 * starts with the second.
 *
 * @param call   the call
 * @param args   the two strings (xs:string?) and the collation
 *               (xs:string)
 * @param count  2 or 3
 * @returns an xs:boolean, or NULL on error
 */
xp3Value *
xp3FnStartsWith(xp3FuncCall *call, xp3Value **args, int count) {
    const xmlChar *str = argString(args[0]);
    const xmlChar *prefix = argString(args[1]);
    size_t prefixLen = strlen((const char *) prefix);
    xp3Collation collation;

    if (callCollation(call, args, count, 2, &collation) < 0)
        return(NULL);
    return(xp3FuncAtomic(call, xp3AtomicNewBoolean(
        (prefixLen <= strlen((const char *) str)) &&
        bytesEqual(collation, str, prefix, prefixLen))));
}

/**
 * fn:ends-with#2 and #3 (F&O 3.1, 5.5.3): whether the first string
 * ends with the second.
 *
 * @param call   the call
 * @param args   the two strings (xs:string?) and the collation
 *               (xs:string)
 * @param count  2 or 3
 * @returns an xs:boolean, or NULL on error
 */
xp3Value *
xp3FnEndsWith(xp3FuncCall *call, xp3Value **args, int count) {
    const xmlChar *str = argString(args[0]);
    const xmlChar *suffix = argString(args[1]);
    size_t len = strlen((const char *) str);
    size_t suffixLen = strlen((const char *) suffix);
    xp3Collation collation;

    if (callCollation(call, args, count, 2, &collation) < 0)
        return(NULL);
    return(xp3FuncAtomic(call, xp3AtomicNewBoolean(
        (suffixLen <= len) &&
        bytesEqual(collation, str + len - suffixLen, suffix, suffixLen))));
}

/**
 * fn:substring-before#2 and #3 (F&O 3.1, 5.5.4): the part of the first
 * string before the first occurrence of the second, or the empty
 * string if there is none.
 *
 * @param call   the call
 * @param args   the two strings (xs:string?) and the collation
 *               (xs:string)
 * @param count  2 or 3
 * @returns an xs:string, or NULL on error
 */
xp3Value *
xp3FnSubstringBefore(xp3FuncCall *call, xp3Value **args, int count) {
    size_t at;
    int found = findArgument(call, args, count, &at);

    if (found < 0)
        return(NULL);
    return(newString(call, argString(args[0]), found ? at : 0));
}

/**
 * fn:substring-after#2 and #3 (F&O 3.1, 5.5.5): the part of the first
 * string after the first occurrence of the second, or the empty string
 * if there is none. After the empty string comes the whole string.
 *
 * @param call   the call
 * @param args   the two strings (xs:string?) and the collation
 *               (xs:string)
 * @param count  2 or 3
 * @returns an xs:string, or NULL on error
 */
xp3Value *
xp3FnSubstringAfter(xp3FuncCall *call, xp3Value **args, int count) {
    const xmlChar *str = argString(args[0]);
    size_t skip = strlen((const char *) argString(args[1]));
    size_t at;
    int found = findArgument(call, args, count, &at);

    if (found < 0)
        return(NULL);
    if (!found)
        return(newString(call, BAD_CAST "", 0));
    return(newString(call, str + at + skip,
                     strlen((const char *) str) - at - skip));
}

/*
 * Splitting strings
 */

/**
 * fn:tokenize#1 (F&O 3.1, 5.6.5): the parts of a string between
 * whitespace, without empty ones, as tokenize(normalize-space($input),
 * ' ') gives them.
 *
 * @param call   the call
 * @param args   the string, xs:string?
 * @param count  1
 * @returns the xs:string values, or NULL on error
 */
xp3Value *
xp3FnTokenize(xp3FuncCall *call, xp3Value **args, int count) {
    const xmlChar *str = argString(args[0]);
    xp3SeqBuilder builder;

    (void) count;
    xp3FuncBuilderInit(call, &builder);
    while (*str != 0) {
        const xmlChar *start;
        xp3Atomic *token;

        while (isSpace(*str))
            str++;
        if (*str == 0)
            break;
        start = str;
        while ((*str != 0) && !isSpace(*str))
            str++;
        token = xp3AtomicNewString(XP3_TYPE_STRING, start,
                                   (size_t) (str - start));
        if (token == NULL) {
            xp3FuncError(call, XP3_ERR_NO_MEMORY, NULL);
            goto error;
        }
        if ((xp3FuncBuilderAdd(call, &builder,
                               xp3ItemFromAtomic(token)) < 0) ||
            (xp3FuncSteps(call, 1) < 0))
            goto error;
    }
    return(xp3FuncBuilderFinish(call, &builder));

error:
    xp3SeqBuilderClear(&builder);
    return(NULL);
}

/*
 * Functions on URIs
 */

/* The characters below U+0080 that a URI function leaves as they are;
 * it escapes all others */
typedef enum {
    XP3_URI_UNRESERVED,         /* fn:encode-for-uri */
    XP3_URI_IRI,                /* fn:iri-to-uri */
    XP3_URI_PRINTABLE           /* fn:escape-html-uri */
} xp3UriKeep;

static int
uriKeeps(xp3UriKeep keep, int c) {
    switch (keep) {
        case XP3_URI_UNRESERVED:
            /* The unreserved characters of RFC 3986, section 2.3 */
            return(((c >= 'A') && (c <= 'Z')) ||
                   ((c >= 'a') && (c <= 'z')) ||
                   ((c >= '0') && (c <= '9')) || (c == '-') ||
                   (c == '_') || (c == '.') || (c == '~'));
        case XP3_URI_IRI:
            /* Printable ASCII but space and the characters invalid in
             * an IRI; "%" stays, so the function is idempotent */
            return((c > 0x20) && (c < 0x7F) && (c != '<') && (c != '>') &&
                   (c != '"') && (c != '{') && (c != '}') && (c != '|') &&
                   (c != '\\') && (c != '^') && (c != '`'));
        default:
            return((c >= 0x20) && (c < 0x7F));
    }
}

/*
 * A string with every character a URI function doesn't keep replaced
 * by %HH for each byte of its UTF-8 form, in uppercase hex. A byte
 * that isn't UTF-8 is the character U+FFFD, so it becomes %EF%BF%BD.
 * Runs of characters kept are copied at once.
 */
static xp3Value *
escapeUri(xp3FuncCall *call, const xp3Value *arg, xp3UriKeep keep,
          const char *name) {
    static const char hex[] = "0123456789ABCDEF";
    const xmlChar *str = argString(arg);
    size_t len = strlen((const char *) str);
    size_t run = 0;
    size_t pos = 0;
    xp3StrBuf buf;

    bufInit(&buf, name);
    while (pos < len) {
        size_t start = pos;
        int c = nextChar(str, len, &pos);
        xmlChar bytes[4];
        xmlChar escaped[12];
        int count;
        int i;

        if ((c < 0x80) && uriKeeps(keep, c))
            continue;
        count = encodeChar(c, bytes);
        for (i = 0; i < count; i++) {
            escaped[3 * i] = '%';
            escaped[3 * i + 1] = (xmlChar) hex[bytes[i] >> 4];
            escaped[3 * i + 2] = (xmlChar) hex[bytes[i] & 0xF];
        }
        if ((bufAppend(call, &buf, str + run, start - run) < 0) ||
            (bufAppend(call, &buf, escaped, (size_t) (3 * count)) < 0)) {
            xmlFree(buf.data);
            return(NULL);
        }
        run = pos;
    }
    if (bufAppend(call, &buf, str + run, len - run) < 0) {
        xmlFree(buf.data);
        return(NULL);
    }
    return(bufFinish(call, &buf));
}

/**
 * fn:encode-for-uri#1 (F&O 3.1, 6.2): a string with every character
 * but the unreserved ones of RFC 3986 percent-encoded, "%" too, for
 * use in a path segment.
 *
 * @param call   the call
 * @param args   the string, xs:string?
 * @param count  1
 * @returns an xs:string, or NULL on error
 */
xp3Value *
xp3FnEncodeForUri(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(escapeUri(call, args[0], XP3_URI_UNRESERVED,
                     "fn:encode-for-uri"));
}

/**
 * fn:iri-to-uri#1 (F&O 3.1, 6.3): an IRI converted to a URI as RFC
 * 3987, section 3.1 does, with the characters outside x21 to x7E and
 * those invalid in an IRI, such as space and "<", percent-encoded. It
 * doesn't check that the string is an IRI.
 *
 * @param call   the call
 * @param args   the string, xs:string?
 * @param count  1
 * @returns an xs:string, or NULL on error
 */
xp3Value *
xp3FnIriToUri(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(escapeUri(call, args[0], XP3_URI_IRI, "fn:iri-to-uri"));
}

/**
 * fn:escape-html-uri#1 (F&O 3.1, 6.4): a string with every character
 * outside printable ASCII, codepoints 32 to 126, percent-encoded, as
 * HTML user agents treat URI attribute values.
 *
 * @param call   the call
 * @param args   the string, xs:string?
 * @param count  1
 * @returns an xs:string, or NULL on error
 */
xp3Value *
xp3FnEscapeHtmlUri(xp3FuncCall *call, xp3Value **args, int count) {
    (void) count;
    return(escapeUri(call, args[0], XP3_URI_PRINTABLE,
                     "fn:escape-html-uri"));
}
