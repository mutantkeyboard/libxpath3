/**
 * @file
 *
 * @brief Parser for XPath 3.1 expressions (internal)
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XP3PARSER_H
#define XP3_XP3PARSER_H

#include <stddef.h>

#include <libxml/xmlstring.h>

#include <libxpath3/xpath3.h>

#include "private/xp3ast.h"

xp3Ast *
xp3Parse(const xp3StaticContext *ctx, const xmlChar *expr, size_t length,
         xp3Error **err);

#endif /* XP3_XP3PARSER_H */
