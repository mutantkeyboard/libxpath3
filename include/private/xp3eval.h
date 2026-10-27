/**
 * @file
 *
 * @brief Dynamic context and evaluation (internal)
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XP3EVAL_H
#define XP3_XP3EVAL_H

#include <stddef.h>
#include <stdint.h>

#include <libxml/tree.h>
#include <libxml/xmlstring.h>

#include <libxpath3/xpath3.h>
#include "private/xp3ast.h"
#include "private/xp3error.h"
#include "private/xp3value.h"

/* A variable bound in a dynamic context */
typedef struct {
    xmlChar *uri;               /* NULL for no namespace */
    xmlChar *localName;
    xp3Value *value;            /* one reference */
} xp3BoundVariable;

/*
 * The dynamic context (XPath 3.1, 2.1.2) that evaluations start from.
 * Evaluating only reads it, but the values it holds are reference
 * counted, so one context is used by one thread at a time.
 */
struct _xp3DynamicContext {
    xmlNodePtr contextNode;     /* canonical, or NULL for none */
    xp3BoundVariable *variables;
    int variableCount;
    int variableMax;
    size_t maxSequenceLength;
    uint64_t maxOperations;     /* internal, for the fuzzers: steps an
                                 * evaluation may take, 0 for no limit */
};

xp3ErrCode
xp3EffectiveBoolean(const xp3Value *value, int *result);

xp3ErrCode
xp3EvalConstant(const xp3AstNode *node, xp3Value **slots, int slotCount,
                xp3LanguageLevel level, uint64_t maxOperations,
                size_t maxItems, size_t maxBytes, xp3Value **value,
                uint64_t *operations);

#endif /* XP3_XP3EVAL_H */
