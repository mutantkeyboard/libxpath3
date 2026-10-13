/*
 * xp3parse.c: a libFuzzer target to test parsing XPath expressions
 *
 * The input starts with a three-byte header: the first byte selects
 * the language level (or no static context), and the next two are a
 * big-endian allocation-failure position, 0 for none. The rest is the
 * expression, which may contain anything, including NUL bytes.
 *
 * The target checks the library's contracts and aborts when one is
 * broken: parsing either returns a tree or reports an error, never
 * neither, and an injected allocation failure results in NO_MEMORY,
 * never a tree and never silence. Everything else (overflows, leaks,
 * undefined behaviour) is left to the sanitizers.
 *
 * Built with -DXP3_FUZZ_DRIVER, the same file gets a main() that
 * replays files given as arguments, so the seed corpus and earlier
 * findings run as regression tests in the normal test suite, with
 * any compiler.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/xmlerror.h>
#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

#include <libxpath3/xpath3.h>
#include "private/xp3ast.h"
#include "private/xp3error.h"
#include "private/xp3parser.h"
#include "../test/testalloc.h"

/* Larger inputs only slow the fuzzer down, as in libxml2's targets. */
#define XP3_FUZZ_MAX_SIZE 10000

static xp3StaticContext *fuzzCtx[3];    /* XPath 2.0, 3.0, 3.1 */
static int fuzzInjects;                 /* allocation failures work */

static void
fuzzErrorSink(void *userData, const xmlError *error) {
    (void) userData;
    (void) error;
}

int
LLVMFuzzerInitialize(int *argc, char ***argv) {
    static const xp3LanguageLevel levels[3] = {
        XP3_XPATH_20, XP3_XPATH_30, XP3_XPATH_31
    };
    size_t i;

    (void) argc;
    (void) argv;

    /* Apple's system libxml2 ignores xmlMemSetup(). Injection then
     * does nothing, and the replay driver only checks the other
     * contracts. */
    fuzzInjects = xp3TestAllocInstall();
    xmlInitParser();

    for (i = 0; i < 3; i++) {
        fuzzCtx[i] = xp3NewStaticContext();
        if ((fuzzCtx[i] == NULL) ||
            (xp3SetStaticContextLanguageLevel(fuzzCtx[i], levels[i]) < 0)) {
            fprintf(stderr, "xp3parse: cannot create the contexts\n");
            abort();
        }
    }
    return(0);
}

int
LLVMFuzzerTestOneInput(const char *data, size_t size) {
    const unsigned char *bytes = (const unsigned char *) data;
    const xp3StaticContext *ctx;
    xp3Error *err = NULL;
    xp3Ast *ast;
    size_t failurePos;
    int failed;

    if ((size < 3) || (size > XP3_FUZZ_MAX_SIZE))
        return(0);

    ctx = (bytes[0] % 4 == 0) ? NULL : fuzzCtx[bytes[0] % 4 - 1];
    failurePos = ((size_t) bytes[1] << 8) | bytes[2];

    xp3TestAllocFailAt((failurePos > 0) ? (int) failurePos - 1 : -1);
    ast = xp3Parse(ctx, BAD_CAST data + 3, size - 3, &err);
    failed = xp3TestAllocFailed();
    xp3TestAllocFailAt(-1);

    /* Parsing returns a tree or reports an error, never neither or
     * both. */
    if ((ast == NULL) == (err == NULL)) {
        fprintf(stderr, "xp3parse: %s\n", (ast == NULL) ?
                "parse failed without an error" :
                "parse succeeded with an error");
        abort();
    }

    if (failed) {
        /* The failed allocation must surface as NO_MEMORY. */
        if (ast != NULL) {
            fprintf(stderr, "xp3parse: tree built after an allocation "
                    "failure\n");
            abort();
        }
        if (err->code != XP3_ERR_NO_MEMORY) {
            fprintf(stderr, "xp3parse: allocation failure reported as "
                    "%s\n", (const char *) xp3ErrorCode(err));
            abort();
        }
    }

    if (ast != NULL) {
        xmlChar *str;

        if (ast->root == NULL) {
            fprintf(stderr, "xp3parse: tree without a root\n");
            abort();
        }
        /* Walk the whole tree through the printer. */
        str = xp3AstToString(ast->root);
        xmlFree(str);
    } else {
        /* The accessors and the libxml2 adapter must cope with any
         * error object. */
        (void) xp3ErrorCode(err);
        (void) xp3ErrorNamespace(err);
        (void) xp3ErrorMessage(err);
        (void) xp3ErrorLine(err);
        (void) xp3ErrorColumn(err);
        xp3ErrorReport(err, (xmlStructuredErrorFunc) fuzzErrorSink, NULL);
    }

    xp3AstFree(ast);
    xp3ErrorFree(err);
    return(0);
}

#ifdef XP3_FUZZ_DRIVER

/*
 * Replay driver: runs each file through the fuzz target once, so the
 * corpus works as regression tests without libFuzzer.
 */
int
main(int argc, char **argv) {
    int i;

    LLVMFuzzerInitialize(NULL, NULL);
    if (!fuzzInjects)
        printf("xp3parse: this libxml2 ignores xmlMemSetup(), replaying "
               "without allocation failures\n");

    for (i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        char *data;
        size_t size;

        if (f == NULL) {
            fprintf(stderr, "xp3parse: cannot open %s\n", argv[i]);
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
            fprintf(stderr, "xp3parse: %s is too large, skipped\n",
                    argv[i]);
        free(data);
    }

    printf("xp3parse: %d inputs replayed\n", argc - 1);
    return(0);
}

#endif /* XP3_FUZZ_DRIVER */
