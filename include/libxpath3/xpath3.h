/**
 * @file
 *
 * @brief XPath 3.1 for libxml2 documents
 *
 * Public interface of libxpath3: compiling and evaluating XPath
 * expressions on libxml2 documents.
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XPATH3_H
#define XP3_XPATH3_H

#include <libxpath3/xp3version.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Marks a function as part of the public API.
 *
 * The library is built with hidden symbol visibility, so only functions
 * marked with this macro are exported.
 */
#if defined(_WIN32) && !defined(XP3_STATIC)
  #ifdef IN_LIBXPATH3
    #define XP3PUBFUN __declspec(dllexport)
  #else
    #define XP3PUBFUN __declspec(dllimport)
  #endif
#elif defined(__GNUC__) && defined(IN_LIBXPATH3)
  #define XP3PUBFUN __attribute__((visibility("default")))
#else
  #define XP3PUBFUN
#endif

XP3PUBFUN const char *
xp3GetVersion(void);

XP3PUBFUN int
xp3GetVersionNumber(void);

#ifdef __cplusplus
}
#endif

#endif /* XP3_XPATH3_H */
