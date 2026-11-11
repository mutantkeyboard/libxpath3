/**
 * @file
 *
 * @brief UTF-8 decoding and character classes from XML 1.0 (internal)
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XP3CHARS_H
#define XP3_XP3CHARS_H

#include <stddef.h>

#include <libxml/xmlstring.h>

int
xp3GetUTF8Char(const xmlChar *utf, int *len);

int
xp3NextChar(const xmlChar *str, size_t len, size_t *pos);

int
xp3IsValidUTF8(const xmlChar *str, size_t len);

xmlChar *
xp3ReplaceInvalidUTF8(const xmlChar *str, size_t len, size_t *copyLen);

int
xp3CompareChars(const xmlChar *str1, const xmlChar *str2, int foldAscii);

int
xp3IsXmlChar(int c);

int
xp3IsNameStartChar(int c);

int
xp3IsNameChar(int c);

#endif /* XP3_XP3CHARS_H */
