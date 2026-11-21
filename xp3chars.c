/*
 * xp3chars.c: character classes from XML 1.0 (Fifth Edition)
 *
 * libxml2 no longer exports its character class functions, so the
 * ranges are spelled out here. The lexer and, later, the regex engine
 * use them, together with a strict UTF-8 decoder.
 *
 * Text that libxml2 stored without checking can hold bytes that are
 * not UTF-8. The string functions count each such byte as one
 * character, U+FFFD, and the helpers after the decoder apply that one
 * rule (xp3NextChar()) to matching and comparing too, so that all of
 * them agree.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdint.h>
#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

#include "private/xp3chars.h"

/**
 * Decode one UTF-8 character, as xmlGetUTF8Char() does but without
 * accepting the lead bytes 0xF8 to 0xFF. They never appear in UTF-8
 * (RFC 3629, section 1), and xmlGetUTF8Char() reads any lead byte from
 * 0xF0 by its last three bits, so F8 90 80 80 would decode as U+10000.
 * libxml2 rejects them from 2.16.0, so the check can go once that is
 * the oldest version supported.
 *
 * @param utf  the bytes
 * @param len  the number of bytes available; set to the length of the
 *             character, or 0 for invalid UTF-8
 * @returns the code point, or -1 for invalid UTF-8
 */
int
xp3GetUTF8Char(const xmlChar *utf, int *len) {
    if (utf[0] >= 0xF8) {
        *len = 0;
        return(-1);
    }
    return(xmlGetUTF8Char(utf, len));
}

/**
 * Decode the character at *pos of a string and move past it. A byte
 * that doesn't start valid UTF-8 is one character, U+FFFD.
 *
 * @param str  the string
 * @param len  its length in bytes, more than *pos
 * @param pos  the byte offset of the character; moved past it
 * @returns the code point
 */
int
xp3NextChar(const xmlChar *str, size_t len, size_t *pos) {
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

/**
 * Encode a code point, at most U+10FFFF, as UTF-8.
 *
 * @param c    the code point
 * @param out  room for 4 bytes
 * @returns the number of bytes written
 */
int
xp3EncodeChar(int c, xmlChar *out) {
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

/**
 * Check whether every byte of a string is part of a character that
 * xp3NextChar() decodes, so that no byte counts as U+FFFD. Unlike
 * xmlCheckUTF8(), it rejects overlong forms, surrogates and code
 * points above U+10FFFF, as the decoder does.
 *
 * @param str  the string
 * @param len  its length in bytes
 * @returns 1 if valid, 0 otherwise
 */
int
xp3IsValidUTF8(const xmlChar *str, size_t len) {
    size_t pos = 0;

    while (pos < len) {
        size_t start = pos;

        xp3NextChar(str, len, &pos);
        if ((pos - start == 1) && (str[start] >= 0x80))
            return(0);
    }
    return(1);
}

/**
 * Copy a string with each byte that xp3NextChar() decodes as U+FFFD
 * replaced by the UTF-8 of U+FFFD, EF BF BD, so that it can be matched
 * and compared byte by byte as valid UTF-8.
 *
 * @param str      the string
 * @param len      its length in bytes
 * @param copyLen  set to the length of the copy
 * @returns the copy, NUL-terminated, to free with xmlFree(), or NULL if
 *          memory allocation failed
 */
xmlChar *
xp3ReplaceInvalidUTF8(const xmlChar *str, size_t len, size_t *copyLen) {
    xmlChar *copy;
    size_t invalid = 0;
    size_t pos = 0;
    size_t used = 0;

    while (pos < len) {
        size_t start = pos;

        xp3NextChar(str, len, &pos);
        if ((pos - start == 1) && (str[start] >= 0x80))
            invalid++;
    }
    if (invalid > (SIZE_MAX - len - 1) / 2)
        return(NULL);
    copy = xmlMalloc(len + 2 * invalid + 1);
    if (copy == NULL)
        return(NULL);
    for (pos = 0; pos < len;) {
        size_t start = pos;

        xp3NextChar(str, len, &pos);
        if ((pos - start == 1) && (str[start] >= 0x80)) {
            memcpy(copy + used, "\xEF\xBF\xBD", 3);
            used += 3;
        } else {
            memcpy(copy + used, str + start, pos - start);
            used += pos - start;
        }
    }
    copy[used] = 0;
    *copyLen = used;
    return(copy);
}

/* A character as compared, with A to Z as a to z if fold is set */
static int
foldChar(int c, int fold) {
    if (fold && (c >= 'A') && (c <= 'Z'))
        return(c - 'A' + 'a');
    return(c);
}

/**
 * Compare two strings by code point, decoded as xp3NextChar() does, so
 * that a byte that isn't UTF-8 compares as U+FFFD. Valid UTF-8 orders
 * by bytes as by code points, so such strings are compared byte by
 * byte.
 *
 * @param str1       the first string
 * @param str2       the second string
 * @param foldAscii  if set, A to Z compare as a to z
 * @returns -1, 0 or 1 as str1 is before, equal to or after str2
 */
int
xp3CompareChars(const xmlChar *str1, const xmlChar *str2, int foldAscii) {
    size_t len1 = strlen((const char *) str1);
    size_t len2 = strlen((const char *) str2);
    size_t pos1 = 0;
    size_t pos2 = 0;

    if (xp3IsValidUTF8(str1, len1) && xp3IsValidUTF8(str2, len2)) {
        while (1) {
            int c1 = foldChar(*str1++, foldAscii);
            int c2 = foldChar(*str2++, foldAscii);

            if (c1 != c2)
                return((c1 < c2) ? -1 : 1);
            if (c1 == 0)
                return(0);
        }
    }
    while ((pos1 < len1) && (pos2 < len2)) {
        int c1 = foldChar(xp3NextChar(str1, len1, &pos1), foldAscii);
        int c2 = foldChar(xp3NextChar(str2, len2, &pos2), foldAscii);

        if (c1 != c2)
            return((c1 < c2) ? -1 : 1);
    }
    if (pos1 < len1)
        return(1);
    return((pos2 < len2) ? -1 : 0);
}

/**
 * Check whether a code point is allowed in XML (production Char).
 *
 * @param c  the code point
 * @returns 1 if allowed, 0 otherwise
 */
int
xp3IsXmlChar(int c) {
    return((c == 0x9) || (c == 0xA) || (c == 0xD) ||
           ((c >= 0x20) && (c <= 0xD7FF)) ||
           ((c >= 0xE000) && (c <= 0xFFFD)) ||
           ((c >= 0x10000) && (c <= 0x10FFFF)));
}

/**
 * Check whether a code point can start an NCName (production
 * NameStartChar without the colon).
 *
 * @param c  the code point
 * @returns 1 if it can, 0 otherwise
 */
int
xp3IsNameStartChar(int c) {
    if (c < 0x80)
        return(((c >= 'A') && (c <= 'Z')) ||
               ((c >= 'a') && (c <= 'z')) ||
               (c == '_'));

    return(((c >= 0xC0) && (c <= 0xD6)) ||
           ((c >= 0xD8) && (c <= 0xF6)) ||
           ((c >= 0xF8) && (c <= 0x2FF)) ||
           ((c >= 0x370) && (c <= 0x37D)) ||
           ((c >= 0x37F) && (c <= 0x1FFF)) ||
           ((c >= 0x200C) && (c <= 0x200D)) ||
           ((c >= 0x2070) && (c <= 0x218F)) ||
           ((c >= 0x2C00) && (c <= 0x2FEF)) ||
           ((c >= 0x3001) && (c <= 0xD7FF)) ||
           ((c >= 0xF900) && (c <= 0xFDCF)) ||
           ((c >= 0xFDF0) && (c <= 0xFFFD)) ||
           ((c >= 0x10000) && (c <= 0xEFFFF)));
}

/**
 * Check whether a code point can continue an NCName (production
 * NameChar without the colon).
 *
 * @param c  the code point
 * @returns 1 if it can, 0 otherwise
 */
int
xp3IsNameChar(int c) {
    if (xp3IsNameStartChar(c))
        return(1);
    return((c == '-') || (c == '.') ||
           ((c >= '0') && (c <= '9')) ||
           (c == 0xB7) ||
           ((c >= 0x300) && (c <= 0x36F)) ||
           ((c >= 0x203F) && (c <= 0x2040)));
}
