/**
 * @file
 *
 * @brief Built-in function table (internal)
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XP3FUNC_H
#define XP3_XP3FUNC_H

#include <stddef.h>
#include <stdint.h>

#include <libxml/xmlstring.h>

#include <libxpath3/xpath3.h>
#include "private/xp3ast.h"
#include "private/xp3error.h"
#include "private/xp3value.h"

/* Namespace of a built-in function */
typedef enum {
    XP3_FUNC_NS_FN = 0,         /* http://www.w3.org/2005/xpath-functions */
    XP3_FUNC_NS_MATH,           /* .../xpath-functions/math */
    XP3_FUNC_NS_MAP,            /* .../xpath-functions/map */
    XP3_FUNC_NS_ARRAY           /* .../xpath-functions/array */
} xp3FuncNamespace;

/*
 * Flags of a signature. Apart from XP3_FUNC_VARIADIC, they are the
 * function properties of F&O 3.1, 1.7.4. Constant folding needs them:
 * a call can only be folded if none of the first four are set.
 * XP3_FUNC_ORDER does not prevent folding, because any one order is a
 * valid result; it tells the evaluator that the order of the result
 * cannot be relied on between calls.
 */
#define XP3_FUNC_VARIADIC (1 << 0)  /* fn:concat: also any higher arity */
#define XP3_FUNC_FOCUS (1 << 1)     /* focus-dependent */
#define XP3_FUNC_CONTEXT (1 << 2)   /* context-dependent */
#define XP3_FUNC_NONDETERMINISTIC (1 << 3)
#define XP3_FUNC_HIGHER_ORDER (1 << 4) /* takes or returns a function */
#define XP3_FUNC_ORDER (1 << 5)     /* nondeterministic with respect to
                                     * ordering: fn:distinct-values,
                                     * fn:unordered, map:keys... */

/*
 * The type of a parameter or result, as a SequenceType of the parse
 * tree (xp3ast.h) would give it:
 *
 *   ATOMIC_TYPE     type: an xp3TypeId, or XP3_SCHEMA_NUMERIC
 *   KIND_TEST       type: an xp3KindTestKind; element(N) and
 *                   document-node(E) keep only their kind
 *   ANY_ITEM_TYPE   item()
 *   FUNCTION_TEST,  function(*), map(*) and array(*); typed tests
 *   MAP_TEST,       keep only their kind
 *   ARRAY_TEST
 *   NONE            "none", the result of fn:error, which never
 *                   returns
 */
typedef struct {
    xp3AstKind kind;
    int type;
    xp3Occurrence occurrence;
} xp3FuncType;

/* One signature of a built-in function, with a fixed arity */
typedef struct _xp3FuncInfo xp3FuncInfo;

struct _xp3FuncInfo {
    xp3FuncNamespace ns;
    const char *name;           /* local name, for example "substring" */
    int arity;                  /* the minimum if XP3_FUNC_VARIADIC */
    xp3LanguageLevel level;     /* the first level that has it */
    int flags;
    const char *signature;      /* as written in F&O 3.1 */
    xp3FuncType result;
    const xp3FuncType *params;  /* arity of them, NULL for none; further
                                 * arguments of a variadic function have
                                 * the type of the last */
};

const xp3FuncInfo *
xp3FuncLookup(const xmlChar *uri, const xmlChar *localName, int arity,
              xp3LanguageLevel level);

int
xp3FuncNameExists(const xmlChar *uri, const xmlChar *localName,
                  xp3LanguageLevel level);

const xmlChar *
xp3FuncNamespaceUri(xp3FuncNamespace ns);

size_t
xp3FuncCount(void);

const xp3FuncInfo *
xp3FuncAt(size_t index);

/*
 * Implementations
 *
 * A function call is evaluated eagerly (doc/DESIGN.md 7.1): the
 * evaluator evaluates the arguments, applies the function conversion
 * rules of XPath 3.1, 3.1.5.2 to each, and passes them to the
 * implementation, which so gets values of the declared types: for an
 * xs:double? parameter, the empty sequence or one xs:double. The
 * arguments are borrowed; the implementation returns a new value with
 * one reference, or NULL after recording an error.
 *
 * Everything else an implementation needs from the evaluation goes
 * through the call, which stays opaque: the focus, errors with their
 * position, the step counter and the limits on sequences. Results with
 * more than one item are built with xp3FuncBuilderInit() and its
 * companions, so that limits and allocation failures behave as for
 * every other expression.
 */
typedef struct _xp3FuncCall xp3FuncCall;

typedef xp3Value *(*xp3FuncImpl)(xp3FuncCall *call, xp3Value **args,
                                 int count);

xp3FuncImpl
xp3FuncImplementation(const xp3FuncInfo *info);

/* The call interface, in xp3eval.c */
int
xp3FuncFocus(xp3FuncCall *call, xp3Item *item, uint64_t *position,
             uint64_t *size);

void
xp3FuncError(xp3FuncCall *call, xp3ErrCode code, const char *format, ...);

void
xp3FuncUserError(xp3FuncCall *call, const xmlChar *uri,
                 const xmlChar *localName, const xmlChar *description);

int
xp3FuncSteps(xp3FuncCall *call, uint64_t steps);

xp3LanguageLevel
xp3FuncLevel(xp3FuncCall *call);

void
xp3FuncBuilderInit(xp3FuncCall *call, xp3SeqBuilder *builder);

int
xp3FuncBuilderAdd(xp3FuncCall *call, xp3SeqBuilder *builder, xp3Item item);

xp3Value *
xp3FuncBuilderFinish(xp3FuncCall *call, xp3SeqBuilder *builder);

xp3Value *
xp3FuncAtomic(xp3FuncCall *call, xp3Atomic *atomic);

xp3Value *
xp3FuncAtomize(xp3FuncCall *call, xp3Value *value);

/* Implementations in xp3fnseq.c */
xp3Value *
xp3FnTrue(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnFalse(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnNot(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnBoolean(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnString(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnData(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnNumber(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnError(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnPosition(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnLast(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnEmpty(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnExists(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnHead(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnTail(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnInsertBefore(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnRemove(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnReverse(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnSubsequence(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnZeroOrOne(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnOneOrMore(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnExactlyOne(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnCount(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnSum(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnAvg(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnMin(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnMax(xp3FuncCall *call, xp3Value **args, int count);

/* Implementations in xp3fnnumeric.c */
xp3Value *
xp3FnAbs(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnCeiling(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnFloor(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnRound(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3FnRoundHalfToEven(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3MathPi(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3MathExp(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3MathExp10(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3MathLog(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3MathLog10(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3MathPow(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3MathSqrt(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3MathSin(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3MathCos(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3MathTan(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3MathAsin(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3MathAcos(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3MathAtan(xp3FuncCall *call, xp3Value **args, int count);

xp3Value *
xp3MathAtan2(xp3FuncCall *call, xp3Value **args, int count);

#endif /* XP3_XP3FUNC_H */
