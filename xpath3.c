/*
 * xpath3.c: library-wide functions
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <libxpath3/xpath3.h>

/**
 * Get the version of the library that is linked at run time.
 *
 * Compare with #XP3_VERSION_STRING to detect a mismatch between the
 * headers an application was compiled with and the library it runs with.
 *
 * @returns the version string, for example "0.0.1"
 */
const char *
xp3GetVersion(void) {
    return(XP3_VERSION_STRING);
}

/**
 * Get the version of the library that is linked at run time as a number.
 *
 * @returns major * 10000 + minor * 100 + micro, for example 1 for 0.0.1
 */
int
xp3GetVersionNumber(void) {
    return(XP3_VERSION_NUMBER);
}
