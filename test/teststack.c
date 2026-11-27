/*
 * teststack.c: the deepest expressions fit in a small stack
 *
 * The parser's nesting limit exists so that deep nesting fails with
 * LIMIT instead of overflowing the stack. For each shape of nesting,
 * this test finds the deepest expression that the default limit
 * accepts, then parses, compiles (with folding) and evaluates it in a
 * thread whose stack is 1 MB less a margin (doc/DESIGN.md 7.3). 1 MB
 * is the stack of an MSVC program's main thread and a common size for
 * threads elsewhere. A limit that is too high for some compiler's
 * frames makes this test crash there.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <windows.h>
  #define XP3_STACK_THREADS
#elif defined(XP3_HAVE_PTHREAD)
  #include <pthread.h>
  #define XP3_STACK_THREADS
#endif

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <libxpath3/xpath3.h>

#include "testtap.h"

/*
 * Sanitizers and coverage counters make frames several times larger:
 * under ASan the deepest parentheses take about three times the stack
 * of an optimized build. Those builds check for memory errors, not
 * for stack size, so they get 8 MB, the usual main-thread stack on
 * Linux and macOS. XP3_COVERAGE comes from the build system, as
 * compilers don't define a macro for coverage.
 */
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
  #define XP3_STACK_INSTRUMENTED
#elif defined(__has_feature)
  #if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
    #define XP3_STACK_INSTRUMENTED
  #endif
#endif
#ifdef XP3_COVERAGE
  #define XP3_STACK_INSTRUMENTED
#endif

/*
 * 1 MB less a margin of 256 KB. The margin is for the frames of the
 * application below the library's, and for compilers whose frames
 * are larger than those measured: the limit is chosen from the frames
 * of clang and estimated for MSVC (doc/DESIGN.md 7.3).
 */
#ifdef XP3_STACK_INSTRUMENTED
  #define XP3_STACK_SIZE (8 * 1024 * 1024)
#else
  #define XP3_STACK_SIZE (1024 * 1024 - 256 * 1024)
#endif

/* No shape is accepted this deep, whatever the limit's cost per level */
#define XP3_MAX_LEVELS 100000

/*
 * A shape of nesting: head, then pre repeated, leaf, and post
 * repeated, as many times as there are levels. $x is a declared
 * variable, so an expression that uses it is evaluated when it runs;
 * one with only constants is evaluated by folding while it compiles.
 */
typedef struct {
    const char *name;
    const char *head;
    const char *pre;
    const char *leaf;
    const char *post;
    int unsupported;            /* evaluation may raise UNSUPPORTED */
} xp3StackShape;

static const xp3StackShape xp3StackShapes[] = {
    { "parentheses",          "", "(",            "$x", ")",        0 },
    { "predicates",           "", "a[",           "1",  "]",        0 },
    { "unary signs",          "", "-",            "$x", "",         0 },
    { "folded unary signs",   "", "-",            "1",  "",         0 },
    { "additions",            "", "$x + ",        "$x", "",         0 },
    { "folded additions",     "", "1 + ",         "1",  "",         0 },
    { "arrows",               "", "",             "$x", " => abs()", 0 },
    { "folded arrows",        "", "",             "1",  " => abs()", 0 },
    { "function calls",       "", "abs(",         "$x", ")",        0 },
    { "if",                   "", "if ($x) then ", "$x", " else 0", 0 },
    { "for",                  "", "for $v in $x return ", "$v", "", 0 },
    { "let",                  "", "let $v := $x return ", "$v", "", 0 },
    { "some",                 "", "some $v in $x satisfies ", "$v", "",
      0 },
    { "square arrays",        "", "[",            "$x", "]",        1 },
    { "curly arrays",         "", "array {",      "$x", "}",        1 },
    { "maps",                 "", "map {1: ",     "$x", "}",        1 },
    { "inline functions",     "", "function () {", "1", "}",        1 },
    { "function types",       "$x instance of ", "function() as ",
      "item()", "", 0 }
};

#define XP3_SHAPE_COUNT \
    (sizeof(xp3StackShapes) / sizeof(xp3StackShapes[0]))

static xp3StaticContext *staticCtx;
static xp3DynamicContext *dynamicCtx;
static xmlDocPtr stackDoc;

/* What the thread found for one shape. Checked in the main thread. */
typedef struct {
    const xp3StackShape *shape;
    int levels;                 /* the deepest accepted, or -1 */
    char deeper[32];            /* the error one level deeper */
    int compiled;
    char evaluated[32];         /* "" or the error code */
} xp3StackResult;

static void
check(int cond, const char *what) {
    xp3TestCheck(cond, what);
}

#ifdef XP3_STACK_THREADS

/* The expression of a shape with levels levels. Returns a malloc'ed
 * string, or NULL. */
static char *
buildExpression(const xp3StackShape *shape, int levels) {
    size_t headLen = strlen(shape->head);
    size_t preLen = strlen(shape->pre);
    size_t leafLen = strlen(shape->leaf);
    size_t postLen = strlen(shape->post);
    size_t size = headLen + leafLen + 1 +
                  (preLen + postLen) * (size_t) levels;
    char *ret = malloc(size);
    char *cur = ret;
    int i;

    if (ret == NULL)
        return(NULL);
    memcpy(cur, shape->head, headLen);
    cur += headLen;
    for (i = 0; i < levels; i++) {
        memcpy(cur, shape->pre, preLen);
        cur += preLen;
    }
    memcpy(cur, shape->leaf, leafLen);
    cur += leafLen;
    for (i = 0; i < levels; i++) {
        memcpy(cur, shape->post, postLen);
        cur += postLen;
    }
    *cur = 0;
    return(ret);
}

/*
 * Compile a shape with levels levels. Returns the compiled expression,
 * or NULL with the error code in code ("NO_MEMORY" if the string
 * couldn't be built).
 */
static xp3Expr *
compileShape(const xp3StackShape *shape, int levels, char *code,
             size_t size) {
    char *str = buildExpression(shape, levels);
    xp3Error *err = NULL;
    xp3Expr *expr;

    code[0] = 0;
    if (str == NULL) {
        snprintf(code, size, "NO_MEMORY");
        return(NULL);
    }
    expr = xp3Compile(staticCtx, BAD_CAST str, &err);
    if (err != NULL)
        snprintf(code, size, "%s", (const char *) xp3ErrorCode(err));
    xp3ErrorFree(err);
    free(str);
    return(expr);
}

/*
 * In the thread: find the deepest expression of the shape that the
 * limit accepts, by doubling and then halving the number of levels,
 * then evaluate it. An expression beyond the limit fails while
 * parsing, at the limit, so the search never goes deeper.
 */
static void
runShape(xp3StackResult *result) {
    const xp3StackShape *shape = result->shape;
    char code[32];
    xp3Expr *expr;
    xp3Error *err = NULL;
    xp3Sequence *seq;
    int good = 0;
    int bad = 1;

    result->levels = -1;
    for (;;) {
        expr = compileShape(shape, bad, code, sizeof(code));
        xp3FreeExpr(expr);
        if (expr == NULL)
            break;
        good = bad;
        if (bad >= XP3_MAX_LEVELS)
            return;
        bad *= 2;
    }
    if (strcmp(code, "LIMIT") != 0) {
        snprintf(result->deeper, sizeof(result->deeper), "%s", code);
        return;
    }
    while (bad - good > 1) {
        int middle = good + (bad - good) / 2;

        expr = compileShape(shape, middle, code, sizeof(code));
        xp3FreeExpr(expr);
        if (expr != NULL) {
            good = middle;
        } else if (strcmp(code, "LIMIT") == 0) {
            bad = middle;
        } else {
            snprintf(result->deeper, sizeof(result->deeper), "%s", code);
            return;
        }
    }
    result->levels = good;
    snprintf(result->deeper, sizeof(result->deeper), "LIMIT");

    expr = compileShape(shape, good, code, sizeof(code));
    if (expr == NULL)
        return;
    result->compiled = 1;
    seq = xp3Evaluate(expr, dynamicCtx, &err);
    if (err != NULL)
        snprintf(result->evaluated, sizeof(result->evaluated), "%s",
                 (const char *) xp3ErrorCode(err));
    else if (seq == NULL)
        snprintf(result->evaluated, sizeof(result->evaluated), "no result");
    xp3FreeSequence(seq);
    xp3ErrorFree(err);
    xp3FreeExpr(expr);
}

/*
 * Run a shape in a thread with a stack of XP3_STACK_SIZE. Returns 0, or
 * -1 if the thread couldn't be started.
 */
#ifdef _WIN32

static DWORD WINAPI
shapeThread(LPVOID arg) {
    runShape(arg);
    return(0);
}

static int
runInThread(xp3StackResult *result) {
    HANDLE thread;

    /* Without the flag, the size is only what is committed at first,
     * and the stack grows up to the executable's default */
    thread = CreateThread(NULL, XP3_STACK_SIZE, shapeThread, result,
                          STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (thread == NULL)
        return(-1);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return(0);
}

#elif defined(XP3_HAVE_PTHREAD)

static void *
shapeThread(void *arg) {
    runShape(arg);
    return(NULL);
}

static int
runInThread(xp3StackResult *result) {
    pthread_attr_t attr;
    pthread_t thread;
    int ret = -1;

    if (pthread_attr_init(&attr) != 0)
        return(-1);
    if ((pthread_attr_setstacksize(&attr, XP3_STACK_SIZE) == 0) &&
        (pthread_create(&thread, &attr, shapeThread, result) == 0)) {
        pthread_join(thread, NULL);
        ret = 0;
    }
    pthread_attr_destroy(&attr);
    return(ret);
}

#endif /* _WIN32 */

#endif /* XP3_STACK_THREADS */

static void
testSetUp(void) {
    static const char doc[] = "<a><a/></a>";
    const int64_t one = 1;
    xp3Sequence *value;

    stackDoc = xmlReadMemory(doc, (int) strlen(doc), "stack.xml", NULL, 0);
    staticCtx = xp3NewStaticContext();
    dynamicCtx = xp3NewDynamicContext();
    value = xp3NewIntegerSequence(&one, 1);
    check((stackDoc != NULL) && (staticCtx != NULL) &&
          (dynamicCtx != NULL) && (value != NULL), "contexts created");
    if (xp3TestFailures() > 0) {
        xp3FreeSequence(value);
        return;
    }
    check(xp3DeclareVariable(staticCtx, NULL, BAD_CAST "x") == 0,
          "$x declared");
    check(xp3SetContextNode(dynamicCtx,
                            xmlDocGetRootElement(stackDoc)) == 0,
          "context node set");
    check(xp3BindVariable(dynamicCtx, NULL, BAD_CAST "x", value) == 0,
          "$x bound");
    xp3FreeSequence(value);
}

static void
testDeepest(void) {
#ifdef XP3_STACK_THREADS
    size_t i;

    for (i = 0; i < XP3_SHAPE_COUNT; i++) {
        const xp3StackShape *shape = &xp3StackShapes[i];
        xp3StackResult result;
        int evaluated;

        memset(&result, 0, sizeof(result));
        result.shape = shape;
        if (runInThread(&result) < 0) {
            xp3TestFail("%s: no thread", shape->name);
            continue;
        }
        if (result.levels < 1) {
            xp3TestFail("%s: no depth found below the limit, then %s",
                        shape->name, result.deeper);
            continue;
        }
        if (!result.compiled) {
            xp3TestFail("%s: %d levels don't compile", shape->name,
                        result.levels);
            continue;
        }
        evaluated = (result.evaluated[0] == 0) ||
                    (shape->unsupported &&
                     (strcmp(result.evaluated, "UNSUPPORTED") == 0));
        if (!evaluated)
            xp3TestFail("%s: %d levels raise %s", shape->name,
                        result.levels, result.evaluated);
    }
#else
    xp3TestSkip("threads are not available");
#endif
}

int
main(void) {
    xmlInitParser();

    XP3_TEST_RUN(testSetUp);
    if (xp3TestFailures() == 0)
        XP3_TEST_RUN(testDeepest);

    xp3FreeDynamicContext(dynamicCtx);
    xp3FreeStaticContext(staticCtx);
    xmlFreeDoc(stackDoc);
    return(xp3TestDone());
}
