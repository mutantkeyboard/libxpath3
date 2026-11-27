/*
 * runexample.c: test driver for the tutorial programs
 *
 *     runexample output PROGRAM DATA-DIRECTORY EXPECTED-FILE
 *
 * runs PROGRAM with DATA-DIRECTORY as its argument, and passes if it
 * exits with status 0 and its standard output equals EXPECTED-FILE
 * byte for byte, apart from line ends: CR LF counts as LF on both
 * sides, so that Windows' text mode and a checkout with CR LF line
 * ends compare equal. On a difference it prints the first differing
 * line and the whole output to stderr.
 *
 *     runexample excerpts PAGE SOURCE...
 *
 * passes if every C code block of the Markdown PAGE (```c ... ```) is
 * a contiguous run of whole lines of one of the SOURCE files, so that
 * the pages never show code that the tested programs don't contain.
 *
 * The driver is C rather than a script so that Meson and CMake can run
 * it on every platform without another dependency.
 *
 * Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
 * See COPYING for the status of this software.
 *
 * Author: Antonio Nešić <nesic.antonio@gmail.com>
 */

/* popen() and pclose() are POSIX, not C99 */
#ifndef _WIN32
  #define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #define popen _popen
  #define pclose _pclose
  /* Binary mode keeps the CR LF line ends, which are compared as LF */
  #define POPEN_READ "rb"
#else
  #include <sys/wait.h>
  #define POPEN_READ "r"
#endif

/* A text with its lines, CR LF read as LF */
typedef struct {
    char *data;
    size_t length;
    char **lines;
    size_t lineCount;
} Text;

/*
 * Read a whole stream, dropping each CR that comes before an LF.
 * Returns 0, or -1 if memory allocation failed.
 */
static int
readStream(FILE *stream, Text *text) {
    size_t size = 4096;
    size_t length = 0;
    char *data;
    int c;

    memset(text, 0, sizeof(*text));
    data = malloc(size);
    if (data == NULL)
        return(-1);
    while ((c = getc(stream)) != EOF) {
        if (length + 2 > size) {
            char *bigger = realloc(data, size * 2);

            if (bigger == NULL) {
                free(data);
                return(-1);
            }
            data = bigger;
            size *= 2;
        }
        if ((c == '\n') && (length > 0) && (data[length - 1] == '\r'))
            length--;
        data[length++] = (char) c;
    }
    data[length] = 0;
    text->data = data;
    text->length = length;
    return(0);
}

/*
 * Split a text into lines, in place: each LF becomes a NUL. A last
 * line without LF counts as a line too. Returns 0, or -1 if memory
 * allocation failed.
 */
static int
splitLines(Text *text) {
    size_t count = 0;
    size_t i;
    char *start;

    for (i = 0; i < text->length; i++) {
        if (text->data[i] == '\n')
            count++;
    }
    if ((text->length > 0) && (text->data[text->length - 1] != '\n'))
        count++;

    text->lines = malloc((count + 1) * sizeof(char *));
    if (text->lines == NULL)
        return(-1);
    start = text->data;
    for (i = 0; i < text->length; i++) {
        if (text->data[i] == '\n') {
            text->data[i] = 0;
            text->lines[text->lineCount++] = start;
            start = text->data + i + 1;
        }
    }
    if (text->lineCount < count)
        text->lines[text->lineCount++] = start;
    return(0);
}

static void
freeText(Text *text) {
    free(text->lines);
    free(text->data);
}

/*
 * Read a file and split it into lines. Returns 0, or -1 after
 * reporting the problem.
 */
static int
readFile(const char *path, Text *text) {
    FILE *file = fopen(path, "rb");
    int ret;

    if (file == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        return(-1);
    }
    ret = readStream(file, text);
    fclose(file);
    if ((ret < 0) || (splitLines(text) < 0)) {
        fprintf(stderr, "out of memory reading %s\n", path);
        freeText(text);
        return(-1);
    }
    return(0);
}

/*
 * Append an argument to a command line for popen(), quoted for the
 * shell that runs it: /bin/sh on POSIX systems, cmd.exe on Windows,
 * where a path can't contain a double quote. Returns the new end.
 */
static char *
appendQuoted(char *end, const char *arg) {
#ifdef _WIN32
    *end++ = '"';
    while (*arg != 0)
        *end++ = *arg++;
    *end++ = '"';
#else
    *end++ = '\'';
    while (*arg != 0) {
        if (*arg == '\'') {
            memcpy(end, "'\\''", 4);
            end += 4;
        } else {
            *end++ = *arg;
        }
        arg++;
    }
    *end++ = '\'';
#endif
    return(end);
}

/*
 * Run the program and read its standard output. Returns its exit
 * status, or -1 if it couldn't be run.
 */
static int
runProgram(const char *program, const char *dataDir, Text *output) {
    size_t size = 4 * (strlen(program) + strlen(dataDir)) + 16;
    char *command = malloc(size);
    char *end;
    FILE *child;
    int status;

    if (command == NULL)
        return(-1);
    end = command;
#ifdef _WIN32
    /* cmd.exe /c removes the outermost quotes, so add a pair */
    *end++ = '"';
#endif
    end = appendQuoted(end, program);
    *end++ = ' ';
    end = appendQuoted(end, dataDir);
#ifdef _WIN32
    *end++ = '"';
#endif
    *end = 0;

    fflush(NULL);
    child = popen(command, POPEN_READ);
    free(command);
    if (child == NULL) {
        fprintf(stderr, "cannot run %s\n", program);
        return(-1);
    }
    if ((readStream(child, output) < 0) || (splitLines(output) < 0)) {
        fprintf(stderr, "out of memory reading the output\n");
        pclose(child);
        freeText(output);
        return(-1);
    }
    status = pclose(child);
#ifndef _WIN32
    if ((status != -1) && WIFEXITED(status))
        status = WEXITSTATUS(status);
    else
        status = -1;
#endif
    return(status);
}

static int
checkOutput(const char *program, const char *dataDir, const char *expected) {
    Text want;
    Text got;
    size_t i;
    int status;
    int ret = 0;

    if (readFile(expected, &want) < 0)
        return(1);
    status = runProgram(program, dataDir, &got);
    if (status < 0) {
        freeText(&want);
        return(1);
    }
    if (status != 0) {
        fprintf(stderr, "%s exited with status %d\n", program, status);
        ret = 1;
    }

    if ((got.length != want.length) ||
        (memcmp(got.data, want.data, got.length) != 0)) {
        for (i = 0; (i < got.lineCount) && (i < want.lineCount); i++) {
            if (strcmp(got.lines[i], want.lines[i]) != 0)
                break;
        }
        fprintf(stderr, "output differs from %s at line %lu\n", expected,
                (unsigned long) i + 1);
        if (i < want.lineCount)
            fprintf(stderr, "expected: %s\n", want.lines[i]);
        else
            fprintf(stderr, "expected: (end of file)\n");
        if (i < got.lineCount)
            fprintf(stderr, "actual:   %s\n", got.lines[i]);
        else
            fprintf(stderr, "actual:   (end of output)\n");
        fprintf(stderr, "--- whole output ---\n");
        for (i = 0; i < got.lineCount; i++)
            fprintf(stderr, "%s\n", got.lines[i]);
        ret = 1;
    }

    freeText(&got);
    freeText(&want);
    return(ret);
}

/*
 * Check whether lines first to first + count - 1 of the page occur as
 * a contiguous run in a source.
 */
static int
isExcerpt(const Text *page, size_t first, size_t count, const Text *source) {
    size_t start;
    size_t i;

    if (count > source->lineCount)
        return(0);
    for (start = 0; start + count <= source->lineCount; start++) {
        for (i = 0; i < count; i++) {
            if (strcmp(page->lines[first + i],
                       source->lines[start + i]) != 0)
                break;
        }
        if (i == count)
            return(1);
    }
    return(0);
}

static int
checkExcerpts(const char *pagePath, char **sourcePaths, int sourceCount) {
    Text page;
    Text *sources;
    size_t line;
    int blocks = 0;
    int ret = 0;
    int loaded = 0;
    int i;

    if (readFile(pagePath, &page) < 0)
        return(1);
    sources = calloc((size_t) sourceCount, sizeof(Text));
    if (sources == NULL) {
        freeText(&page);
        return(1);
    }
    for (loaded = 0; loaded < sourceCount; loaded++) {
        if (readFile(sourcePaths[loaded], &sources[loaded]) < 0) {
            ret = 1;
            goto done;
        }
    }

    line = 0;
    while (line < page.lineCount) {
        size_t first;
        size_t count;
        int found = 0;

        if (strcmp(page.lines[line], "```c") != 0) {
            line++;
            continue;
        }
        first = line + 1;
        count = 0;
        while ((first + count < page.lineCount) &&
               (strcmp(page.lines[first + count], "```") != 0))
            count++;
        if (first + count == page.lineCount) {
            fprintf(stderr, "%s:%lu: code block without its end\n",
                    pagePath, (unsigned long) line + 1);
            ret = 1;
            break;
        }
        blocks++;
        for (i = 0; (i < sourceCount) && (!found); i++)
            found = (count > 0) && isExcerpt(&page, first, count, &sources[i]);
        if (!found) {
            fprintf(stderr, "%s:%lu: code block is not part of %s\n",
                    pagePath, (unsigned long) line + 1, sourcePaths[0]);
            ret = 1;
        }
        line = first + count + 1;
    }
    if ((ret == 0) && (blocks == 0)) {
        fprintf(stderr, "%s has no C code blocks\n", pagePath);
        ret = 1;
    }

done:
    for (i = 0; i < loaded; i++)
        freeText(&sources[i]);
    free(sources);
    freeText(&page);
    return(ret);
}

int
main(int argc, char **argv) {
    if ((argc == 5) && (strcmp(argv[1], "output") == 0))
        return(checkOutput(argv[2], argv[3], argv[4]));
    if ((argc >= 4) && (strcmp(argv[1], "excerpts") == 0))
        return(checkExcerpts(argv[2], argv + 3, argc - 3));

    fprintf(stderr,
            "usage: %s output PROGRAM DATA-DIRECTORY EXPECTED-FILE\n"
            "       %s excerpts PAGE SOURCE...\n", argv[0], argv[0]);
    return(2);
}
