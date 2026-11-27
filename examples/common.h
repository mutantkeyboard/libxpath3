/*
 * common.h: helpers shared by the tutorial programs
 *
 * Reading a document from the data directory, printing a sequence in
 * the format of the tutorials, and running one query from compiling to
 * printing. The first tutorial writes all of this out itself; the
 * others use these helpers so that each page can concentrate on its
 * subject.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#ifndef XP3_EXAMPLES_COMMON_H
#define XP3_EXAMPLES_COMMON_H

#include <libxml/tree.h>
#include <libxpath3/xpath3.h>

xmlDocPtr
exReadDocument(const char *dataDir, const char *fileName, int options);

void
exPrintText(const xmlChar *text);

void
exPrintSequence(const xp3Sequence *seq);

void
exPrintError(const xp3Error *error);

int
exEvaluate(const xp3Expr *expr, xp3DynamicContext *dctx, const char *label,
           const char *expectedError);

int
exRun(const xp3StaticContext *sctx, xp3DynamicContext *dctx,
      const char *label, const char *query, const char *expectedError);

#endif /* XP3_EXAMPLES_COMMON_H */
