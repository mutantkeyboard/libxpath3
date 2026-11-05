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
#include <stdint.h>

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
 * Built-in atomic types of XML Schema 1.1 and XDM 3.1, all in the
 * namespace http://www.w3.org/2001/XMLSchema, each after the type it
 * derives from. The values never change; new types are only appended.
 */
typedef enum {
    /** xs:anyAtomicType, the root of all atomic types */
    XP3_TYPE_ANY_ATOMIC = 0,
    /** xs:untypedAtomic, derived from xs:anyAtomicType */
    XP3_TYPE_UNTYPED_ATOMIC = 1,
    /** xs:string, derived from xs:anyAtomicType */
    XP3_TYPE_STRING = 2,
    /** xs:normalizedString, derived from xs:string */
    XP3_TYPE_NORMALIZED_STRING = 3,
    /** xs:token, derived from xs:normalizedString */
    XP3_TYPE_TOKEN = 4,
    /** xs:language, derived from xs:token */
    XP3_TYPE_LANGUAGE = 5,
    /** xs:NMTOKEN, derived from xs:token */
    XP3_TYPE_NMTOKEN = 6,
    /** xs:Name, derived from xs:token */
    XP3_TYPE_NAME = 7,
    /** xs:NCName, derived from xs:Name */
    XP3_TYPE_NCNAME = 8,
    /** xs:ID, derived from xs:NCName */
    XP3_TYPE_ID = 9,
    /** xs:IDREF, derived from xs:NCName */
    XP3_TYPE_IDREF = 10,
    /** xs:ENTITY, derived from xs:NCName */
    XP3_TYPE_ENTITY = 11,
    /** xs:boolean, derived from xs:anyAtomicType */
    XP3_TYPE_BOOLEAN = 12,
    /** xs:decimal, derived from xs:anyAtomicType */
    XP3_TYPE_DECIMAL = 13,
    /** xs:integer, derived from xs:decimal */
    XP3_TYPE_INTEGER = 14,
    /** xs:nonPositiveInteger, derived from xs:integer */
    XP3_TYPE_NON_POSITIVE_INTEGER = 15,
    /** xs:negativeInteger, derived from xs:nonPositiveInteger */
    XP3_TYPE_NEGATIVE_INTEGER = 16,
    /** xs:long, derived from xs:integer */
    XP3_TYPE_LONG = 17,
    /** xs:int, derived from xs:long */
    XP3_TYPE_INT = 18,
    /** xs:short, derived from xs:int */
    XP3_TYPE_SHORT = 19,
    /** xs:byte, derived from xs:short */
    XP3_TYPE_BYTE = 20,
    /** xs:nonNegativeInteger, derived from xs:integer */
    XP3_TYPE_NON_NEGATIVE_INTEGER = 21,
    /** xs:unsignedLong, derived from xs:nonNegativeInteger */
    XP3_TYPE_UNSIGNED_LONG = 22,
    /** xs:unsignedInt, derived from xs:unsignedLong */
    XP3_TYPE_UNSIGNED_INT = 23,
    /** xs:unsignedShort, derived from xs:unsignedInt */
    XP3_TYPE_UNSIGNED_SHORT = 24,
    /** xs:unsignedByte, derived from xs:unsignedShort */
    XP3_TYPE_UNSIGNED_BYTE = 25,
    /** xs:positiveInteger, derived from xs:nonNegativeInteger */
    XP3_TYPE_POSITIVE_INTEGER = 26,
    /** xs:float, derived from xs:anyAtomicType */
    XP3_TYPE_FLOAT = 27,
    /** xs:double, derived from xs:anyAtomicType */
    XP3_TYPE_DOUBLE = 28,
    /** xs:duration, derived from xs:anyAtomicType */
    XP3_TYPE_DURATION = 29,
    /** xs:yearMonthDuration, derived from xs:duration */
    XP3_TYPE_YEAR_MONTH_DURATION = 30,
    /** xs:dayTimeDuration, derived from xs:duration */
    XP3_TYPE_DAY_TIME_DURATION = 31,
    /** xs:dateTime, derived from xs:anyAtomicType */
    XP3_TYPE_DATE_TIME = 32,
    /** xs:dateTimeStamp, derived from xs:dateTime */
    XP3_TYPE_DATE_TIME_STAMP = 33,
    /** xs:time, derived from xs:anyAtomicType */
    XP3_TYPE_TIME = 34,
    /** xs:date, derived from xs:anyAtomicType */
    XP3_TYPE_DATE = 35,
    /** xs:gYearMonth, derived from xs:anyAtomicType */
    XP3_TYPE_G_YEAR_MONTH = 36,
    /** xs:gYear, derived from xs:anyAtomicType */
    XP3_TYPE_G_YEAR = 37,
    /** xs:gMonthDay, derived from xs:anyAtomicType */
    XP3_TYPE_G_MONTH_DAY = 38,
    /** xs:gDay, derived from xs:anyAtomicType */
    XP3_TYPE_G_DAY = 39,
    /** xs:gMonth, derived from xs:anyAtomicType */
    XP3_TYPE_G_MONTH = 40,
    /** xs:hexBinary, derived from xs:anyAtomicType */
    XP3_TYPE_HEX_BINARY = 41,
    /** xs:base64Binary, derived from xs:anyAtomicType */
    XP3_TYPE_BASE64_BINARY = 42,
    /** xs:anyURI, derived from xs:anyAtomicType */
    XP3_TYPE_ANY_URI = 43,
    /** xs:QName, derived from xs:anyAtomicType */
    XP3_TYPE_QNAME = 44,
    /** xs:NOTATION, derived from xs:anyAtomicType */
    XP3_TYPE_NOTATION = 45
} xp3AtomicType;

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

/**
 * The call of an extension function, passed to its implementation.
 * Opaque, and valid only during that call.
 */
typedef struct _xp3FunctionCall xp3FunctionCall;

/**
 * The implementation of an extension function.
 *
 * It returns a new sequence, which the library owns from then on: one
 * made with the xp3New*Sequence() functions, or an argument kept with
 * xp3SequenceRef(), returning the pointer that gives, which may be a
 * copy, and NULL if it failed. The result is converted to the
 * declared result type and checked like the arguments, so a wrong
 * result raises XPTY0004 instead of reaching the evaluator.
 *
 * On error it returns NULL, after xp3FunctionError(); NULL without an
 * error is taken as a failed allocation (NO_MEMORY), since that's why
 * the xp3New*Sequence() functions return NULL.
 *
 * A compiled expression can be evaluated by several threads at once,
 * so the implementation may run concurrently; it may evaluate other
 * expressions, but not with the dynamic context of the call.
 *
 * @param call      the call, for errors and the evaluation's user data
 * @param args      the arguments, converted to the declared parameter
 *                  types by the function conversion rules (XPath 3.1,
 *                  3.1.5.2); borrowed, valid during the call
 * @param count     the number of arguments, the arity of the signature
 * @param userData  the pointer given to xp3RegisterFunction()
 * @returns the result, or NULL on error
 */
typedef xp3Sequence *(*xp3FunctionImpl)(xp3FunctionCall *call,
                                        xp3Sequence **args, int count,
                                        void *userData);

/**
 * Properties of an extension function (F&O 3.1, 1.7.4). Without
 * flags, a function is taken to be nondeterministic and is called on
 * each evaluation, also with constant arguments.
 */
typedef enum {
    /** Same arguments give the same result, without side effects, so a
     * call with constant arguments may be evaluated once, at compile
     * time (constant folding). */
    XP3_FUNCTION_DETERMINISTIC = 1 << 0
} xp3FunctionFlags;

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
 * Extension functions
 */
XP3PUBFUN int
		    xp3RegisterFunction		(xp3StaticContext *ctx,
						 const xmlChar *signature,
						 int flags,
						 xp3FunctionImpl impl,
						 void *userData,
						 xp3Error **error);
XP3PUBFUN void
		    xp3FunctionError		(xp3FunctionCall *call,
						 const xmlChar *uri,
						 const xmlChar *localName,
						 const xmlChar *message);
XP3PUBFUN void *
		    xp3FunctionUserData		(xp3FunctionCall *call);

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
XP3PUBFUN int
		    xp3SetMaxSteps		(xp3DynamicContext *ctx,
						 uint64_t max);
XP3PUBFUN int
		    xp3SetDynamicContextUserData(xp3DynamicContext *ctx,
						 void *userData);

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
 * Atomic types
 */
XP3PUBFUN const xmlChar *
		    xp3AtomicTypeName		(xp3AtomicType type);
XP3PUBFUN int
		    xp3AtomicTypeDerivesFrom	(xp3AtomicType type,
						 xp3AtomicType ancestor);

/*
 * Sequences
 */
XP3PUBFUN xp3Sequence *
		    xp3NewNodeSequence		(xmlNodePtr *nodes,
						 size_t count);
XP3PUBFUN xp3Sequence *
		    xp3NewStringSequence	(const xmlChar **strs,
						 size_t count);
XP3PUBFUN xp3Sequence *
		    xp3NewIntegerSequence	(const int64_t *values,
						 size_t count);
XP3PUBFUN xp3Sequence *
		    xp3NewDoubleSequence	(const double *values,
						 size_t count);
XP3PUBFUN xp3Sequence *
		    xp3NewBooleanSequence	(const int *values,
						 size_t count);
XP3PUBFUN xp3Sequence *
		    xp3NewAtomicSequence	(xp3AtomicType type,
						 const xmlChar **lexicals,
						 size_t count,
						 xp3Error **error);
XP3PUBFUN void
		    xp3FreeSequence		(xp3Sequence *seq);
XP3PUBFUN xp3Sequence *
		    xp3SequenceRef		(xp3Sequence *seq);
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
XP3PUBFUN int
		    xp3SequenceAtomicType	(const xp3Sequence *seq,
						 size_t index);
XP3PUBFUN int
		    xp3SequenceInteger		(const xp3Sequence *seq,
						 size_t index,
						 int64_t *value);
XP3PUBFUN int
		    xp3SequenceDouble		(const xp3Sequence *seq,
						 size_t index,
						 double *value);
XP3PUBFUN int
		    xp3SequenceBoolean		(const xp3Sequence *seq,
						 size_t index);

#ifdef __cplusplus
}
#endif

#endif /* XP3_XPATH3_H */
