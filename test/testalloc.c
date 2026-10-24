/*
 * testalloc.c: allocator for testing allocation failures
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdlib.h>
#include <string.h>

#include <libxml/xmlmemory.h>

#include "testalloc.h"

/*
 * Apple's libxml2 (macOS 15.4 and later) marks the allocator functions
 * as deprecated, and ignores xmlMemSetup() anyway, which
 * xp3TestAllocInstall() detects. Upstream libxml2 does not deprecate
 * them, so the warning is silenced for this test-only file.
 */
#if defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

static xmlFreeFunc oldFree;
static xmlMallocFunc oldMalloc;
static xmlReallocFunc oldRealloc;
static xmlStrdupFunc oldStrdup;

static int failAt = -1;
static int allocCount;
static int failed;
static int used;    /* set by any call: libxml2 uses this allocator */

/*
 * Whether this allocation fails. The counts stop once the failure is
 * past, so that a fuzzer making billions of allocations never
 * overflows them.
 */
static int
shouldFail(void) {
    used = 1;
    if ((failAt < 0) || (allocCount > failAt))
        return(0);
    if (allocCount++ == failAt) {
        failed = 1;
        return(1);
    }
    return(0);
}

static void *
testMalloc(size_t size) {
    if (shouldFail())
        return(NULL);
    return(malloc(size));
}

static void *
testRealloc(void *ptr, size_t size) {
    if (shouldFail())
        return(NULL);
    return(realloc(ptr, size));
}

static char *
testStrdup(const char *str) {
    size_t len = strlen(str) + 1;
    char *copy;

    copy = testMalloc(len);
    if (copy != NULL)
        memcpy(copy, str, len);
    return(copy);
}

int
xp3TestAllocInstall(void) {
    void *probe;

    xmlMemGet(&oldFree, &oldMalloc, &oldRealloc, &oldStrdup);
    xmlMemSetup(free, testMalloc, testRealloc, testStrdup);
    failAt = -1;

    /* Some builds of libxml2 ignore xmlMemSetup(). Check that an
     * allocation through xmlMalloc() actually reaches testMalloc(). */
    used = 0;
    probe = xmlMalloc(1);
    xmlFree(probe);
    return(used);
}

void
xp3TestAllocRestore(void) {
    failAt = -1;
    xmlMemSetup(oldFree, oldMalloc, oldRealloc, oldStrdup);
}

void
xp3TestAllocFailAt(int n) {
    failAt = n;
    allocCount = 0;
    failed = 0;
}

int
xp3TestAllocFailed(void) {
    return(failed);
}

#if defined(__GNUC__)
  #pragma GCC diagnostic pop
#endif
