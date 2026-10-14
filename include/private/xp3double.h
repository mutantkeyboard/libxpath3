/**
 * @file
 *
 * @brief Converting doubles and floats to and from strings (internal)
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XP3DOUBLE_H
#define XP3_XP3DOUBLE_H

#include <libxml/xmlstring.h>

double
xp3DoubleNaN(void);

double
xp3DoubleParse(const char *str);

float
xp3FloatParse(const char *str);

xmlChar *
xp3DoubleToString(double value);

xmlChar *
xp3FloatToString(float value);

#endif /* XP3_XP3DOUBLE_H */
