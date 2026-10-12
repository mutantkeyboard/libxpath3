/*
 * testalloc.h: allocator for testing allocation failures
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#ifndef XP3_TESTALLOC_H
#define XP3_TESTALLOC_H

/*
 * Typical use: run an operation with the n-th allocation failing, for
 * n = 0, 1, 2, ... until xp3TestAllocFailed() reports that no failure
 * was injected. Every run must fail cleanly or succeed completely.
 */

/* Install the failing allocator in libxml2. Nothing fails until
 * xp3TestAllocFailAt() is called. */
void
xp3TestAllocInstall(void);

/* Restore the allocator that was active before xp3TestAllocInstall(). */
void
xp3TestAllocRestore(void);

/* Make the n-th allocation from now on fail (0 = the next one).
 * A negative n turns failures off. */
void
xp3TestAllocFailAt(int n);

/* Whether a failure was injected since the last xp3TestAllocFailAt(). */
int
xp3TestAllocFailed(void);

#endif /* XP3_TESTALLOC_H */
