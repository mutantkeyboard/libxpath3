#!/usr/bin/env python3
#
# check-style.py: check the mechanical parts of the coding style
#
# Usage: python3 .gitlab-ci/check-style.py [file...]
#
# Without arguments, checks every C source and header that git knows
# about, including new files that are not committed yet. The style is
# libxml2's, which no formatter reproduces (clang-format cannot keep
# "return(x)", for example), so only these rules are checked:
#
# - lines are at most 79 columns
# - no tabs, except in the public header, where the function names
#   below XP3PUBFUN are tab-aligned as in libxml2's headers
# - no trailing whitespace
# - no // comments
# - return(x) or return;, not return x or return (x)
#
# Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
# See COPYING for the status of this software.
#
# Author: Antonio Nešić <nesic.antonio@gmail.com>

import re
import subprocess
import sys

MAX_COLUMNS = 79
TABS_ALLOWED = {'include/libxpath3/xpath3.h'}
PATTERNS = ('*.c', '*.h', '*.h.in', '*.inc')


def source_files():
    out = subprocess.run(['git', 'ls-files', '--cached', '--others',
                          '--exclude-standard', '--'] + list(PATTERNS),
                         capture_output=True, text=True, check=True).stdout
    return sorted(set(out.split()))


def code_only(text):
    """The text with comments and string and character literals replaced
    by spaces, keeping line breaks, so that later checks see only code."""
    pieces = []
    position = 0
    length = len(text)
    while position < length:
        char = text[position]
        if text.startswith('/*', position):
            end = text.find('*/', position + 2)
            end = length if end < 0 else end + 2
            pieces.append(re.sub(r'[^\n]', ' ', text[position:end]))
            position = end
        elif text.startswith('//', position):
            # Kept, so the // check can report it
            end = text.find('\n', position)
            end = length if end < 0 else end
            pieces.append(text[position:end])
            position = end
        elif char in '"\'':
            quote = char
            end = position + 1
            while (end < length and text[end] != quote and
                   text[end] != '\n'):
                end += 2 if text[end] == '\\' else 1
            end = min(end + 1, length)
            literal_length = end - position
            if literal_length >= 2:
                pieces.append(quote + ' ' * (literal_length - 2) + quote)
            else:
                pieces.append(quote)
            position = end
        else:
            pieces.append(char)
            position += 1
    return ''.join(pieces)


def check(path):
    problems = []
    with open(path, encoding='utf-8') as source:
        text = source.read()
    lines = text.split('\n')
    code = code_only(text).split('\n')

    for number, line in enumerate(lines, 1):
        if len(line) > MAX_COLUMNS:
            problems.append((number, 'longer than %d columns' % MAX_COLUMNS))
        if '\t' in line and path not in TABS_ALLOWED:
            problems.append((number, 'tab'))
        if line != line.rstrip():
            problems.append((number, 'trailing whitespace'))

    for number, line in enumerate(code, 1):
        if '//' in line:
            problems.append((number, '// comment, use /* */'))
        if re.search(r'\breturn\s+\(', line):
            problems.append((number, 'return (x), write return(x)'))
        elif re.search(r'\breturn\s+[^\s(;]', line):
            problems.append((number, 'return x, write return(x)'))

    return ['%s:%d: %s' % (path, number, what)
            for number, what in sorted(problems)]


def main():
    files = sys.argv[1:] or source_files()
    problems = []
    for path in files:
        problems.extend(check(path))
    for problem in problems:
        print(problem)
    if problems:
        print('%d style problem%s' % (len(problems),
                                      '' if len(problems) == 1 else 's'))
        return 1
    print('style: %d files checked, no problems' % len(files))
    return 0


sys.exit(main())
