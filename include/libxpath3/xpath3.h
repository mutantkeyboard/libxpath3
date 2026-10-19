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

#include <stddef.h>

#include <libxpath3/xp3version.h>
#include <libxml/tree.h>

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

typedef struct _xp3Error xp3Error;
typedef struct _xp3StaticContext xp3StaticContext;
typedef struct _xp3Expr xp3Expr;
typedef struct _xp3DynamicContext xp3DynamicContext;

/**
 * A sequence of items, the value of every XPath expression. Sequences
 * are immutable and reference counted: one reference never belongs to
 * more than one thread at a time.
 */
typedef struct _xp3Value xp3Sequence;

/**
 * Kinds of items in a sequence.
 */
typedef enum {
    /** a node of a libxml2 document */
    XP3_ITEM_NODE = 0,
    /** an atomic value, such as a string or a number */
    XP3_ITEM_ATOMIC,
    /** a function item */
    XP3_ITEM_FUNCTION,
    /** a map */
    XP3_ITEM_MAP,
    /** an array */
    XP3_ITEM_ARRAY
} xp3ItemKind;

/**
 * Specify the language level of the XPath expression. The default is
 * #XP3_XPATH_31.
 */
typedef enum {
    /** XPath 2.0 */
    XP3_XPATH_20 = 20,
    /** XPath 3.0 */
    XP3_XPATH_30 = 30,
    /** XPath 3.1 */
    XP3_XPATH_31 = 31
} xp3LanguageLevel;

/*
 * Version
 */
XP3PUBFUN const char *
		    xp3GetVersion		(void);
XP3PUBFUN int
		    xp3GetVersionNumber		(void);

/*
 * Errors
 */
XP3PUBFUN const xmlChar *
		    xp3ErrorCode		(const xp3Error *error);
XP3PUBFUN const xmlChar *
		    xp3ErrorNamespace		(const xp3Error *error);
XP3PUBFUN const xmlChar *
		    xp3ErrorMessage		(const xp3Error *error);
XP3PUBFUN int
		    xp3ErrorLine		(const xp3Error *error);
XP3PUBFUN int
		    xp3ErrorColumn		(const xp3Error *error);
XP3PUBFUN void
		    xp3ErrorFree		(xp3Error *error);

/*
 * Static context
 */
XP3PUBFUN xp3StaticContext *
		    xp3NewStaticContext		(void);
XP3PUBFUN void
		    xp3FreeStaticContext	(xp3StaticContext *ctx);
XP3PUBFUN int
		    xp3SetStaticContextLanguageLevel(xp3StaticContext *ctx,
						 xp3LanguageLevel level);
XP3PUBFUN int
		    xp3DeclareNamespace		(xp3StaticContext *ctx,
						 const xmlChar *prefix,
						 const xmlChar *uri);
XP3PUBFUN int
		    xp3SetDefaultElementNamespace(xp3StaticContext *ctx,
						 const xmlChar *uri);
XP3PUBFUN int
		    xp3DeclareVariable		(xp3StaticContext *ctx,
						 const xmlChar *uri,
						 const xmlChar *localName);

/*
 * Compiling
 */
XP3PUBFUN xp3Expr *
		    xp3Compile			(const xp3StaticContext *ctx,
						 const xmlChar *expr,
						 xp3Error **error);
XP3PUBFUN void
		    xp3FreeExpr			(xp3Expr *expr);

/*
 * Dynamic context
 */
XP3PUBFUN xp3DynamicContext *
		    xp3NewDynamicContext	(void);
XP3PUBFUN void
		    xp3FreeDynamicContext	(xp3DynamicContext *ctx);
XP3PUBFUN int
		    xp3SetContextNode		(xp3DynamicContext *ctx,
						 xmlNodePtr node);
XP3PUBFUN int
		    xp3BindVariable		(xp3DynamicContext *ctx,
						 const xmlChar *uri,
						 const xmlChar *localName,
						 xp3Sequence *value);
XP3PUBFUN int
		    xp3SetMaxSequenceLength	(xp3DynamicContext *ctx,
						 size_t max);

/*
 * Evaluating
 */
XP3PUBFUN xp3Sequence *
		    xp3Evaluate			(const xp3Expr *expr,
						 xp3DynamicContext *ctx,
						 xp3Error **error);
XP3PUBFUN int
		    xp3EvaluateBoolean		(const xp3Expr *expr,
						 xp3DynamicContext *ctx,
						 xp3Error **error);

/*
 * Sequences
 */
XP3PUBFUN xp3Sequence *
		    xp3NewNodeSequence		(xmlNodePtr *nodes,
						 size_t count);
XP3PUBFUN xp3Sequence *
		    xp3NewStringSequence	(const xmlChar *str);
XP3PUBFUN void
		    xp3FreeSequence		(xp3Sequence *seq);
XP3PUBFUN size_t
		    xp3SequenceLength		(const xp3Sequence *seq);
XP3PUBFUN int
		    xp3SequenceItemKind		(const xp3Sequence *seq,
						 size_t index);
XP3PUBFUN xmlNodePtr
		    xp3SequenceNode		(const xp3Sequence *seq,
						 size_t index);
XP3PUBFUN xmlChar *
		    xp3SequenceStringValue	(const xp3Sequence *seq,
						 size_t index);

#ifdef __cplusplus
}
#endif

#endif /* XP3_XPATH3_H */
