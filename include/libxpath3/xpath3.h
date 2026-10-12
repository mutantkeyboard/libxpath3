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
#include <libxml/tree.h>

#ifdef __cplusplus
extern "C"
{
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

  typedef struct _xp3Error xp3Error;
  typedef struct _xp3StaticContext xp3StaticContext;

  // Specify the language level of the XPath expression. The default is XP3_XPATH_31.
  typedef enum
  {
    XP3_XPATH_20 = 20,
    XP3_XPATH_30 = 30,
    XP3_XPATH_31 = 31
  } xp3LanguageLevel;

  /* Version */
  XP3PUBFUN const char *
  xp3GetVersion(void);

  XP3PUBFUN int
  xp3GetVersionNumber(void);

  /* Errors */
  XP3PUBFUN const xmlChar *xp3ErrorCode(const xp3Error *error);
  XP3PUBFUN const xmlChar *xp3ErrorNamespace(const xp3Error *error);
  XP3PUBFUN const xmlChar *xp3ErrorMessage(const xp3Error *error);
  XP3PUBFUN int xp3ErrorLine(const xp3Error *error);
  XP3PUBFUN int xp3ErrorColumn(const xp3Error *error);
  XP3PUBFUN void xp3ErrorFree(xp3Error *error);

  /* Static context */
  XP3PUBFUN xp3StaticContext *xp3NewStaticContext(void);
  XP3PUBFUN void xp3FreeStaticContext(xp3StaticContext *ctx);
  XP3PUBFUN int xp3SetStaticContextLanguageLevel(xp3StaticContext *ctx, xp3LanguageLevel level);
  XP3PUBFUN int xp3DeclareNamespace(xp3StaticContext *ctx, const xmlChar *prefix, const xmlChar *uri);

#ifdef __cplusplus
}
#endif

#endif /* XP3_XPATH3_H */
