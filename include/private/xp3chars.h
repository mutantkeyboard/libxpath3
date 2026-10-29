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

#include <libxml/xmlstring.h>

int
xp3GetUTF8Char(const xmlChar *utf, int *len);

int
xp3IsXmlChar(int c);

int
xp3IsNameStartChar(int c);

int
xp3IsNameChar(int c);

#endif /* XP3_XP3CHARS_H */
