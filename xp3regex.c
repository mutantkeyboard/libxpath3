/*
 * xp3regex.c: regular expressions of F&O 3.1
 *
 * The dialect is that of XML Schema 1.0 Second Edition, Appendix F,
 * with the additions of F&O 3.1, 5.6.1: the anchors ^ and $, reluctant
 * quantifiers, non-capturing groups, back-references, and an unknown
 * block name as an error (FORX0002). Where XSD 1.0 is unclear, F&O
 * points to XSD 1.1, Appendix G, as for the braces that F&O adds to
 * the metacharacters. For hyphens in character classes, where XSD 1.1
 * allows more, the rule of XSD 1.0 holds, the one QT3 tests for XSD
 * 1.0: an unescaped hyphen is a character only as the first or the
 * last of a group. The flags s, m, i, x and q are those of F&O 3.1,
 * 5.6.2.
 *
 * A pattern is parsed by recursive descent into a tree, which is
 * compiled into a program for a small virtual machine: characters,
 * classes, anchors, back-references, SPLIT with a preferred branch,
 * JMP, SAVE for the positions of groups, and MATCH. Counted repetition
 * is expanded, so the parser limits the size of the program as well
 * as the nesting of groups and class subtractions; past either limit
 * it raises LIMIT.
 *
 * Character classes stay lists of items: ranges, blocks, categories
 * and the multi-character escapes, each with its complement. A class
 * is evaluated for each character: the character is in it if an item
 * takes it, the class isn't negated, and the character isn't in the
 * class subtracted from it. That costs at most a binary search of the
 * Unicode tables per item, and the compiled program stays small,
 * however large the sets are. Under the i flag a character also
 * matches a single character or a range of a class if one of its case
 * variants does; the other items ignore the flag, as F&O 3.1, 5.6.2
 * says.
 *
 * Two matchers run the program.
 *
 * The Pike VM runs every pattern without back-references. It reads the
 * subject once, keeping the live threads of the program in priority
 * order, each with its own capture positions. A thread that reaches an
 * instruction another thread already holds at the same position is
 * dropped, so a search takes at most the size of the program times the
 * length of the subject, whatever the pattern. Matches are
 * leftmost-first, as F&O 3.1, 5.6.4 asks for: at each position a new
 * thread starts with the lowest priority until a match is found, and a
 * match ends the threads of lower priority.
 *
 * The backtracking matcher runs the patterns with back-references,
 * which no automaton can match. It tries the branches in the same
 * order, so for a pattern without back-references it finds the same
 * match with the same groups as the Pike VM; the tests and the fuzzer
 * compare the two. To end loops whose body matches the empty string,
 * a path that comes back to a SPLIT inside such a loop at the position
 * where it passed it before fails, as the Pike VM drops such a thread.
 * Its time can grow exponentially, and only the steps of the
 * evaluation bound it.
 *
 * Both matchers are adapted from re1 by Russ Cox,
 * https://github.com/rsc/re1, commit 6ca5ae5: pike.c, backtrack.c,
 * and the capture arrays of sub.c. Taken from re1: the two thread
 * lists of the Pike VM; JMP, SPLIT and SAVE followed while a thread is
 * added, which keeps the threads in the order a backtracker would try
 * them; capture arrays shared by reference count and copied before a
 * write; and the shape of the backtracking matcher, a stack of saved
 * threads run in stack order. Changed: memory comes from xmlMalloc()
 * and running out of it is an error, not an abort; adding a thread
 * uses an explicit stack, not recursion; the instructions visited at
 * a position are a sparse set in the state of the search, not a
 * counter stored in the program, so that threads can share a program;
 * the subject is UTF-8 with a length, not a NUL-terminated string;
 * searches are unanchored; there are anchors, classes, back-references
 * and case-insensitive matching; work is counted in steps; and the
 * stack of the backtracking matcher grows as needed and holds the old
 * values of captures to restore, not a capture array for each saved
 * thread. re1 comes with this notice:
 *
 * Copyright (c) 2007-2009 Russ Cox, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *    * Neither the name of Google, Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/xmlstring.h>

#include "private/xp3chars.h"
#include "private/xp3error.h"
#include "private/xp3regex.h"
#include "private/xp3unicode.h"

/* Nesting of groups and class subtractions. The parser recurses a few
 * functions deep per level, and compiling the tree a few more. */
#define XP3_REGEX_MAX_DEPTH 256

/* Instructions of a program, with counted repetition expanded */
#define XP3_REGEX_MAX_PROGRAM 100000

/* Instructions times capture slots. The Pike VM holds up to about
 * four capture arrays per instruction, so this bounds the memory of a
 * search to about 32 MB. */
#define XP3_REGEX_MAX_CAPTURE_CELLS (1 << 20)

/* Nodes of the tree, and items and classes: repeating an empty group
 * zero times makes nodes without instructions */
#define XP3_REGEX_MAX_PARTS (4 * XP3_REGEX_MAX_PROGRAM)

/* The longest name of \p{..} that can be known */
#define XP3_REGEX_MAX_PROPERTY 80

/* A character and its case variants */
#define XP3_REGEX_CHARS_MAX (1 + XP3_UNICODE_VARIANTS_MAX)

/* A capture slot that holds no position */
#define XP3_REGEX_UNSET ((ptrdiff_t) -1)

/* What parseEscape() found */
#define XP3_REGEX_ESCAPE_CHAR 1     /* a single character */
#define XP3_REGEX_ESCAPE_ITEM 2     /* a class escape */

/*
 * The program
 */

typedef enum {
    XP3_RE_CHAR = 0,        /* one of chars[0] to chars[count - 1] */
    XP3_RE_ANY,             /* any character, . with the s flag */
    XP3_RE_ANY_NOT_NL,      /* any character but #xA and #xD */
    XP3_RE_CLASS,           /* a character of class arg */
    XP3_RE_LINE_START,      /* ^ */
    XP3_RE_LINE_END,        /* $ */
    XP3_RE_BACKREF,         /* the text group arg matched */
    XP3_RE_SPLIT,           /* go on at arg, then at alt */
    XP3_RE_JMP,             /* go on at arg */
    XP3_RE_SAVE,            /* store the position in capture slot arg */
    XP3_RE_MATCH
} xp3RegexOp;

typedef struct {
    uint8_t op;
    uint8_t count;          /* XP3_RE_CHAR: the characters in chars */
    int32_t arg;
    int32_t alt;            /* XP3_RE_SPLIT: the branch tried second */
    int32_t mark;           /* XP3_RE_SPLIT: its mark against empty
                             * loops in the backtracking matcher, or -1 */
    int32_t chars[XP3_REGEX_CHARS_MAX];
} xp3RegexInst;

typedef enum {
    XP3_RE_ITEM_RANGE = 0,  /* first to last, with case variants */
    XP3_RE_ITEM_BLOCK,      /* first to last, a block, without them */
    XP3_RE_ITEM_CATEGORY,   /* xp3UnicodeCategory first to last */
    XP3_RE_ITEM_SPACE,      /* \s */
    XP3_RE_ITEM_NAME_START, /* \i */
    XP3_RE_ITEM_NAME,       /* \c */
    XP3_RE_ITEM_WORD        /* \w */
} xp3RegexItemKind;

/* A part of a character class */
typedef struct {
    uint8_t kind;
    uint8_t negated;        /* the complement: \S, \P{..} and the like */
    int32_t first;
    int32_t last;
} xp3RegexItem;

/* A character class: the items from first, count of them, negated by
 * ^, minus the class subtracted, or -1 */
typedef struct {
    int32_t first;
    int32_t count;
    int32_t subtracted;
    uint8_t negated;
} xp3RegexClass;

struct _xp3Regex {
    xp3RegexInst *program;
    int programLength;
    xp3RegexClass *classes;
    xp3RegexItem *items;
    int groupCount;
    int markCount;          /* marks of the SPLITs of empty loops */
    int fold;               /* the i flag */
    int multiline;          /* the m flag */
    int backrefs;           /* back-references: the backtracking matcher
                             * runs it */
};

/*
 * The tree
 */

typedef enum {
    XP3_RE_NODE_EMPTY = 0,
    XP3_RE_NODE_CHAR,       /* value: the code point */
    XP3_RE_NODE_ANY,        /* . */
    XP3_RE_NODE_CLASS,      /* value: the class */
    XP3_RE_NODE_LINE_START,
    XP3_RE_NODE_LINE_END,
    XP3_RE_NODE_BACKREF,    /* value: the group */
    XP3_RE_NODE_GROUP,      /* value: the group; child: its contents */
    XP3_RE_NODE_REPEAT,     /* min, max, greedy; child: what repeats */
    XP3_RE_NODE_CONCAT,     /* the children in order */
    XP3_RE_NODE_ALT         /* the children as alternatives */
} xp3RegexNodeKind;

typedef struct {
    uint8_t kind;
    uint8_t greedy;
    uint8_t nullable;       /* can match the empty string */
    int32_t value;
    int32_t min;
    int32_t max;            /* -1 for no upper bound */
    int32_t child;          /* the first child, or -1 */
    int32_t last;           /* the last child, or -1 */
    int32_t next;           /* the next sibling, or -1 */
    int64_t size;           /* instructions, at most
                             * XP3_REGEX_MAX_PROGRAM + 1 */
} xp3RegexNode;

typedef struct {
    const xmlChar *pattern;
    size_t len;
    size_t pos;
    int extended;           /* the x flag: skip whitespace */
    int inClass;            /* classes being read: nothing is skipped */
    int depth;
    xp3RegexNode *nodes;
    int nodeCount;
    int nodeCapacity;
    xp3RegexClass *classes;
    int classCount;
    int classCapacity;
    xp3RegexItem *items;
    int itemCount;
    int itemCapacity;
    unsigned char *closed;  /* closed[n]: group n is closed */
    int groupCount;
    int groupCapacity;
    int backrefs;
    xp3Error *error;
} xp3RegexParser;

/* The flags of F&O 3.1, 5.6.2 */
typedef struct {
    int dotAll;             /* s */
    int multiline;          /* m */
    int fold;               /* i */
    int extended;           /* x */
    int literal;            /* q */
} xp3RegexFlags;

/* The general categories of \p{..}, and their one-letter groups,
 * which are runs of xp3UnicodeCategory */
static const struct {
    const char *name;
    xp3UnicodeCategory first;
    xp3UnicodeCategory last;
} xp3RegexCategories[] = {
    { "L", XP3_UNICODE_LU, XP3_UNICODE_LO },
    { "Lu", XP3_UNICODE_LU, XP3_UNICODE_LU },
    { "Ll", XP3_UNICODE_LL, XP3_UNICODE_LL },
    { "Lt", XP3_UNICODE_LT, XP3_UNICODE_LT },
    { "Lm", XP3_UNICODE_LM, XP3_UNICODE_LM },
    { "Lo", XP3_UNICODE_LO, XP3_UNICODE_LO },
    { "M", XP3_UNICODE_MN, XP3_UNICODE_ME },
    { "Mn", XP3_UNICODE_MN, XP3_UNICODE_MN },
    { "Mc", XP3_UNICODE_MC, XP3_UNICODE_MC },
    { "Me", XP3_UNICODE_ME, XP3_UNICODE_ME },
    { "N", XP3_UNICODE_ND, XP3_UNICODE_NO },
    { "Nd", XP3_UNICODE_ND, XP3_UNICODE_ND },
    { "Nl", XP3_UNICODE_NL, XP3_UNICODE_NL },
    { "No", XP3_UNICODE_NO, XP3_UNICODE_NO },
    { "P", XP3_UNICODE_PC, XP3_UNICODE_PO },
    { "Pc", XP3_UNICODE_PC, XP3_UNICODE_PC },
    { "Pd", XP3_UNICODE_PD, XP3_UNICODE_PD },
    { "Ps", XP3_UNICODE_PS, XP3_UNICODE_PS },
    { "Pe", XP3_UNICODE_PE, XP3_UNICODE_PE },
    { "Pi", XP3_UNICODE_PI, XP3_UNICODE_PI },
    { "Pf", XP3_UNICODE_PF, XP3_UNICODE_PF },
    { "Po", XP3_UNICODE_PO, XP3_UNICODE_PO },
    { "Z", XP3_UNICODE_ZS, XP3_UNICODE_ZP },
    { "Zs", XP3_UNICODE_ZS, XP3_UNICODE_ZS },
    { "Zl", XP3_UNICODE_ZL, XP3_UNICODE_ZL },
    { "Zp", XP3_UNICODE_ZP, XP3_UNICODE_ZP },
    { "S", XP3_UNICODE_SM, XP3_UNICODE_SO },
    { "Sm", XP3_UNICODE_SM, XP3_UNICODE_SM },
    { "Sc", XP3_UNICODE_SC, XP3_UNICODE_SC },
    { "Sk", XP3_UNICODE_SK, XP3_UNICODE_SK },
    { "So", XP3_UNICODE_SO, XP3_UNICODE_SO },
    /* C includes Cs, which no string holds; \p{Cs} isn't allowed */
    { "C", XP3_UNICODE_CC, XP3_UNICODE_CN },
    { "Cc", XP3_UNICODE_CC, XP3_UNICODE_CC },
    { "Cf", XP3_UNICODE_CF, XP3_UNICODE_CF },
    { "Co", XP3_UNICODE_CO, XP3_UNICODE_CO },
    { "Cn", XP3_UNICODE_CN, XP3_UNICODE_CN }
};

#define XP3_REGEX_CATEGORY_COUNT \
    (sizeof(xp3RegexCategories) / sizeof(xp3RegexCategories[0]))

/*
 * Parser helpers
 */

/* Record the first error of a parse, with the byte offset in the
 * pattern that it concerns */
static void
parserError(xp3RegexParser *p, xp3ErrCode code, size_t offset,
            const char *format, ...) {
    char what[200];
    va_list args;

    if (p->error != NULL)
        return;
    va_start(args, format);
    vsnprintf(what, sizeof(what), format, args);
    va_end(args);
    p->error = xp3ErrorNew(code, 0, 0,
                           "%s at offset %zu of the regular expression",
                           what, offset);
}

static void
parserNoMemory(xp3RegexParser *p) {
    if (p->error == NULL)
        p->error = xp3ErrorNoMemory();
}

/* Whitespace as the x flag removes it */
static int
isRegexSpace(int c) {
    return((c == 0x20) || (c == 0x9) || (c == 0xA) || (c == 0xD));
}

static int
isDigit(int c) {
    return((c >= '0') && (c <= '9'));
}

/* Skip the whitespace the x flag removes, which is all of it outside
 * character classes */
static void
skipIgnored(xp3RegexParser *p) {
    if ((!p->extended) || (p->inClass > 0))
        return;
    while ((p->pos < p->len) && isRegexSpace(p->pattern[p->pos]))
        p->pos++;
}

/* The next character of the pattern, or -1 at the end */
static int
peekChar(xp3RegexParser *p) {
    size_t pos;

    skipIgnored(p);
    if (p->pos >= p->len)
        return(-1);
    pos = p->pos;
    return(xp3NextChar(p->pattern, p->len, &pos));
}

/* Read the next character of the pattern, or -1 at the end */
static int
nextChar(xp3RegexParser *p) {
    skipIgnored(p);
    if (p->pos >= p->len)
        return(-1);
    return(xp3NextChar(p->pattern, p->len, &p->pos));
}

/* A byte ahead of the position, in a class where nothing is skipped,
 * or -1 past the end. The bytes looked for are ASCII, which never
 * appear inside the encoding of another character. */
static int
peekByte(const xp3RegexParser *p, size_t ahead) {
    if (ahead >= p->len - p->pos)
        return(-1);
    return(p->pattern[p->pos + ahead]);
}

/* Make room for one more element in an array of count elements of
 * size bytes. Returns the array, moved or not, or NULL if memory
 * allocation failed, with the array left as it was. */
static void *
growArray(void *array, int count, int *capacity, size_t size) {
    void *grown;
    int newCapacity = 16;

    if (count < *capacity)
        return(array);
    if (*capacity > 0)
        newCapacity = *capacity * 2;
    grown = xmlRealloc(array, (size_t) newCapacity * size);
    if (grown == NULL)
        return(NULL);
    *capacity = newCapacity;
    return(grown);
}

/* Fail with LIMIT if the parts of a pattern have reached their limit */
static int
checkParts(xp3RegexParser *p, int count) {
    if (count < XP3_REGEX_MAX_PARTS)
        return(0);
    parserError(p, XP3_ERR_LIMIT, p->pos, "regular expression too large");
    return(-1);
}

/* Fail with LIMIT if a node compiles to too many instructions */
static int
checkSize(xp3RegexParser *p, int node) {
    if (p->nodes[node].size <= XP3_REGEX_MAX_PROGRAM)
        return(0);
    parserError(p, XP3_ERR_LIMIT, p->pos,
                "regular expression too large: more than %d instructions",
                XP3_REGEX_MAX_PROGRAM);
    return(-1);
}

/* A size, or the limit plus one if it is larger */
static int64_t
capSize(int64_t size) {
    if (size > XP3_REGEX_MAX_PROGRAM)
        return(XP3_REGEX_MAX_PROGRAM + 1);
    return(size);
}

/*
 * The tree
 */

/* Add a node. Returns its index, or -1 on error. */
static int
newNode(xp3RegexParser *p, xp3RegexNodeKind kind) {
    xp3RegexNode *nodes;
    xp3RegexNode *node;

    if (checkParts(p, p->nodeCount) < 0)
        return(-1);
    nodes = growArray(p->nodes, p->nodeCount, &p->nodeCapacity,
                      sizeof(*nodes));
    if (nodes == NULL) {
        parserNoMemory(p);
        return(-1);
    }
    p->nodes = nodes;
    node = &p->nodes[p->nodeCount];
    memset(node, 0, sizeof(*node));
    node->kind = kind;
    node->greedy = 1;
    node->child = -1;
    node->last = -1;
    node->next = -1;
    switch (kind) {
        case XP3_RE_NODE_CHAR:
        case XP3_RE_NODE_ANY:
        case XP3_RE_NODE_CLASS:
            node->size = 1;
            break;
        case XP3_RE_NODE_LINE_START:
        case XP3_RE_NODE_LINE_END:
        case XP3_RE_NODE_BACKREF:
            node->size = 1;
            node->nullable = 1;
            break;
        default:
            node->size = 0;
            node->nullable = 1;
            break;
    }
    return(p->nodeCount++);
}

/* Add a node with a value, such as a character */
static int
newValueNode(xp3RegexParser *p, xp3RegexNodeKind kind, int value) {
    int node = newNode(p, kind);

    if (node >= 0)
        p->nodes[node].value = value;
    return(node);
}

/* Append a child to a concatenation or alternation, and update its
 * size: an alternation takes a SPLIT and a JMP per alternative but the
 * last one. Returns 0, or -1 if the result is too large. */
static int
appendChild(xp3RegexParser *p, int parent, int child) {
    xp3RegexNode *parentNode = &p->nodes[parent];
    const xp3RegexNode *childNode = &p->nodes[child];
    int first = (parentNode->child < 0);

    if (first)
        parentNode->child = child;
    else
        p->nodes[parentNode->last].next = child;
    parentNode->last = child;

    if (parentNode->kind == XP3_RE_NODE_CONCAT) {
        parentNode->size = capSize(parentNode->size + childNode->size);
        if (!childNode->nullable)
            parentNode->nullable = 0;
    } else if (first) {
        parentNode->size = childNode->size;
        parentNode->nullable = childNode->nullable;
    } else {
        parentNode->size = capSize(parentNode->size + childNode->size + 2);
        if (childNode->nullable)
            parentNode->nullable = 1;
    }
    return(checkSize(p, parent));
}

/* The size of a repetition of a body of size bodySize: the copies
 * that must match, then a loop, or a SPLIT and a copy for each
 * optional one */
static int64_t
repeatSize(int64_t bodySize, int64_t min, int64_t max) {
    int64_t size;

    if (bodySize == 0)
        return(0);
    if (max < 0) {
        /* X* is SPLIT X JMP, and X{n,} is n - 1 copies and the loop of
         * X+, X SPLIT */
        if (min == 0)
            return(capSize(bodySize + 2));
        size = (min - 1) * bodySize + bodySize + 1;
        return(capSize(size));
    }
    size = min * bodySize + (max - min) * (bodySize + 1);
    return(capSize(size));
}

/*
 * Character classes
 */

/* Add a class without items yet. Returns its index, or -1. */
static int
newClass(xp3RegexParser *p) {
    xp3RegexClass *classes;
    xp3RegexClass *cls;

    if (checkParts(p, p->classCount) < 0)
        return(-1);
    classes = growArray(p->classes, p->classCount, &p->classCapacity,
                        sizeof(*classes));
    if (classes == NULL) {
        parserNoMemory(p);
        return(-1);
    }
    p->classes = classes;
    cls = &p->classes[p->classCount];
    cls->first = p->itemCount;
    cls->count = 0;
    cls->subtracted = -1;
    cls->negated = 0;
    return(p->classCount++);
}

/* Add an item to the class being read. Returns 0, or -1. */
static int
addItem(xp3RegexParser *p, const xp3RegexItem *item) {
    xp3RegexItem *items;

    if (checkParts(p, p->itemCount) < 0)
        return(-1);
    items = growArray(p->items, p->itemCount, &p->itemCapacity,
                      sizeof(*items));
    if (items == NULL) {
        parserNoMemory(p);
        return(-1);
    }
    p->items = items;
    p->items[p->itemCount] = *item;
    p->itemCount++;
    return(0);
}

/* Add a range of characters, which the i flag extends */
static int
addRange(xp3RegexParser *p, int first, int last) {
    xp3RegexItem item;

    memset(&item, 0, sizeof(item));
    item.kind = XP3_RE_ITEM_RANGE;
    item.first = first;
    item.last = last;
    return(addItem(p, &item));
}

/* A class node of one class escape, such as \d outside brackets */
static int
newEscapeClassNode(xp3RegexParser *p, const xp3RegexItem *item) {
    int cls = newClass(p);

    if (cls < 0)
        return(-1);
    if (addItem(p, item) < 0)
        return(-1);
    p->classes[cls].count = 1;
    return(newValueNode(p, XP3_RE_NODE_CLASS, cls));
}

/* Whether a block name, without "Is", has only the characters that
 * production IsBlock allows: [a-zA-Z0-9#x2D]+ */
static int
isBlockName(const char *name, size_t len) {
    size_t i;

    if (len == 0)
        return(0);
    for (i = 0; i < len; i++) {
        char c = name[i];
        int letter = ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z'));

        if ((!letter) && (!isDigit(c)) && (c != '-'))
            return(0);
    }
    return(1);
}

/* Turn the name of \p{..} into an item: a block (IsX, F&O 3.1,
 * 5.6.1.5) or a general category */
static int
propertyItem(xp3RegexParser *p, size_t offset, const char *name,
             size_t len, xp3RegexItem *item) {
    size_t i;

    if ((len >= 2) && (name[0] == 'I') && (name[1] == 's')) {
        int first;
        int last;

        if (!isBlockName(name + 2, len - 2)) {
            parserError(p, XP3_ERR_FORX0002, offset,
                        "invalid block name in \\p{..}");
            return(-1);
        }
        if (xp3UnicodeBlock(BAD_CAST name + 2, len - 2, &first, &last) < 0) {
            parserError(p, XP3_ERR_FORX0002, offset,
                        "unknown block name '%s'", name);
            return(-1);
        }
        item->kind = XP3_RE_ITEM_BLOCK;
        item->first = first;
        item->last = last;
        return(0);
    }
    for (i = 0; i < XP3_REGEX_CATEGORY_COUNT; i++) {
        if (strcmp(xp3RegexCategories[i].name, name) == 0) {
            item->kind = XP3_RE_ITEM_CATEGORY;
            item->first = xp3RegexCategories[i].first;
            item->last = xp3RegexCategories[i].last;
            return(0);
        }
    }
    parserError(p, XP3_ERR_FORX0002, offset,
                "unknown character property in \\p{..} or \\P{..}");
    return(-1);
}

/* Parse the {name} of \p{name} or \P{name} */
static int
parseProperty(xp3RegexParser *p, size_t offset, int negated,
              xp3RegexItem *item) {
    char name[XP3_REGEX_MAX_PROPERTY + 1];
    size_t len = 0;
    int unknown = 0;
    int c;

    if (nextChar(p) != '{') {
        parserError(p, XP3_ERR_FORX0002, offset,
                    "'{' missing after \\p or \\P");
        return(-1);
    }
    while (1) {
        c = nextChar(p);
        if (c < 0) {
            parserError(p, XP3_ERR_FORX0002, offset,
                        "'}' missing after \\p{ or \\P{");
            return(-1);
        }
        if (c == '}')
            break;
        if ((c < 0x20) || (c > 0x7E) || (len >= XP3_REGEX_MAX_PROPERTY))
            unknown = 1;
        else
            name[len++] = (char) c;
    }
    name[len] = 0;
    if (unknown) {
        parserError(p, XP3_ERR_FORX0002, offset,
                    "unknown character property in \\p{..} or \\P{..}");
        return(-1);
    }
    item->negated = (uint8_t) negated;
    return(propertyItem(p, offset, name, len, item));
}

/*
 * Parse what follows a backslash, but for a back-reference: a single
 * character escape sets *c and returns XP3_REGEX_ESCAPE_CHAR, and a
 * class escape (multi-character or category) sets *item and returns
 * XP3_REGEX_ESCAPE_ITEM. Returns -1 on error. offset is that of the
 * backslash.
 */
static int
parseEscape(xp3RegexParser *p, size_t offset, int *c, xp3RegexItem *item) {
    int letter = nextChar(p);

    memset(item, 0, sizeof(*item));
    switch (letter) {
        case 'n':
            *c = 0xA;
            return(XP3_REGEX_ESCAPE_CHAR);
        case 'r':
            *c = 0xD;
            return(XP3_REGEX_ESCAPE_CHAR);
        case 't':
            *c = 0x9;
            return(XP3_REGEX_ESCAPE_CHAR);
        case '\\': case '|': case '.': case '-': case '^': case '?':
        case '*': case '+': case '{': case '}': case '(': case ')':
        case '[': case ']': case '$':
            *c = letter;
            return(XP3_REGEX_ESCAPE_CHAR);
        case 's':
        case 'S':
            item->kind = XP3_RE_ITEM_SPACE;
            item->negated = (letter == 'S');
            return(XP3_REGEX_ESCAPE_ITEM);
        case 'i':
        case 'I':
            item->kind = XP3_RE_ITEM_NAME_START;
            item->negated = (letter == 'I');
            return(XP3_REGEX_ESCAPE_ITEM);
        case 'c':
        case 'C':
            item->kind = XP3_RE_ITEM_NAME;
            item->negated = (letter == 'C');
            return(XP3_REGEX_ESCAPE_ITEM);
        case 'd':
        case 'D':
            item->kind = XP3_RE_ITEM_CATEGORY;
            item->first = XP3_UNICODE_ND;
            item->last = XP3_UNICODE_ND;
            item->negated = (letter == 'D');
            return(XP3_REGEX_ESCAPE_ITEM);
        case 'w':
        case 'W':
            item->kind = XP3_RE_ITEM_WORD;
            item->negated = (letter == 'W');
            return(XP3_REGEX_ESCAPE_ITEM);
        case 'p':
        case 'P':
            if (parseProperty(p, offset, letter == 'P', item) < 0)
                return(-1);
            return(XP3_REGEX_ESCAPE_ITEM);
        default:
            break;
    }
    if (letter < 0)
        parserError(p, XP3_ERR_FORX0002, offset,
                    "'\\' at the end of the pattern");
    else
        parserError(p, XP3_ERR_FORX0002, offset, "invalid escape");
    return(-1);
}

/*
 * Read one character of a character group, plain or escaped: a single
 * character sets *c and returns XP3_REGEX_ESCAPE_CHAR, with *plainHyphen
 * set for an unescaped '-'; a class escape sets *item and returns
 * XP3_REGEX_ESCAPE_ITEM. Returns -1 on error.
 */
static int
readClassChar(xp3RegexParser *p, int *c, xp3RegexItem *item,
              int *plainHyphen) {
    size_t offset = p->pos;
    int ch = nextChar(p);

    *plainHyphen = 0;
    if (ch == '[') {
        parserError(p, XP3_ERR_FORX0002, offset,
                    "'[' must be escaped in a character class");
        return(-1);
    }
    if (ch == ']') {
        parserError(p, XP3_ERR_FORX0002, offset,
                    "a range must end with a character");
        return(-1);
    }
    if (ch != '\\') {
        *c = ch;
        *plainHyphen = (ch == '-');
        return(XP3_REGEX_ESCAPE_CHAR);
    }
    if (isDigit(peekChar(p))) {
        parserError(p, XP3_ERR_FORX0002, offset,
                    "no back-reference is allowed in a character class");
        return(-1);
    }
    return(parseEscape(p, offset, c, item));
}

/*
 * Parse the rest of a range, from the hyphen after its first
 * character: the last character must be single and not an unescaped
 * hyphen (XSD 1.1, G.4.1), and not before the first.
 */
static int
parseRangeEnd(xp3RegexParser *p, size_t offset, int first,
              int firstIsHyphen) {
    xp3RegexItem item;
    int last = 0;
    int plainHyphen;
    int kind;

    if (firstIsHyphen) {
        parserError(p, XP3_ERR_FORX0002, offset,
                    "an unescaped '-' cannot start a range");
        return(-1);
    }
    nextChar(p);
    if (peekByte(p, 0) < 0) {
        parserError(p, XP3_ERR_FORX0002, offset, "unclosed '['");
        return(-1);
    }
    kind = readClassChar(p, &last, &item, &plainHyphen);
    if (kind < 0)
        return(-1);
    if ((kind != XP3_REGEX_ESCAPE_CHAR) || plainHyphen) {
        parserError(p, XP3_ERR_FORX0002, offset,
                    "a range must end with a single character other "
                    "than an unescaped '-'");
        return(-1);
    }
    if (last < first) {
        parserError(p, XP3_ERR_FORX0002, offset,
                    "a range ends before it starts");
        return(-1);
    }
    return(addRange(p, first, last));
}

/* Whether the positive group of a class ends at a byte ahead: at its
 * ']', or at the '-[' of a subtraction */
static int
atGroupEnd(const xp3RegexParser *p, size_t ahead) {
    int c = peekByte(p, ahead);

    if (c == ']')
        return(1);
    return((c == '-') && (peekByte(p, ahead + 1) == '['));
}

/*
 * Parse the parts of a positive character group, up to the ']' or the
 * '-[' of a subtraction. A hyphen between two single characters makes
 * a range; an unescaped hyphen is a character only as the first or the
 * last part of the group (XSD 1.0, F.1).
 */
static int
parseClassParts(xp3RegexParser *p, size_t offset) {
    int parts = 0;

    while (1) {
        xp3RegexItem item;
        size_t partOffset = p->pos;
        int first = 0;
        int plainHyphen;
        int kind;

        if (peekByte(p, 0) < 0) {
            parserError(p, XP3_ERR_FORX0002, offset, "unclosed '['");
            return(-1);
        }
        if (atGroupEnd(p, 0))
            return(0);

        kind = readClassChar(p, &first, &item, &plainHyphen);
        if (kind < 0)
            return(-1);
        parts++;
        if (kind == XP3_REGEX_ESCAPE_ITEM) {
            if (addItem(p, &item) < 0)
                return(-1);
            continue;
        }
        if ((peekByte(p, 0) == '-') && (!atGroupEnd(p, 0)) &&
            (!atGroupEnd(p, 1))) {
            if (parseRangeEnd(p, partOffset, first, plainHyphen) < 0)
                return(-1);
            continue;
        }
        if (plainHyphen && (parts > 1) && (!atGroupEnd(p, 0))) {
            parserError(p, XP3_ERR_FORX0002, partOffset,
                        "an unescaped '-' must be the first or the last "
                        "character of a character group");
            return(-1);
        }
        if (addRange(p, first, first) < 0)
            return(-1);
    }
}

/* Parse a character class expression, after its '['. Returns the
 * class, or -1. */
static int
parseClassExpr(xp3RegexParser *p, size_t offset) {
    int cls;

    if (p->depth >= XP3_REGEX_MAX_DEPTH) {
        parserError(p, XP3_ERR_LIMIT, offset,
                    "character classes nested too deeply");
        return(-1);
    }
    p->depth++;
    p->inClass++;

    cls = newClass(p);
    if (cls < 0)
        return(-1);
    if (peekByte(p, 0) == '^') {
        nextChar(p);
        p->classes[cls].negated = 1;
    }
    if (parseClassParts(p, offset) < 0)
        return(-1);
    p->classes[cls].count = p->itemCount - p->classes[cls].first;
    if (p->classes[cls].count == 0) {
        parserError(p, XP3_ERR_FORX0002, offset,
                    "empty character class");
        return(-1);
    }

    if (peekByte(p, 0) == '-') {
        size_t subOffset;
        int subtracted;

        nextChar(p);
        subOffset = p->pos;
        nextChar(p);
        subtracted = parseClassExpr(p, subOffset);
        if (subtracted < 0)
            return(-1);
        p->classes[cls].subtracted = subtracted;
        if (peekByte(p, 0) != ']') {
            parserError(p, XP3_ERR_FORX0002, offset,
                        "']' expected after a class subtraction");
            return(-1);
        }
    }
    nextChar(p);

    p->inClass--;
    p->depth--;
    return(cls);
}

/*
 * Atoms and pieces
 */

static int
parseRegExp(xp3RegexParser *p);

/* Parse a group, from its '(' at offset */
static int
parseGroup(xp3RegexParser *p, size_t offset) {
    int capturing = 1;
    int number = 0;
    int child;
    int node;

    if (p->depth >= XP3_REGEX_MAX_DEPTH) {
        parserError(p, XP3_ERR_LIMIT, offset, "groups nested too deeply");
        return(-1);
    }
    p->depth++;
    nextChar(p);
    if (peekChar(p) == '?') {
        nextChar(p);
        if (nextChar(p) != ':') {
            parserError(p, XP3_ERR_FORX0002, offset,
                        "'(?' must be followed by ':'");
            return(-1);
        }
        capturing = 0;
    }
    if (capturing) {
        unsigned char *closed;

        if (checkParts(p, p->groupCount) < 0)
            return(-1);
        number = p->groupCount + 1;
        closed = growArray(p->closed, number, &p->groupCapacity, 1);
        if (closed == NULL) {
            parserNoMemory(p);
            return(-1);
        }
        p->closed = closed;
        p->closed[number] = 0;
        p->groupCount = number;
    }

    child = parseRegExp(p);
    if (child < 0)
        return(-1);
    if (peekChar(p) != ')') {
        parserError(p, XP3_ERR_FORX0002, offset, "unclosed '('");
        return(-1);
    }
    nextChar(p);
    p->depth--;
    if (!capturing)
        return(child);

    p->closed[number] = 1;
    node = newValueNode(p, XP3_RE_NODE_GROUP, number);
    if (node < 0)
        return(-1);
    p->nodes[node].child = child;
    p->nodes[node].size = capSize(p->nodes[child].size + 2);
    p->nodes[node].nullable = p->nodes[child].nullable;
    if (checkSize(p, node) < 0)
        return(-1);
    return(node);
}

/*
 * Parse a back-reference after its backslash, from its first digit
 * (F&O 3.1, 5.6.1.4): a further digit belongs to it as long as the
 * group with the longer number has opened before it. The group must
 * be closed.
 */
static int
parseBackref(xp3RegexParser *p, size_t offset) {
    int64_t number = nextChar(p) - '0';
    int node;

    while (isDigit(peekChar(p))) {
        int64_t longer = number * 10 + (peekChar(p) - '0');

        if (longer > p->groupCount)
            break;
        nextChar(p);
        number = longer;
    }
    if ((number > p->groupCount) || (!p->closed[number])) {
        parserError(p, XP3_ERR_FORX0002, offset,
                    "back-reference to group %d, which isn't closed "
                    "before it", (int) number);
        return(-1);
    }
    p->backrefs = 1;
    node = newValueNode(p, XP3_RE_NODE_BACKREF, (int) number);
    return(node);
}

/* Parse an escape outside brackets, from its backslash */
static int
parseAtomEscape(xp3RegexParser *p, size_t offset) {
    xp3RegexItem item;
    int c = 0;
    int kind;

    nextChar(p);
    if (isDigit(peekChar(p))) {
        if (peekChar(p) == '0') {
            parserError(p, XP3_ERR_FORX0002, offset, "invalid escape");
            return(-1);
        }
        return(parseBackref(p, offset));
    }
    kind = parseEscape(p, offset, &c, &item);
    if (kind < 0)
        return(-1);
    if (kind == XP3_REGEX_ESCAPE_ITEM)
        return(newEscapeClassNode(p, &item));
    return(newValueNode(p, XP3_RE_NODE_CHAR, c));
}

static int
parseAtom(xp3RegexParser *p) {
    size_t offset;
    int c = peekChar(p);
    int cls;

    offset = p->pos;
    switch (c) {
        case '(':
            return(parseGroup(p, offset));
        case '[':
            nextChar(p);
            cls = parseClassExpr(p, offset);
            if (cls < 0)
                return(-1);
            return(newValueNode(p, XP3_RE_NODE_CLASS, cls));
        case '\\':
            return(parseAtomEscape(p, offset));
        case '.':
            nextChar(p);
            return(newNode(p, XP3_RE_NODE_ANY));
        case '^':
            nextChar(p);
            return(newNode(p, XP3_RE_NODE_LINE_START));
        case '$':
            nextChar(p);
            return(newNode(p, XP3_RE_NODE_LINE_END));
        case '?':
        case '*':
        case '+':
            parserError(p, XP3_ERR_FORX0002, offset,
                        "quantifier '%c' without anything to repeat", c);
            return(-1);
        case '{':
        case '}':
        case ']':
            parserError(p, XP3_ERR_FORX0002, offset,
                        "'%c' must be escaped", c);
            return(-1);
        default:
            break;
    }
    nextChar(p);
    return(newValueNode(p, XP3_RE_NODE_CHAR, c));
}

/* Parse the digits of a quantity. A number above INT32_MAX is
 * FORX0002, as QT3 allows, since no program could hold that many
 * copies anyway. */
static int
parseNumber(xp3RegexParser *p, size_t offset, int64_t *value) {
    int64_t number = 0;

    if (!isDigit(peekChar(p))) {
        parserError(p, XP3_ERR_FORX0002, offset,
                    "a number is missing in a quantifier");
        return(-1);
    }
    while (isDigit(peekChar(p))) {
        number = number * 10 + (nextChar(p) - '0');
        if (number > INT32_MAX) {
            parserError(p, XP3_ERR_FORX0002, offset,
                        "number too large in a quantifier");
            return(-1);
        }
    }
    *value = number;
    return(0);
}

/* Parse {n}, {n,} or {n,m}, from the '{' */
static int
parseQuantity(xp3RegexParser *p, int *min, int *max) {
    size_t offset = p->pos;
    int64_t low;
    int64_t high;

    nextChar(p);
    if (parseNumber(p, offset, &low) < 0)
        return(-1);
    *min = (int) low;
    *max = (int) low;
    if (peekChar(p) == ',') {
        nextChar(p);
        *max = -1;
        if (peekChar(p) != '}') {
            if (parseNumber(p, offset, &high) < 0)
                return(-1);
            if (high < low) {
                parserError(p, XP3_ERR_FORX0002, offset,
                            "a quantifier {n,m} with m less than n");
                return(-1);
            }
            *max = (int) high;
        }
    }
    if (nextChar(p) != '}') {
        parserError(p, XP3_ERR_FORX0002, offset, "unclosed '{'");
        return(-1);
    }
    return(0);
}

/* Parse an atom and its quantifier, if any */
static int
parsePiece(xp3RegexParser *p) {
    int atom;
    int node;
    int min;
    int max;
    int c;

    atom = parseAtom(p);
    if (atom < 0)
        return(-1);
    c = peekChar(p);
    if (c == '?') {
        min = 0;
        max = 1;
    } else if (c == '*') {
        min = 0;
        max = -1;
    } else if (c == '+') {
        min = 1;
        max = -1;
    } else if (c == '{') {
        if (parseQuantity(p, &min, &max) < 0)
            return(-1);
    } else {
        return(atom);
    }
    if (c != '{')
        nextChar(p);

    node = newNode(p, XP3_RE_NODE_REPEAT);
    if (node < 0)
        return(-1);
    if (peekChar(p) == '?') {
        nextChar(p);
        p->nodes[node].greedy = 0;
    }
    p->nodes[node].child = atom;
    p->nodes[node].min = min;
    p->nodes[node].max = max;
    p->nodes[node].size = repeatSize(p->nodes[atom].size, min, max);
    p->nodes[node].nullable = (min == 0) || p->nodes[atom].nullable;
    if (checkSize(p, node) < 0)
        return(-1);
    return(node);
}

/* Parse a branch: pieces up to '|', ')' or the end */
static int
parseBranch(xp3RegexParser *p) {
    int concat = newNode(p, XP3_RE_NODE_CONCAT);

    if (concat < 0)
        return(-1);
    while (1) {
        int c = peekChar(p);
        int piece;

        if ((c < 0) || (c == '|') || (c == ')'))
            break;
        piece = parsePiece(p);
        if (piece < 0)
            return(-1);
        if (appendChild(p, concat, piece) < 0)
            return(-1);
    }
    return(concat);
}

/* Parse branches separated by '|' */
static int
parseRegExp(xp3RegexParser *p) {
    int branch;
    int alt;

    branch = parseBranch(p);
    if (branch < 0)
        return(-1);
    if (peekChar(p) != '|')
        return(branch);
    alt = newNode(p, XP3_RE_NODE_ALT);
    if (alt < 0)
        return(-1);
    if (appendChild(p, alt, branch) < 0)
        return(-1);
    while (peekChar(p) == '|') {
        nextChar(p);
        branch = parseBranch(p);
        if (branch < 0)
            return(-1);
        if (appendChild(p, alt, branch) < 0)
            return(-1);
    }
    return(alt);
}

/* With the q flag, every character stands for itself */
static int
parseLiteral(xp3RegexParser *p) {
    int concat = newNode(p, XP3_RE_NODE_CONCAT);

    if (concat < 0)
        return(-1);
    while (p->pos < p->len) {
        int c = xp3NextChar(p->pattern, p->len, &p->pos);
        int node = newValueNode(p, XP3_RE_NODE_CHAR, c);

        if (node < 0)
            return(-1);
        if (appendChild(p, concat, node) < 0)
            return(-1);
    }
    return(concat);
}

/*
 * Flags
 */

/* Read the flags (F&O 3.1, 5.6.2). Returns 0, or -1 with FORX0001. */
static int
parseFlags(const xmlChar *flags, size_t len, xp3LanguageLevel level,
           xp3RegexFlags *out, xp3Error **error) {
    size_t pos = 0;

    memset(out, 0, sizeof(*out));
    while (pos < len) {
        int c = xp3NextChar(flags, len, &pos);

        if (c == 's') {
            out->dotAll = 1;
        } else if (c == 'm') {
            out->multiline = 1;
        } else if (c == 'i') {
            out->fold = 1;
        } else if (c == 'x') {
            out->extended = 1;
        } else if ((c == 'q') && (level >= XP3_XPATH_30)) {
            out->literal = 1;
        } else if (c == 'q') {
            *error = xp3ErrorNew(XP3_ERR_FORX0001, 0, 0,
                                 "the regular expression flag 'q' needs "
                                 "XPath 3.0");
            return(-1);
        } else {
            xmlChar utf8[5];
            int bytes = xp3EncodeChar(c, utf8);

            utf8[bytes] = 0;
            *error = xp3ErrorNew(XP3_ERR_FORX0001, 0, 0,
                                 "invalid regular expression flag '%s'",
                                 (const char *) utf8);
            return(-1);
        }
    }
    return(0);
}

/*
 * Compiling the tree
 */

typedef struct {
    const xp3RegexNode *nodes;
    xp3RegexInst *program;
    int count;
    int nullableLoops;      /* loops with nullable bodies being emitted */
    int markCount;
    int fold;
    int dotAll;
} xp3RegexEmitter;

/* Append an instruction */
static xp3RegexInst *
emitInst(xp3RegexEmitter *e, xp3RegexOp op, int arg) {
    xp3RegexInst *inst = &e->program[e->count];

    memset(inst, 0, sizeof(*inst));
    inst->op = (uint8_t) op;
    inst->arg = arg;
    inst->mark = -1;
    e->count++;
    return(inst);
}

/* Append a SPLIT, with a mark if a path can come back to it without
 * reading a character. Returns its index; setSplit() sets its
 * branches. */
static int
emitSplit(xp3RegexEmitter *e) {
    xp3RegexInst *inst = emitInst(e, XP3_RE_SPLIT, 0);

    if (e->nullableLoops > 0) {
        inst->mark = e->markCount;
        e->markCount++;
    }
    return(e->count - 1);
}

/* Set the branches of a SPLIT: a greedy one tries the preferred branch
 * first, a reluctant one the other */
static void
setSplit(xp3RegexEmitter *e, int split, int preferred, int other,
         int greedy) {
    if (greedy) {
        e->program[split].arg = preferred;
        e->program[split].alt = other;
    } else {
        e->program[split].arg = other;
        e->program[split].alt = preferred;
    }
}

static void
emitNode(xp3RegexEmitter *e, int index);

/* A character, and under the i flag its case variants */
static void
emitChar(xp3RegexEmitter *e, int c) {
    xp3RegexInst *inst = emitInst(e, XP3_RE_CHAR, 0);
    int variants[XP3_UNICODE_VARIANTS_MAX];
    int count = 0;
    int i;

    inst->chars[0] = c;
    inst->count = 1;
    if (e->fold)
        count = xp3UnicodeCaseVariants(c, variants);
    for (i = 0; i < count; i++) {
        inst->chars[inst->count] = variants[i];
        inst->count++;
    }
}

/* X*: L: SPLIT body, end; body; JMP L */
static void
emitStar(xp3RegexEmitter *e, const xp3RegexNode *node) {
    int nullable = e->nodes[node->child].nullable;
    int loop;
    int split;

    if (nullable)
        e->nullableLoops++;
    loop = e->count;
    split = emitSplit(e);
    emitNode(e, node->child);
    emitInst(e, XP3_RE_JMP, loop);
    setSplit(e, split, split + 1, e->count, node->greedy);
    if (nullable)
        e->nullableLoops--;
}

/* X+: L: body; SPLIT L, end */
static void
emitPlus(xp3RegexEmitter *e, const xp3RegexNode *node) {
    int nullable = e->nodes[node->child].nullable;
    int body = e->count;
    int split;

    if (nullable)
        e->nullableLoops++;
    emitNode(e, node->child);
    split = emitSplit(e);
    setSplit(e, split, body, e->count, node->greedy);
    if (nullable)
        e->nullableLoops--;
}

/* count optional copies, each SPLIT copy, end */
static void
emitOptional(xp3RegexEmitter *e, const xp3RegexNode *node, int count) {
    int bodySize = (int) e->nodes[node->child].size;
    int end = e->count + count * (bodySize + 1);
    int i;

    for (i = 0; i < count; i++) {
        int split = emitSplit(e);

        setSplit(e, split, split + 1, end, node->greedy);
        emitNode(e, node->child);
    }
}

static void
emitRepeat(xp3RegexEmitter *e, const xp3RegexNode *node) {
    int i;

    /* Repeating nothing is nothing, however often */
    if (e->nodes[node->child].size == 0)
        return;
    if ((node->max < 0) && (node->min == 0)) {
        emitStar(e, node);
        return;
    }
    if (node->max < 0) {
        for (i = 1; i < node->min; i++)
            emitNode(e, node->child);
        emitPlus(e, node);
        return;
    }
    for (i = 0; i < node->min; i++)
        emitNode(e, node->child);
    emitOptional(e, node, node->max - node->min);
}

/* a|b|c: SPLIT a, next; a; JMP end; next: SPLIT b, c; b; JMP end; c */
static void
emitAlternatives(xp3RegexEmitter *e, const xp3RegexNode *node) {
    int end = e->count + (int) node->size;
    int child = node->child;

    while (child >= 0) {
        int next = e->nodes[child].next;
        int split;

        if (next < 0) {
            emitNode(e, child);
            break;
        }
        split = emitSplit(e);
        emitNode(e, child);
        emitInst(e, XP3_RE_JMP, end);
        setSplit(e, split, split + 1, e->count, 1);
        child = next;
    }
}

/* Emit the instructions of a node. The tree is at most a few levels
 * per level of nesting deep, which the parser limits. */
static void
emitNode(xp3RegexEmitter *e, int index) {
    const xp3RegexNode *node = &e->nodes[index];
    int child;

    switch (node->kind) {
        case XP3_RE_NODE_EMPTY:
            break;
        case XP3_RE_NODE_CHAR:
            emitChar(e, node->value);
            break;
        case XP3_RE_NODE_ANY:
            if (e->dotAll)
                emitInst(e, XP3_RE_ANY, 0);
            else
                emitInst(e, XP3_RE_ANY_NOT_NL, 0);
            break;
        case XP3_RE_NODE_CLASS:
            emitInst(e, XP3_RE_CLASS, node->value);
            break;
        case XP3_RE_NODE_LINE_START:
            emitInst(e, XP3_RE_LINE_START, 0);
            break;
        case XP3_RE_NODE_LINE_END:
            emitInst(e, XP3_RE_LINE_END, 0);
            break;
        case XP3_RE_NODE_BACKREF:
            emitInst(e, XP3_RE_BACKREF, node->value);
            break;
        case XP3_RE_NODE_GROUP:
            emitInst(e, XP3_RE_SAVE, 2 * node->value);
            emitNode(e, node->child);
            emitInst(e, XP3_RE_SAVE, 2 * node->value + 1);
            break;
        case XP3_RE_NODE_REPEAT:
            emitRepeat(e, node);
            break;
        case XP3_RE_NODE_CONCAT:
            for (child = node->child; child >= 0;
                 child = e->nodes[child].next)
                emitNode(e, child);
            break;
        case XP3_RE_NODE_ALT:
            emitAlternatives(e, node);
            break;
        default:
            break;
    }
}

/*
 * Compiling
 */

static void
freeParser(xp3RegexParser *p) {
    xmlFree(p->nodes);
    xmlFree(p->classes);
    xmlFree(p->items);
    xmlFree(p->closed);
}

/* Parse the whole pattern into a tree. Returns the root, or -1. */
static int
parsePattern(xp3RegexParser *p, const xp3RegexFlags *flags) {
    int root;

    if (flags->literal)
        return(parseLiteral(p));
    root = parseRegExp(p);
    if (root < 0)
        return(-1);
    if (p->pos < p->len) {
        /* parseRegExp() stops at the end or at a ')' */
        parserError(p, XP3_ERR_FORX0002, p->pos, "unmatched ')'");
        return(-1);
    }
    return(root);
}

/* Build the program of a parsed pattern: SAVE 0, the pattern, SAVE 1,
 * MATCH. Takes the classes and items of the parser. */
static xp3Regex *
buildRegex(xp3RegexParser *p, int root, const xp3RegexFlags *flags) {
    xp3RegexEmitter e;
    xp3Regex *regex;
    int64_t length = p->nodes[root].size + 3;
    int64_t slots = 2 * ((int64_t) p->groupCount + 1);

    if ((length > XP3_REGEX_MAX_PROGRAM) ||
        (length * slots > XP3_REGEX_MAX_CAPTURE_CELLS)) {
        parserError(p, XP3_ERR_LIMIT, p->len,
                    "regular expression too large: too many instructions "
                    "or groups");
        return(NULL);
    }

    regex = xmlMalloc(sizeof(*regex));
    if (regex == NULL) {
        parserNoMemory(p);
        return(NULL);
    }
    memset(regex, 0, sizeof(*regex));
    regex->program = xmlMalloc((size_t) length * sizeof(xp3RegexInst));
    if (regex->program == NULL) {
        xmlFree(regex);
        parserNoMemory(p);
        return(NULL);
    }

    memset(&e, 0, sizeof(e));
    e.nodes = p->nodes;
    e.program = regex->program;
    e.fold = flags->fold;
    e.dotAll = flags->dotAll;
    emitInst(&e, XP3_RE_SAVE, 0);
    emitNode(&e, root);
    emitInst(&e, XP3_RE_SAVE, 1);
    emitInst(&e, XP3_RE_MATCH, 0);

    regex->programLength = e.count;
    regex->markCount = e.markCount;
    regex->classes = p->classes;
    regex->items = p->items;
    regex->groupCount = p->groupCount;
    regex->fold = flags->fold;
    regex->multiline = flags->multiline;
    regex->backrefs = p->backrefs;
    p->classes = NULL;
    p->items = NULL;
    return(regex);
}

/**
 * Compile a regular expression of F&O 3.1, 5.6.1, with its flags
 * (5.6.2). A byte of the pattern that isn't UTF-8 is the character
 * U+FFFD.
 *
 * @param pattern   the pattern, UTF-8
 * @param len       its length in bytes
 * @param flags     the flags, UTF-8, or NULL for none
 * @param flagsLen  their length in bytes
 * @param level     the language level: the q flag needs XPath 3.0
 * @param error     set on failure: FORX0001 for the flags, FORX0002 for
 *                  the pattern, LIMIT for a pattern nested too deeply
 *                  or one whose program would be too large, or
 *                  NO_MEMORY. The message names the byte offset in the
 *                  pattern.
 * @returns the compiled regular expression, to free with
 *          xp3RegexFree(), or NULL on error
 */
xp3Regex *
xp3RegexCompile(const xmlChar *pattern, size_t len, const xmlChar *flags,
                size_t flagsLen, xp3LanguageLevel level, xp3Error **error) {
    xp3RegexParser p;
    xp3RegexFlags parsedFlags;
    xp3Regex *regex = NULL;
    int root;

    *error = NULL;
    if (parseFlags(flags, flagsLen, level, &parsedFlags, error) < 0)
        return(NULL);

    memset(&p, 0, sizeof(p));
    p.pattern = pattern;
    p.len = len;
    if (!parsedFlags.literal)
        p.extended = parsedFlags.extended;
    if (parsedFlags.literal) {
        /* q: m, s and x have no effect */
        parsedFlags.multiline = 0;
        parsedFlags.dotAll = 0;
    }

    root = parsePattern(&p, &parsedFlags);
    if (root >= 0)
        regex = buildRegex(&p, root, &parsedFlags);
    if (regex == NULL)
        *error = p.error;
    freeParser(&p);
    return(regex);
}

/**
 * Free a compiled regular expression.
 *
 * @param regex  the regular expression, or NULL
 */
void
xp3RegexFree(xp3Regex *regex) {
    if (regex == NULL)
        return;
    xmlFree(regex->program);
    xmlFree(regex->classes);
    xmlFree(regex->items);
    xmlFree(regex);
}

/**
 * Get the number of capturing groups of a regular expression.
 *
 * @param regex  the regular expression
 * @returns the number of groups, not counting group 0, the whole match
 */
int
xp3RegexGroupCount(const xp3Regex *regex) {
    return(regex->groupCount);
}

/*
 * Matching characters
 */

/* Whether an item takes a character, without case variants */
static int
itemTakes(const xp3RegexItem *item, int c) {
    xp3UnicodeCategory category;
    int in = 0;

    switch (item->kind) {
        case XP3_RE_ITEM_RANGE:
        case XP3_RE_ITEM_BLOCK:
            in = (c >= item->first) && (c <= item->last);
            break;
        case XP3_RE_ITEM_CATEGORY:
            category = xp3UnicodeCategoryOf(c);
            in = ((int) category >= item->first) &&
                 ((int) category <= item->last);
            break;
        case XP3_RE_ITEM_SPACE:
            in = isRegexSpace(c);
            break;
        case XP3_RE_ITEM_NAME_START:
            in = xp3IsNameStartChar(c) || (c == ':');
            break;
        case XP3_RE_ITEM_NAME:
            in = xp3IsNameChar(c) || (c == ':');
            break;
        case XP3_RE_ITEM_WORD:
            /* Not in P, Z or C, which leaves L, M, N and S */
            category = xp3UnicodeCategoryOf(c);
            in = (category <= XP3_UNICODE_NO) ||
                 ((category >= XP3_UNICODE_SM) &&
                  (category <= XP3_UNICODE_SO));
            break;
        default:
            break;
    }
    if (item->negated)
        return(!in);
    return(in);
}

/* Whether the group of a class takes a character, before its
 * subtraction: one of its items does, or a range does one of the case
 * variants of the character */
static int
groupTakes(const xp3Regex *regex, const xp3RegexClass *cls, int c,
           const int *variants, int variantCount) {
    int found = 0;
    int i;
    int j;

    for (i = 0; (i < cls->count) && (!found); i++) {
        const xp3RegexItem *item = &regex->items[cls->first + i];

        if (itemTakes(item, c)) {
            found = 1;
        } else if (item->kind == XP3_RE_ITEM_RANGE) {
            for (j = 0; j < variantCount; j++) {
                if ((variants[j] >= item->first) &&
                    (variants[j] <= item->last))
                    found = 1;
            }
        }
    }
    if (cls->negated)
        return(!found);
    return(found);
}

/*
 * Whether a class takes a character. A class G - C takes c if G does
 * and C doesn't, and C may be a subtraction itself. Walking the chain
 * G0 - (G1 - (G2 - ...)), the first group that doesn't take c decides:
 * it makes the class at its depth false, so the whole is true if that
 * depth is odd. If every group takes it, the last one is true.
 */
static int
classTakes(const xp3Regex *regex, int index, int c) {
    int variants[XP3_UNICODE_VARIANTS_MAX];
    int variantCount = 0;
    int depth = 0;

    if (regex->fold)
        variantCount = xp3UnicodeCaseVariants(c, variants);
    while (1) {
        const xp3RegexClass *cls = &regex->classes[index];

        if (!groupTakes(regex, cls, c, variants, variantCount))
            return(depth % 2 == 1);
        if (cls->subtracted < 0)
            return(depth % 2 == 0);
        index = cls->subtracted;
        depth++;
    }
}

/* Whether an instruction that reads a character takes c */
static int
instTakes(const xp3Regex *regex, const xp3RegexInst *inst, int c) {
    int i;

    switch (inst->op) {
        case XP3_RE_CHAR:
            for (i = 0; i < inst->count; i++) {
                if (inst->chars[i] == c)
                    return(1);
            }
            return(0);
        case XP3_RE_ANY:
            return(1);
        case XP3_RE_ANY_NOT_NL:
            return((c != 0xA) && (c != 0xD));
        case XP3_RE_CLASS:
            return(classTakes(regex, inst->arg, c));
        default:
            return(0);
    }
}

/* ^: the start of the subject, and with the m flag the position after
 * a newline, unless that newline ends the subject (F&O 3.1, 5.6.2) */
static int
atLineStart(const xp3Regex *regex, const xmlChar *subject, size_t len,
            size_t pos) {
    if (pos == 0)
        return(1);
    if ((!regex->multiline) || (pos == len))
        return(0);
    return(subject[pos - 1] == 0xA);
}

/* $: the end of the subject, and with the m flag the position before a
 * newline, while the end only if no newline ends the subject */
static int
atLineEnd(const xp3Regex *regex, const xmlChar *subject, size_t len,
          size_t pos) {
    if (!regex->multiline)
        return(pos == len);
    if (pos < len)
        return(subject[pos] == 0xA);
    if (len == 0)
        return(1);
    return(subject[len - 1] != 0xA);
}

/* Whether c2 is c1 or, under the i flag, a case variant of it */
static int
sameChar(const xp3Regex *regex, int c1, int c2) {
    int variants[XP3_UNICODE_VARIANTS_MAX];
    int count;
    int i;

    if (c1 == c2)
        return(1);
    if (!regex->fold)
        return(0);
    count = xp3UnicodeCaseVariants(c1, variants);
    for (i = 0; i < count; i++) {
        if (variants[i] == c2)
            return(1);
    }
    return(0);
}

/*
 * Match the text a group captured at pos, character by character, so
 * that under the i flag a case variant matches and both sides read a
 * byte that isn't UTF-8 as U+FFFD. A group that took no part matches
 * the empty string (F&O 3.1, 5.6.1.4). Returns 1 with *end set, or 0.
 */
static int
matchBackref(const xp3Regex *regex, const ptrdiff_t *slots, int group,
             const xmlChar *subject, size_t len, size_t pos, size_t *end) {
    ptrdiff_t start = slots[2 * group];
    ptrdiff_t stop = slots[2 * group + 1];
    size_t captured;

    if ((start < 0) || (stop < 0)) {
        *end = pos;
        return(1);
    }
    captured = (size_t) start;
    while (captured < (size_t) stop) {
        int c1;
        int c2;

        if (pos >= len)
            return(0);
        c1 = xp3NextChar(subject, len, &captured);
        c2 = xp3NextChar(subject, len, &pos);
        if (!sameChar(regex, c1, c2))
            return(0);
    }
    *end = pos;
    return(1);
}

/*
 * Counting work
 */

/* Count an operation, and charge a step for each
 * XP3_REGEX_OPERATIONS_PER_STEP of them. Returns 0, or -1 if the
 * caller stops the search. */
static int
countOperation(xp3RegexSteps *steps) {
    uint64_t pending;
    uint64_t due;

    if (steps == NULL)
        return(0);
    steps->operations++;
    pending = steps->operations - steps->charged;
    if (pending < XP3_REGEX_OPERATIONS_PER_STEP)
        return(0);
    due = pending / XP3_REGEX_OPERATIONS_PER_STEP;
    steps->charged += due * XP3_REGEX_OPERATIONS_PER_STEP;
    if (steps->charge == NULL)
        return(0);
    return(steps->charge(steps->data, due));
}

/*
 * The state of searches
 */

/*
 * A capture array, shared by threads of the Pike VM and counted.
 *
 * Based on struct Sub in re1's regexp.h.
 */
typedef struct _xp3RegexCaps xp3RegexCaps;
struct _xp3RegexCaps {
    int refs;
    xp3RegexCaps *nextFree;
    ptrdiff_t *slots;
};

/* Capture arrays are allocated in chunks, and the chunks are kept for
 * the next search */
typedef struct {
    xp3RegexCaps *caps;
    ptrdiff_t *slots;
    int count;
} xp3RegexCapsChunk;

/*
 * A thread of the Pike VM: where it is in the program, and its
 * captures; and a list of threads.
 *
 * Based on struct Thread and struct ThreadList in re1's pike.c.
 */
typedef struct {
    int pc;
    xp3RegexCaps *caps;
} xp3RegexThread;

typedef struct {
    xp3RegexThread *threads;
    int count;
} xp3RegexThreadList;

/*
 * What the backtracking matcher saves on its stack.
 *
 * Based on struct Thread in re1's backtrack.c; frames restore
 * captures and marks instead of holding a capture array each.
 */
typedef enum {
    XP3_RE_FRAME_THREAD = 0,    /* a branch to try: index, pos */
    XP3_RE_FRAME_SLOT,          /* a capture to restore: index, value */
    XP3_RE_FRAME_MARK           /* a mark to restore: index, pos,
                                 * generation */
} xp3RegexFrameKind;

typedef struct {
    uint8_t kind;
    int32_t index;
    size_t pos;
    ptrdiff_t value;
    uint64_t generation;
} xp3RegexFrame;

/* Where a path last passed a marked SPLIT: valid if generation is that
 * of the current attempt */
typedef struct {
    uint64_t generation;
    size_t pos;
} xp3RegexMark;

struct _xp3RegexScratch {
    const xp3Regex *regex;
    int slotCount;

    /* The Pike VM, allocated by its first search */
    xp3RegexThreadList lists[2];
    xp3RegexThread *stack;      /* for adding a thread */
    int stackCount;
    int *sparse;                /* the instructions visited at a */
    int *dense;                 /* position, a sparse set */
    int visitedCount;
    xp3RegexCapsChunk *chunks;
    int chunkCount;
    int chunkCapacity;
    int chunkInUse;             /* where unused arrays come from */
    int usedInChunk;
    xp3RegexCaps *freeCaps;

    /* The backtracking matcher, allocated by its first search */
    xp3RegexFrame *frames;
    size_t frameCount;
    size_t frameCapacity;
    ptrdiff_t *captures;
    xp3RegexMark *marks;
    uint64_t generation;        /* of the current attempt */
};

/* One search */
typedef struct {
    const xp3Regex *regex;
    xp3RegexScratch *scratch;
    const xmlChar *subject;
    size_t len;
    xp3RegexSteps *steps;
} xp3RegexRun;

/**
 * Create the state for searches with a regular expression. It belongs
 * to one thread, and can serve any number of searches with that
 * regular expression, one at a time.
 *
 * @param regex  the regular expression
 * @returns the state, to free with xp3RegexFreeScratch(), or NULL if
 *          memory allocation failed
 */
xp3RegexScratch *
xp3RegexNewScratch(const xp3Regex *regex) {
    xp3RegexScratch *scratch = xmlMalloc(sizeof(*scratch));

    if (scratch == NULL)
        return(NULL);
    memset(scratch, 0, sizeof(*scratch));
    scratch->regex = regex;
    scratch->slotCount = 2 * (regex->groupCount + 1);
    return(scratch);
}

/**
 * Free the state of searches.
 *
 * @param scratch  the state, or NULL
 */
void
xp3RegexFreeScratch(xp3RegexScratch *scratch) {
    int i;

    if (scratch == NULL)
        return;
    xmlFree(scratch->lists[0].threads);
    xmlFree(scratch->lists[1].threads);
    xmlFree(scratch->stack);
    xmlFree(scratch->sparse);
    xmlFree(scratch->dense);
    for (i = 0; i < scratch->chunkCount; i++) {
        xmlFree(scratch->chunks[i].caps);
        xmlFree(scratch->chunks[i].slots);
    }
    xmlFree(scratch->chunks);
    xmlFree(scratch->frames);
    xmlFree(scratch->captures);
    xmlFree(scratch->marks);
    xmlFree(scratch);
}

/*
 * Capture arrays of the Pike VM
 */

/* Add a chunk of capture arrays, twice as large as the one before */
static int
addCapsChunk(xp3RegexScratch *s) {
    xp3RegexCapsChunk *chunks;
    xp3RegexCapsChunk *chunk;
    int shift = s->chunkCount;
    int count;
    int i;

    if (shift > 16)
        shift = 16;
    count = 16 << shift;
    chunks = growArray(s->chunks, s->chunkCount, &s->chunkCapacity,
                       sizeof(*chunks));
    if (chunks == NULL)
        return(-1);
    s->chunks = chunks;
    chunk = &s->chunks[s->chunkCount];
    chunk->caps = xmlMalloc((size_t) count * sizeof(xp3RegexCaps));
    chunk->slots = xmlMalloc((size_t) count * (size_t) s->slotCount *
                             sizeof(ptrdiff_t));
    if ((chunk->caps == NULL) || (chunk->slots == NULL)) {
        xmlFree(chunk->caps);
        xmlFree(chunk->slots);
        return(-1);
    }
    chunk->count = count;
    for (i = 0; i < count; i++)
        chunk->caps[i].slots = chunk->slots + (size_t) i * s->slotCount;
    s->chunkCount++;
    return(0);
}

/*
 * A capture array with one reference and undefined slots, or NULL if
 * memory allocation failed.
 *
 * Based on newsub() in re1's sub.c; the free list is in the scratch,
 * not global.
 */
static xp3RegexCaps *
takeCaps(xp3RegexScratch *s) {
    xp3RegexCaps *caps;

    if (s->freeCaps != NULL) {
        caps = s->freeCaps;
        s->freeCaps = caps->nextFree;
        caps->refs = 1;
        return(caps);
    }
    if ((s->chunkInUse < s->chunkCount) &&
        (s->usedInChunk >= s->chunks[s->chunkInUse].count)) {
        s->chunkInUse++;
        s->usedInChunk = 0;
    }
    if (s->chunkInUse >= s->chunkCount) {
        if (addCapsChunk(s) < 0)
            return(NULL);
    }
    caps = &s->chunks[s->chunkInUse].caps[s->usedInChunk];
    s->usedInChunk++;
    caps->refs = 1;
    return(caps);
}

/*
 * Drop a reference to a capture array.
 *
 * Based on decref() in re1's sub.c.
 */
static void
releaseCaps(xp3RegexScratch *s, xp3RegexCaps *caps) {
    caps->refs--;
    if (caps->refs == 0) {
        caps->nextFree = s->freeCaps;
        s->freeCaps = caps;
    }
}

/*
 * Set a slot of a capture array, copying it first if other threads
 * share it. Returns the array written, or NULL if memory allocation
 * failed.
 *
 * Based on update() in re1's sub.c.
 */
static xp3RegexCaps *
updateCaps(xp3RegexScratch *s, xp3RegexCaps *caps, int slot,
           ptrdiff_t value) {
    if (caps->refs > 1) {
        xp3RegexCaps *copy = takeCaps(s);

        if (copy == NULL)
            return(NULL);
        memcpy(copy->slots, caps->slots,
               (size_t) s->slotCount * sizeof(ptrdiff_t));
        caps->refs--;
        caps = copy;
    }
    caps->slots[slot] = value;
    return(caps);
}

/* Copy the spans of the groups out of capture slots. A group with an
 * unset slot took no part. */
static void
copyGroups(const ptrdiff_t *slots, int groupCount, xp3RegexSpan *groups) {
    int i;

    if (groups == NULL)
        return;
    for (i = 0; i <= groupCount; i++) {
        ptrdiff_t start = slots[2 * i];
        ptrdiff_t end = slots[2 * i + 1];

        if ((start < 0) || (end < 0)) {
            start = XP3_REGEX_UNSET;
            end = XP3_REGEX_UNSET;
        }
        groups[i].start = start;
        groups[i].end = end;
    }
}

/*
 * The Pike VM
 */

/* Allocate an array once: if *array is still NULL. Returns 0, or -1
 * if memory allocation failed. */
static int
allocateOnce(void **array, size_t size) {
    if (*array != NULL)
        return(0);
    *array = xmlMalloc(size);
    if (*array == NULL)
        return(-1);
    /* The sparse set of the Pike VM reads entries it never wrote, and
     * the marks of the backtracking matcher must start unset */
    memset(*array, 0, size);
    return(0);
}

/* Allocate the state of the Pike VM, once per scratch. After a failed
 * allocation, the next search allocates what is missing. */
static int
preparePike(xp3RegexScratch *s) {
    size_t length = (size_t) s->regex->programLength;
    void *lists[2];
    void *stack;
    void *sparse;
    void *dense;
    int ret = 0;

    lists[0] = s->lists[0].threads;
    lists[1] = s->lists[1].threads;
    stack = s->stack;
    sparse = s->sparse;
    dense = s->dense;
    if ((allocateOnce(&lists[0], length * sizeof(xp3RegexThread)) < 0) ||
        (allocateOnce(&lists[1], length * sizeof(xp3RegexThread)) < 0) ||
        (allocateOnce(&stack, (length + 2) * sizeof(xp3RegexThread)) < 0) ||
        (allocateOnce(&sparse, length * sizeof(int)) < 0) ||
        (allocateOnce(&dense, length * sizeof(int)) < 0))
        ret = -1;
    s->lists[0].threads = lists[0];
    s->lists[1].threads = lists[1];
    s->stack = stack;
    s->sparse = sparse;
    s->dense = dense;
    return(ret);
}

static int
isVisited(const xp3RegexScratch *s, int pc) {
    int index = s->sparse[pc];

    return((index < s->visitedCount) && (s->dense[index] == pc));
}

static void
markVisited(xp3RegexScratch *s, int pc) {
    s->sparse[pc] = s->visitedCount;
    s->dense[s->visitedCount] = pc;
    s->visitedCount++;
}

static void
pushThread(xp3RegexScratch *s, int pc, xp3RegexCaps *caps) {
    s->stack[s->stackCount].pc = pc;
    s->stack[s->stackCount].caps = caps;
    s->stackCount++;
}

/*
 * Add a thread to a list at pos, following JMP, SPLIT and SAVE, and
 * the anchors, in the order a backtracker would try them, with an
 * explicit stack: the preferred branch of a SPLIT is pushed last, so
 * it comes first. An instruction already visited at this position
 * belongs to a thread of higher priority, and the thread ends there.
 * Each instruction is visited once, so the stack holds at most one
 * entry more than the instructions. Takes the reference to caps.
 *
 * Based on addthread() in re1's pike.c; an explicit stack instead of
 * recursion, a sparse set instead of the counter in the program.
 */
static int
pikeAddThread(xp3RegexRun *run, xp3RegexThreadList *list, int pc,
              xp3RegexCaps *caps, size_t pos) {
    xp3RegexScratch *s = run->scratch;
    const xp3Regex *regex = run->regex;

    s->stackCount = 0;
    pushThread(s, pc, caps);
    while (s->stackCount > 0) {
        xp3RegexThread thread;
        const xp3RegexInst *inst;
        xp3RegexCaps *updated;
        int holds;

        s->stackCount--;
        thread = s->stack[s->stackCount];
        if (countOperation(run->steps) < 0)
            return(XP3_REGEX_LIMIT);
        if (isVisited(s, thread.pc)) {
            releaseCaps(s, thread.caps);
            continue;
        }
        markVisited(s, thread.pc);
        inst = &regex->program[thread.pc];
        switch (inst->op) {
            case XP3_RE_JMP:
                pushThread(s, inst->arg, thread.caps);
                break;
            case XP3_RE_SPLIT:
                thread.caps->refs++;
                pushThread(s, inst->alt, thread.caps);
                pushThread(s, inst->arg, thread.caps);
                break;
            case XP3_RE_SAVE:
                updated = updateCaps(s, thread.caps, inst->arg,
                                     (ptrdiff_t) pos);
                if (updated == NULL)
                    return(XP3_REGEX_NO_MEMORY);
                pushThread(s, thread.pc + 1, updated);
                break;
            case XP3_RE_LINE_START:
            case XP3_RE_LINE_END:
                if (inst->op == XP3_RE_LINE_START)
                    holds = atLineStart(regex, run->subject, run->len, pos);
                else
                    holds = atLineEnd(regex, run->subject, run->len, pos);
                if (holds)
                    pushThread(s, thread.pc + 1, thread.caps);
                else
                    releaseCaps(s, thread.caps);
                break;
            default:
                /* Reads a character, or MATCH. Back-references never
                 * get here: their patterns run on the backtracking
                 * matcher. */
                list->threads[list->count] = thread;
                list->count++;
                break;
        }
    }
    return(0);
}

/*
 * Run the threads of clist on the character c at pos, adding those
 * that take it to nlist at next. A thread at MATCH replaces *matched
 * and ends the threads after it, which have lower priority.
 *
 * Based on the loop over clist in pikevm() in re1's pike.c.
 */
static int
pikeStep(xp3RegexRun *run, xp3RegexThreadList *clist,
         xp3RegexThreadList *nlist, int c, size_t next,
         xp3RegexCaps **matched) {
    xp3RegexScratch *s = run->scratch;
    int i;
    int j;

    for (i = 0; i < clist->count; i++) {
        xp3RegexThread *thread = &clist->threads[i];
        const xp3RegexInst *inst = &run->regex->program[thread->pc];
        int ret;

        if (countOperation(run->steps) < 0)
            return(XP3_REGEX_LIMIT);
        if (inst->op == XP3_RE_MATCH) {
            if (*matched != NULL)
                releaseCaps(s, *matched);
            *matched = thread->caps;
            for (j = i + 1; j < clist->count; j++)
                releaseCaps(s, clist->threads[j].caps);
            break;
        }
        if ((c < 0) || (!instTakes(run->regex, inst, c))) {
            releaseCaps(s, thread->caps);
            continue;
        }
        ret = pikeAddThread(run, nlist, thread->pc + 1, thread->caps, next);
        if (ret < 0)
            return(ret);
    }
    clist->count = 0;
    return(0);
}

/*
 * Search with the Pike VM, unanchored: until a match is found, a new
 * thread starts at each position, after the threads already there.
 *
 * Based on pikevm() in re1's pike.c; unanchored.
 */
static int
pikeSearch(xp3RegexRun *run, size_t start, xp3RegexSpan *groups) {
    xp3RegexScratch *s = run->scratch;
    xp3RegexThreadList *clist = &s->lists[0];
    xp3RegexThreadList *nlist = &s->lists[1];
    xp3RegexCaps *matched = NULL;
    size_t pos = start;
    int ret;
    int i;

    if (preparePike(s) < 0)
        return(XP3_REGEX_NO_MEMORY);
    s->chunkInUse = 0;
    s->usedInChunk = 0;
    s->freeCaps = NULL;
    s->visitedCount = 0;
    clist->count = 0;
    nlist->count = 0;

    while (1) {
        xp3RegexThreadList *swap;
        size_t next = pos;
        int c = -1;

        if (matched == NULL) {
            xp3RegexCaps *caps = takeCaps(s);

            if (caps == NULL)
                return(XP3_REGEX_NO_MEMORY);
            for (i = 0; i < s->slotCount; i++)
                caps->slots[i] = XP3_REGEX_UNSET;
            ret = pikeAddThread(run, clist, 0, caps, pos);
            if (ret < 0)
                return(ret);
        }
        if (pos < run->len)
            c = xp3NextChar(run->subject, run->len, &next);

        /* The visited set now goes with the list for the next
         * position */
        s->visitedCount = 0;
        ret = pikeStep(run, clist, nlist, c, next, &matched);
        if (ret < 0)
            return(ret);
        swap = clist;
        clist = nlist;
        nlist = swap;

        if (pos >= run->len)
            break;
        if ((matched != NULL) && (clist->count == 0))
            break;
        pos = next;
    }
    if (matched == NULL)
        return(0);
    copyGroups(matched->slots, run->regex->groupCount, groups);
    return(1);
}

/*
 * The backtracking matcher
 */

/* Allocate the state of the backtracking matcher, once per scratch */
static int
prepareBacktrack(xp3RegexScratch *s) {
    size_t markCount = (size_t) s->regex->markCount;
    void *captures = s->captures;
    void *marks = s->marks;
    int ret = 0;

    if (allocateOnce(&captures,
                     (size_t) s->slotCount * sizeof(ptrdiff_t)) < 0)
        ret = -1;
    if ((markCount > 0) &&
        (allocateOnce(&marks, markCount * sizeof(xp3RegexMark)) < 0))
        ret = -1;
    s->captures = captures;
    s->marks = marks;
    return(ret);
}

/*
 * Push a frame on the stack, which grows as needed.
 *
 * Based on backtrack() in re1's backtrack.c; the stack grows instead
 * of being fixed.
 */
static int
pushFrame(xp3RegexScratch *s, xp3RegexFrameKind kind, int index,
          size_t pos, ptrdiff_t value, uint64_t generation) {
    xp3RegexFrame *frame;

    if (s->frameCount >= s->frameCapacity) {
        size_t capacity = 64;
        xp3RegexFrame *frames;

        if (s->frameCapacity > 0)
            capacity = s->frameCapacity * 2;
        if (capacity > SIZE_MAX / sizeof(*frames))
            return(XP3_REGEX_NO_MEMORY);
        frames = xmlRealloc(s->frames, capacity * sizeof(*frames));
        if (frames == NULL)
            return(XP3_REGEX_NO_MEMORY);
        s->frames = frames;
        s->frameCapacity = capacity;
    }
    frame = &s->frames[s->frameCount];
    frame->kind = (uint8_t) kind;
    frame->index = index;
    frame->pos = pos;
    frame->value = value;
    frame->generation = generation;
    s->frameCount++;
    return(0);
}

/*
 * Pass a SPLIT: fail if this path passed it at the same position
 * without reading a character since, which only a loop whose body can
 * match the empty string allows, and otherwise remember the position
 * until the path backtracks over it. Returns 1 to go on, 0 to fail, or
 * an error.
 */
static int
passMark(xp3RegexScratch *s, const xp3RegexInst *inst, size_t pos) {
    xp3RegexMark *mark;
    int ret;

    if (inst->mark < 0)
        return(1);
    mark = &s->marks[inst->mark];
    if ((mark->generation == s->generation) && (mark->pos == pos))
        return(0);
    ret = pushFrame(s, XP3_RE_FRAME_MARK, inst->mark, mark->pos, 0,
                    mark->generation);
    if (ret < 0)
        return(ret);
    mark->generation = s->generation;
    mark->pos = pos;
    return(1);
}

/*
 * Run one thread from pc at pos until it fails or matches: a SPLIT
 * saves its second branch, a SAVE the value it overwrites. Returns 1
 * for a match, 0 if the thread fails, or an error.
 *
 * Based on the inner loop of backtrack() in re1's backtrack.c;
 * captures are restored from the stack instead of copied.
 */
static int
backtrackThread(xp3RegexRun *run, int pc, size_t pos,
                xp3RegexSpan *groups) {
    xp3RegexScratch *s = run->scratch;
    const xp3Regex *regex = run->regex;
    int ret;

    while (1) {
        const xp3RegexInst *inst = &regex->program[pc];
        size_t next = pos;
        int c;

        if (countOperation(run->steps) < 0)
            return(XP3_REGEX_LIMIT);
        switch (inst->op) {
            case XP3_RE_CHAR:
            case XP3_RE_ANY:
            case XP3_RE_ANY_NOT_NL:
            case XP3_RE_CLASS:
                if (pos >= run->len)
                    return(0);
                c = xp3NextChar(run->subject, run->len, &next);
                if (!instTakes(regex, inst, c))
                    return(0);
                pos = next;
                pc++;
                break;
            case XP3_RE_BACKREF:
                if (!matchBackref(regex, s->captures, inst->arg,
                                  run->subject, run->len, pos, &next))
                    return(0);
                pos = next;
                pc++;
                break;
            case XP3_RE_LINE_START:
                if (!atLineStart(regex, run->subject, run->len, pos))
                    return(0);
                pc++;
                break;
            case XP3_RE_LINE_END:
                if (!atLineEnd(regex, run->subject, run->len, pos))
                    return(0);
                pc++;
                break;
            case XP3_RE_JMP:
                pc = inst->arg;
                break;
            case XP3_RE_SPLIT:
                ret = passMark(s, inst, pos);
                if (ret <= 0)
                    return(ret);
                ret = pushFrame(s, XP3_RE_FRAME_THREAD, inst->alt, pos, 0,
                                0);
                if (ret < 0)
                    return(ret);
                pc = inst->arg;
                break;
            case XP3_RE_SAVE:
                ret = pushFrame(s, XP3_RE_FRAME_SLOT, inst->arg, 0,
                                s->captures[inst->arg], 0);
                if (ret < 0)
                    return(ret);
                s->captures[inst->arg] = (ptrdiff_t) pos;
                pc++;
                break;
            case XP3_RE_MATCH:
                copyGroups(s->captures, regex->groupCount, groups);
                return(1);
            default:
                return(0);
        }
    }
}

/*
 * Try to match at one start position, running saved threads in stack
 * order and restoring what they changed as the stack unwinds.
 *
 * Based on the outer loop of backtrack() in re1's backtrack.c.
 */
static int
backtrackAt(xp3RegexRun *run, size_t start, xp3RegexSpan *groups) {
    xp3RegexScratch *s = run->scratch;
    int ret;
    int i;

    s->generation++;
    s->frameCount = 0;
    for (i = 0; i < s->slotCount; i++)
        s->captures[i] = XP3_REGEX_UNSET;
    ret = pushFrame(s, XP3_RE_FRAME_THREAD, 0, start, 0, 0);
    if (ret < 0)
        return(ret);

    while (s->frameCount > 0) {
        xp3RegexFrame frame;

        s->frameCount--;
        frame = s->frames[s->frameCount];
        if (countOperation(run->steps) < 0)
            return(XP3_REGEX_LIMIT);
        if (frame.kind == XP3_RE_FRAME_SLOT) {
            s->captures[frame.index] = frame.value;
        } else if (frame.kind == XP3_RE_FRAME_MARK) {
            s->marks[frame.index].generation = frame.generation;
            s->marks[frame.index].pos = frame.pos;
        } else {
            ret = backtrackThread(run, frame.index, frame.pos, groups);
            if (ret != 0)
                return(ret);
        }
    }
    return(0);
}

/* Search with the backtracking matcher: each start position in turn,
 * so that the leftmost match wins */
static int
backtrackSearch(xp3RegexRun *run, size_t start, xp3RegexSpan *groups) {
    size_t pos = start;

    if (prepareBacktrack(run->scratch) < 0)
        return(XP3_REGEX_NO_MEMORY);
    while (1) {
        int ret = backtrackAt(run, pos, groups);

        if (ret != 0)
            return(ret);
        if (pos >= run->len)
            return(0);
        xp3NextChar(run->subject, run->len, &pos);
    }
}

/*
 * Searching
 */

static int
search(const xp3Regex *regex, xp3RegexScratch *scratch,
       const xmlChar *subject, size_t len, size_t start,
       xp3RegexSpan *groups, xp3RegexSteps *steps, int backtrack) {
    xp3RegexScratch *own = NULL;
    xp3RegexRun run;
    int ret;

    if (start > len)
        return(0);
    if (scratch == NULL) {
        own = xp3RegexNewScratch(regex);
        if (own == NULL)
            return(XP3_REGEX_NO_MEMORY);
        scratch = own;
    }
    run.regex = regex;
    run.scratch = scratch;
    run.subject = subject;
    run.len = len;
    run.steps = steps;
    if (backtrack || regex->backrefs)
        ret = backtrackSearch(&run, start, groups);
    else
        ret = pikeSearch(&run, start, groups);
    xp3RegexFreeScratch(own);
    return(ret);
}

/**
 * Find the leftmost match of a regular expression that starts at or
 * after an offset of a subject; at one start position, the branches
 * and quantifiers decide by their priority (F&O 3.1, 5.6.4). Patterns
 * with back-references run on the backtracking matcher, all others on
 * the Pike VM.
 *
 * @param regex    the regular expression
 * @param scratch  the state from xp3RegexNewScratch() for this regular
 *                 expression, or NULL to allocate one for this search
 * @param subject  the subject, UTF-8
 * @param len      its length in bytes
 * @param start    the byte offset where the search starts, at most len;
 *                 ^ still matches only at offset 0, or after a newline
 * @param groups   room for xp3RegexGroupCount() + 1 spans, or NULL: set
 *                 for a match, group 0 being the whole match
 * @param steps    counts the work and charges it, or NULL
 * @returns 1 for a match, 0 for none, XP3_REGEX_NO_MEMORY if memory
 *          allocation failed, or XP3_REGEX_LIMIT if the charge function
 *          stopped the search
 */
int
xp3RegexSearch(const xp3Regex *regex, xp3RegexScratch *scratch,
               const xmlChar *subject, size_t len, size_t start,
               xp3RegexSpan *groups, xp3RegexSteps *steps) {
    return(search(regex, scratch, subject, len, start, groups, steps, 0));
}

/**
 * Search as xp3RegexSearch() does, but always with the backtracking
 * matcher, which finds the same match. For tests and the fuzzer, which
 * compare the two matchers.
 *
 * @param regex    the regular expression
 * @param scratch  as for xp3RegexSearch()
 * @param subject  the subject, UTF-8
 * @param len      its length in bytes
 * @param start    the byte offset where the search starts
 * @param groups   room for the spans, or NULL
 * @param steps    counts the work and charges it, or NULL
 * @returns as xp3RegexSearch()
 */
int
xp3RegexSearchBacktracking(const xp3Regex *regex, xp3RegexScratch *scratch,
                           const xmlChar *subject, size_t len, size_t start,
                           xp3RegexSpan *groups, xp3RegexSteps *steps) {
    return(search(regex, scratch, subject, len, start, groups, steps, 1));
}
