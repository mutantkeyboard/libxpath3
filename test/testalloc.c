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

static xmlFreeFunc oldFree;
static xmlMallocFunc oldMalloc;
static xmlReallocFunc oldRealloc;
static xmlStrdupFunc oldStrdup;

static int failAt = -1;
static int allocCount;
static int failed;

static int
shouldFail(void) {
    if (failAt < 0)
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

void
xp3TestAllocInstall(void) {
    xmlMemGet(&oldFree, &oldMalloc, &oldRealloc, &oldStrdup);
    xmlMemSetup(free, testMalloc, testRealloc, testStrdup);
    failAt = -1;
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
