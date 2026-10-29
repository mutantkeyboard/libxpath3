/*
 * xp3eval.c: a libFuzzer target to test evaluation, and that
 * optimizing never changes a result
 *
 * The input starts with a three-byte header. The first byte selects
 * the language level (byte % 3) and the context node (byte / 3 % 5):
 * the document, its root element, a nested element, an attribute or
 * a text node of a fixed document with every kind of node. The next
 * two are a big-endian allocation-failure position, 0 for none. The
 * rest is the expression.
 *
 * Without an allocation failure, the expression is compiled twice,
 * optimized as by default and without optimizing (no constant folding
 * or path rewrites), and both are evaluated. The target aborts if the
 * two disagree: one compiles and the other doesn't, they raise
 * different static errors, they return different items (nodes are
 * numbered, so order and identity count), or only the optimized one
 * raises an error. The unoptimized one may raise an error the
 * optimized one doesn't, because pruning skips operands that XPath
 * allows to go unevaluated (XPath 3.1, 2.3.4 and 3.8), and a step or
 * sequence limit may stop either one. For the same reason, when both
 * fail, their codes may differ: in "comment() = 1 and 0, if (0 to 2)
 * then 1 else 2", the unoptimized one fails on the comparison first,
 * the optimized one skips it and fails on the if.
 *
 * With an allocation failure, only the optimized expression is
 * compiled and evaluated, and the failure must end in NO_MEMORY,
 * never in a result and never in silence.
 *
 * Built with -DXP3_FUZZ_DRIVER, the file gets a main() that replays
 * files given as arguments, as xp3parse.c does.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

#include <libxpath3/xpath3.h>
#include "private/xp3context.h"
#include "private/xp3error.h"
#include "private/xp3eval.h"
#include "private/xp3nodes.h"
#include "private/xp3static.h"
#include "private/xp3types.h"
#include "private/xp3value.h"
#include "../test/testalloc.h"

#define XP3_FUZZ_MAX_SIZE 10000

/* Limits that keep each evaluation short */
#define XP3_FUZZ_OPERATIONS 200000
#define XP3_FUZZ_ITEMS 10000

/* Room for a result description; longer ones are cut, the same way */
#define XP3_FUZZ_DESCRIPTION 8192

static const char xp3FuzzDoc[] =
    "<?xml version='1.0'?>"
    "<!--top--><r xmlns='urn:default' xmlns:ex='urn:example' id='r1'>"
    "text<![CDATA[cdata]]><a id='i1' k='v'>one<a id='i2'><b/>two</a>"
    "<b ex:at='x'/></a><!--c--><?pi data?><b id='i3'>"
    "<a id='i4'>3</a><ex:c>4.5</ex:c></b>tail</r>";

static xp3StaticContext *fuzzCtx[3];        /* optimizing, per level */
static xp3StaticContext *fuzzPlainCtx[3];   /* not optimizing */
static xmlDocPtr fuzzDoc;
static xmlNodePtr fuzzContexts[5];
static xp3Sequence *fuzzVariable;           /* bound to $v */
static xp3Sequence *fuzzString;             /* bound to $ex:w */
static int fuzzInjects;

static xp3StaticContext *
newContext(xp3LanguageLevel level, int optimize) {
    xp3StaticContext *ctx = xp3NewStaticContext();

    if ((ctx == NULL) ||
        (xp3SetStaticContextLanguageLevel(ctx, level) < 0) ||
        (xp3DeclareNamespace(ctx, BAD_CAST "ex",
                             BAD_CAST "urn:example") < 0) ||
        (xp3SetDefaultElementNamespace(ctx, BAD_CAST "urn:default") < 0) ||
        (xp3DeclareVariable(ctx, NULL, BAD_CAST "v") < 0) ||
        (xp3DeclareVariable(ctx, BAD_CAST "urn:example",
                            BAD_CAST "w") < 0)) {
        fprintf(stderr, "xp3eval: cannot create the contexts\n");
        exit(1);
    }
    ctx->noOptimize = !optimize;
    return(ctx);
}

/* Number the nodes in document order, so results show identity */
static void
numberNodes(void) {
    xmlNodePtr root = (xmlNodePtr) fuzzDoc;
    xmlNodePtr node = NULL;
    intptr_t number = 0;

    while ((node = xp3AxisNext(XP3_AXIS_DESCENDANT_OR_SELF, root, node)) !=
           NULL) {
        xmlNodePtr attr = NULL;

        node->_private = (void *) ++number;
        while ((attr = xp3AxisNext(XP3_AXIS_ATTRIBUTE, node, attr)) != NULL)
            attr->_private = (void *) ++number;
    }
}

int
LLVMFuzzerInitialize(int *argc, char ***argv) {
    static const xp3LanguageLevel levels[3] = {
        XP3_XPATH_20, XP3_XPATH_30, XP3_XPATH_31
    };
    const xmlChar *text = BAD_CAST "1.5";
    xmlNodePtr root;
    xmlNodePtr nodes[2];
    size_t i;

    (void) argc;
    (void) argv;

    fuzzInjects = xp3TestAllocInstall();
    xmlInitParser();
    for (i = 0; i < 3; i++) {
        fuzzCtx[i] = newContext(levels[i], 1);
        fuzzPlainCtx[i] = newContext(levels[i], 0);
    }
    fuzzDoc = xmlReadMemory(xp3FuzzDoc, (int) strlen(xp3FuzzDoc),
                           "fuzz.xml", NULL, XML_PARSE_NOENT);
    if (fuzzDoc == NULL) {
        fprintf(stderr, "xp3eval: cannot parse the document\n");
        exit(1);
    }
    numberNodes();

    root = xmlDocGetRootElement(fuzzDoc);
    fuzzContexts[0] = (xmlNodePtr) fuzzDoc;
    fuzzContexts[1] = root;
    fuzzContexts[2] = xmlFirstElementChild(xmlFirstElementChild(root));
    fuzzContexts[3] = (xmlNodePtr) root->properties;
    fuzzContexts[4] = root->children;
    nodes[0] = fuzzContexts[2];
    nodes[1] = root;
    fuzzVariable = xp3NewNodeSequence(nodes, 2);
    fuzzString = xp3NewStringSequence(&text, 1);
    if ((fuzzContexts[2] == NULL) || (fuzzContexts[3] == NULL) ||
        (fuzzVariable == NULL) || (fuzzString == NULL)) {
        fprintf(stderr, "xp3eval: cannot set up the document\n");
        exit(1);
    }
    return(0);
}

/* A dynamic context with the variables, a context node and limits.
 * Returns NULL if memory allocation failed. */
static xp3DynamicContext *
newDynamicContext(xmlNodePtr contextNode) {
    xp3DynamicContext *ctx = xp3NewDynamicContext();

    if (ctx == NULL)
        return(NULL);
    ctx->maxOperations = XP3_FUZZ_OPERATIONS;
    if ((xp3SetMaxSequenceLength(ctx, XP3_FUZZ_ITEMS) < 0) ||
        (xp3SetContextNode(ctx, contextNode) < 0) ||
        (xp3BindVariable(ctx, NULL, BAD_CAST "v", fuzzVariable) < 0) ||
        (xp3BindVariable(ctx, BAD_CAST "urn:example", BAD_CAST "w",
                         fuzzString) < 0)) {
        xp3FreeDynamicContext(ctx);
        return(NULL);
    }
    return(ctx);
}

/* A result as node numbers and typed atomic values */
static void
describe(const xp3Sequence *seq, char *out, size_t size) {
    size_t used = 0;
    size_t i;

    out[0] = 0;
    for (i = 0; (i < xp3SequenceLength(seq)) && (used + 1 < size); i++) {
        xmlNodePtr node = xp3SequenceNode(seq, i);
        int written;

        if (node != NULL) {
            written = snprintf(out + used, size - used, " #%ld",
                               (long) (intptr_t) node->_private);
        } else {
            xmlChar *str = xp3SequenceStringValue(seq, i);
            xp3Item item;
            const char *type = "?";

            if (xp3ValueItemAt(seq, i, &item) > 0) {
                if (item.kind == XP3_ITEM_ATOMIC)
                    type = (const char *) xp3TypeLocalName(
                        item.u.atomic->type);
                xp3ItemUnref(item);
            }
            written = snprintf(out + used, size - used, " %s:%s", type,
                               (str != NULL) ? (const char *) str : "?");
            xmlFree(str);
        }
        if (written < 0)
            break;
        used += (size_t) written;
    }
}

/* Whether an error code may come from one evaluation and not the other:
 * a limit, or something not implemented that pruning skipped */
static int
isSoftCode(const xmlChar *code) {
    return(xmlStrEqual(code, BAD_CAST "LIMIT") ||
           xmlStrEqual(code, BAD_CAST "UNSUPPORTED"));
}

/* Compile and evaluate both ways, and compare */
static void
compareBothWays(int level, xmlNodePtr contextNode, const char *data,
                size_t size) {
    xp3Error *optimizedErr = NULL;
    xp3Error *plainErr = NULL;
    xp3Expr *optimized;
    xp3Expr *plain;
    xp3DynamicContext *ctx;
    xp3Sequence *optimizedResult = NULL;
    xp3Sequence *plainResult = NULL;
    static char optimizedText[XP3_FUZZ_DESCRIPTION];
    static char plainText[XP3_FUZZ_DESCRIPTION];

    optimized = xp3CompileLength(fuzzCtx[level], BAD_CAST data, size,
                                 &optimizedErr);
    plain = xp3CompileLength(fuzzPlainCtx[level], BAD_CAST data, size,
                             &plainErr);
    if ((optimized == NULL) != (plain == NULL)) {
        fprintf(stderr, "xp3eval: only one of the two compiled\n");
        abort();
    }
    if (optimized == NULL) {
        if (!xmlStrEqual(xp3ErrorCode(optimizedErr),
                         xp3ErrorCode(plainErr))) {
            fprintf(stderr, "xp3eval: static errors differ: %s, %s\n",
                    (const char *) xp3ErrorCode(optimizedErr),
                    (const char *) xp3ErrorCode(plainErr));
            abort();
        }
        goto done;
    }
    xp3ErrorFree(optimizedErr);
    xp3ErrorFree(plainErr);
    optimizedErr = NULL;
    plainErr = NULL;

    ctx = newDynamicContext(contextNode);
    if (ctx == NULL) {
        fprintf(stderr, "xp3eval: cannot create a dynamic context\n");
        abort();
    }
    optimizedResult = xp3Evaluate(optimized, ctx, &optimizedErr);
    plainResult = xp3Evaluate(plain, ctx, &plainErr);
    xp3FreeDynamicContext(ctx);

    if ((optimizedResult != NULL) && (plainResult != NULL)) {
        describe(optimizedResult, optimizedText, sizeof(optimizedText));
        describe(plainResult, plainText, sizeof(plainText));
        if (strcmp(optimizedText, plainText) != 0) {
            fprintf(stderr, "xp3eval: results differ\n  optimized:  %s\n"
                    "  unoptimized: %s\n", optimizedText, plainText);
            abort();
        }
    } else if ((optimizedResult == NULL) && (plainResult == NULL)) {
        /* Both fail, maybe with different codes: see the top */
    } else if ((optimizedResult == NULL) &&
               !isSoftCode(xp3ErrorCode(optimizedErr))) {
        /* Optimizing may skip an error, never add one */
        fprintf(stderr, "xp3eval: only the optimized expression fails, "
                "with %s\n", (const char *) xp3ErrorCode(optimizedErr));
        abort();
    }

done:
    xp3FreeSequence(optimizedResult);
    xp3FreeSequence(plainResult);
    xp3ErrorFree(optimizedErr);
    xp3ErrorFree(plainErr);
    xp3FreeExpr(optimized);
    xp3FreeExpr(plain);
}

/* Compile and evaluate with an allocation failing: NO_MEMORY or no
 * failure at all, never a hidden one */
static void
checkAllocationFailure(int level, xmlNodePtr contextNode, const char *data,
                       size_t size, size_t failurePos) {
    xp3Error *err = NULL;
    xp3DynamicContext *ctx = NULL;
    xp3Sequence *result = NULL;
    xp3Expr *expr;
    int setupFailed = 0;
    int failed;

    xp3TestAllocFailAt((int) failurePos - 1);
    expr = xp3CompileLength(fuzzCtx[level], BAD_CAST data, size, &err);
    if (expr != NULL) {
        ctx = newDynamicContext(contextNode);
        if (ctx == NULL)
            setupFailed = 1;
        else
            result = xp3Evaluate(expr, ctx, &err);
    }
    failed = xp3TestAllocFailed();
    xp3TestAllocFailAt(-1);

    if (failed && !setupFailed) {
        if (result != NULL) {
            fprintf(stderr, "xp3eval: a result after an allocation "
                    "failure\n");
            abort();
        }
        if ((err == NULL) || (err->code != XP3_ERR_NO_MEMORY)) {
            fprintf(stderr, "xp3eval: allocation failure reported as %s\n",
                    (err != NULL) ? (const char *) xp3ErrorCode(err) :
                    "nothing");
            abort();
        }
    }
    xp3FreeSequence(result);
    xp3FreeDynamicContext(ctx);
    xp3FreeExpr(expr);
    xp3ErrorFree(err);
}

int
LLVMFuzzerTestOneInput(const char *data, size_t size) {
    const unsigned char *bytes = (const unsigned char *) data;
    size_t failurePos;
    int level;
    xmlNodePtr contextNode;

    if ((size < 3) || (size > XP3_FUZZ_MAX_SIZE))
        return(0);
    level = bytes[0] % 3;
    contextNode = fuzzContexts[bytes[0] / 3 % 5];
    failurePos = ((size_t) bytes[1] << 8) | bytes[2];

    if ((failurePos > 0) && fuzzInjects)
        checkAllocationFailure(level, contextNode, data + 3, size - 3,
                               failurePos);
    else
        compareBothWays(level, contextNode, data + 3, size - 3);
    return(0);
}

#ifdef XP3_FUZZ_DRIVER

/* Replay driver: runs each file through the fuzz target once */
int
main(int argc, char **argv) {
    int i;

    LLVMFuzzerInitialize(NULL, NULL);
    if (!fuzzInjects)
        printf("xp3eval: this libxml2 ignores xmlMemSetup(), replaying "
               "without allocation failures\n");

    for (i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        char *data;
        size_t size;

        if (f == NULL) {
            fprintf(stderr, "xp3eval: cannot open %s\n", argv[i]);
            return(1);
        }
        data = malloc(XP3_FUZZ_MAX_SIZE + 1);
        if (data == NULL) {
            fclose(f);
            return(1);
        }
        size = fread(data, 1, XP3_FUZZ_MAX_SIZE + 1, f);
        fclose(f);
        if (size <= XP3_FUZZ_MAX_SIZE)
            LLVMFuzzerTestOneInput(data, size);
        else
            fprintf(stderr, "xp3eval: %s is too large, skipped\n", argv[i]);
        free(data);
    }

    printf("xp3eval: %d inputs replayed\n", argc - 1);
    return(0);
}

#endif /* XP3_FUZZ_DRIVER */
