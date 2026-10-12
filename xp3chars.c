/*
 * xp3chars.c: character classes from XML 1.0 (Fifth Edition)
 *
 * libxml2 no longer exports its character class functions, so the
 * ranges are spelled out here. The lexer and, later, the regex engine
 * use them.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include "private/xp3chars.h"

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
