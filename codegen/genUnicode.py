#!/usr/bin/env python3
#
# genUnicode.py: generate codegen/unicode.inc, the Unicode tables, from
# the Unicode Character Database
#
# Usage, to regenerate the tables:
#
#   python3 codegen/genUnicode.py [--download] UCD_DIR > codegen/unicode.inc
#
# UCD_DIR  a directory with these files of the Unicode Character
#          Database, version UNICODE_VERSION below:
#            UnicodeData.txt
#            Blocks.txt
#            SpecialCasing.txt
#            DerivedNormalizationProps.txt
#          With --download, the files missing from UCD_DIR are fetched
#          first from https://www.unicode.org/Public/18.0.0/ucd/, as
#          these commands would:
#            mkdir -p UCD_DIR && cd UCD_DIR
#            for name in UnicodeData Blocks SpecialCasing \
#                DerivedNormalizationProps; do
#              curl -O https://www.unicode.org/Public/18.0.0/ucd/$name.txt
#            done
#
# The data files are not committed, and neither is a cache of them;
# the output is, so building needs no Python.
#
# The output has these tables, each sorted for a binary search:
#
# - the general category of every assigned code point, as ranges of
#   code points with the same category; unassigned code points, which
#   no range covers, are Cn
# - the blocks, by the name XSD regular expressions use in \p{IsX}:
#   the name of Blocks.txt without its spaces, so "Latin-1 Supplement"
#   is Latin-1Supplement, with the range of each
# - the simple case mappings of UnicodeData.txt, as ranges of code
#   points whose mapping adds the same number, either to each code
#   point of the range or to every second one, as in Latin
#   Extended-A, where capital and small letters alternate
# - the full case mappings of SpecialCasing.txt that differ from the
#   simple ones, such as U+00DF to "SS" in upper case. Only the
#   unconditional ones: F&O 3.1, 5.4.7 and 5.4.8 ask for the mappings
#   without tailoring, and the conditions are languages and contexts.
# - the case variants of each code point for the i flag of regular
#   expressions (F&O 3.1, 5.6.2): C2 is one of C1 if lower-case(C1) eq
#   lower-case(C2) or upper-case(C1) eq upper-case(C2), with the full
#   mappings, so U+212A KELVIN SIGN is one of k and K. For all but
#   about a hundred code points, the variants are just the simple
#   upper-case and lower-case mappings, so only the others are listed,
#   with their variants.
# - the canonical combining classes other than 0, as ranges
# - the canonical and the compatibility decompositions, each expanded
#   fully, so that one lookup gives the whole decomposition of a code
#   point; the compatibility table lists only those that differ from
#   the canonical one
# - the canonical composition pairs, from the canonical decompositions
#   of two code points, without those that DerivedNormalizationProps.txt
#   lists as Full_Composition_Exclusion
# - the starters that compose with a starter before them, which a
#   normalizer must not take as the start of a new segment
#
# Hangul syllables are composed and decomposed by the arithmetic of
# The Unicode Standard, 3.12, in xp3unicode.c, and are in no table but
# the categories.
#
# The layout of the range tables, sorted ranges found by binary search,
# follows libxml2's codegen/genUnicode.py, which generates the Unicode
# tables of libxml2's regular expressions:
#
#   Copyright (C) 1998-2012 Daniel Veillard
#   Copyright (C) The Libxml2 Contributors
#   MIT License, https://gitlab.gnome.org/GNOME/libxml2
#
# That generator targets Unicode 4.0.1 and reads the ranges that
# UnicodeData.txt gives as a <..., First> and a <..., Last> line as two
# code points, so its categories miss everything between them, such
# as most CJK ideographs and Hangul syllables. Here those ranges are
# expanded.
#
# The output is derived from the Unicode Character Database, so it
# carries the Unicode License v3 (UNICODE_LICENSE below, from
# https://www.unicode.org/license.txt).
#
# Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
# See COPYING for the status of this software.
#
# Author: Antonio Nešić <nesic.antonio@gmail.com>

import collections
import os
import sys
import urllib.request

UNICODE_VERSION = '18.0.0'
UCD_URL = 'https://www.unicode.org/Public/%s/ucd/' % UNICODE_VERSION
UCD_FILES = ['UnicodeData.txt', 'Blocks.txt', 'SpecialCasing.txt',
             'DerivedNormalizationProps.txt']

UNICODE_LICENSE = '''\
UNICODE LICENSE V3

COPYRIGHT AND PERMISSION NOTICE

Copyright © 1991-2026 Unicode, Inc.

NOTICE TO USER: Carefully read the following legal agreement. BY
DOWNLOADING, INSTALLING, COPYING OR OTHERWISE USING DATA FILES, AND/OR
SOFTWARE, YOU UNEQUIVOCALLY ACCEPT, AND AGREE TO BE BOUND BY, ALL OF THE
TERMS AND CONDITIONS OF THIS AGREEMENT. IF YOU DO NOT AGREE, DO NOT
DOWNLOAD, INSTALL, COPY, DISTRIBUTE OR USE THE DATA FILES OR SOFTWARE.

Permission is hereby granted, free of charge, to any person obtaining a
copy of data files and any associated documentation (the "Data Files") or
software and any associated documentation (the "Software") to deal in the
Data Files or Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, and/or sell
copies of the Data Files or Software, and to permit persons to whom the
Data Files or Software are furnished to do so, provided that either (a)
this copyright and permission notice appear with all copies of the Data
Files or Software, or (b) this copyright and permission notice appear in
associated Documentation.

THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
THIRD PARTY RIGHTS.

IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS NOTICE
BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES,
OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THE DATA
FILES OR SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall
not be used in advertising or otherwise to promote the sale, use or other
dealings in these Data Files or Software without prior written
authorization of the copyright holder.'''

# The general categories, as xp3UnicodeCategory in
# include/private/xp3unicode.h has them
CATEGORIES = ['Lu', 'Ll', 'Lt', 'Lm', 'Lo', 'Mn', 'Mc', 'Me', 'Nd', 'Nl',
              'No', 'Pc', 'Pd', 'Ps', 'Pe', 'Pi', 'Pf', 'Po', 'Sm', 'Sc',
              'Sk', 'So', 'Zs', 'Zl', 'Zp', 'Cc', 'Cf', 'Cs', 'Co', 'Cn']

# Hangul syllables, The Unicode Standard, 3.12
HANGUL_S_BASE = 0xAC00
HANGUL_L_BASE = 0x1100
HANGUL_V_BASE = 0x1161
HANGUL_T_BASE = 0x11A7
HANGUL_V_COUNT = 21
HANGUL_T_COUNT = 28
HANGUL_N_COUNT = HANGUL_V_COUNT * HANGUL_T_COUNT
HANGUL_S_COUNT = 11172

# One code point of UnicodeData.txt. decomposition is a list of code
# points, tag the <...> of a compatibility decomposition or None, and
# upper and lower the simple case mappings, or None for none.
CodePoint = collections.namedtuple(
    'CodePoint', 'category combining tag decomposition upper lower')

# A run of code points with one property value
Range = collections.namedtuple('Range', 'first last value')

# A run of code points mapped by adding delta, to each stride-th one
DeltaRange = collections.namedtuple('DeltaRange', 'first last delta stride')

Block = collections.namedtuple('Block', 'name first last')

# A code point with a list in one of the pools
PoolEntry = collections.namedtuple('PoolEntry', 'code start length')

Composition = collections.namedtuple('Composition', 'first second composite')


def download(directory):
    """Fetch the data files that are missing from the directory."""
    os.makedirs(directory, exist_ok=True)
    for name in UCD_FILES:
        path = os.path.join(directory, name)
        if os.path.exists(path):
            continue
        sys.stderr.write('downloading %s%s\n' % (UCD_URL, name))
        with urllib.request.urlopen(UCD_URL + name) as response:
            data = response.read()
        with open(path, 'wb') as output:
            output.write(data)


def data_lines(directory, name):
    """The fields of each data line of a UCD file, without comments.
    Files that name their version in the first line must name
    UNICODE_VERSION."""
    path = os.path.join(directory, name)
    with open(path, encoding='utf-8') as source:
        lines = source.read().split('\n')
    if name != 'UnicodeData.txt':
        expected = '# %s-%s.txt' % (name[:-4], UNICODE_VERSION)
        if lines[0].strip() != expected:
            sys.exit('%s is not version %s: %s' % (path, UNICODE_VERSION,
                                                    lines[0]))
    result = []
    for line in lines:
        content = line.split('#', 1)[0].strip()
        if content:
            result.append([field.strip() for field in content.split(';')])
    return result


def code_range(text):
    """The first and last code point of "XXXX" or "XXXX..YYYY"."""
    first, separator, last = text.partition('..')
    if not separator:
        last = first
    return int(first, 16), int(last, 16)


def code_list(text):
    return [int(code, 16) for code in text.split()]


def read_unicode_data(directory):
    """Every assigned code point, with the ranges of <..., First> and
    <..., Last> lines expanded."""
    points = {}
    range_start = None
    for fields in data_lines(directory, 'UnicodeData.txt'):
        code = int(fields[0], 16)
        name = fields[1]
        tag = None
        decomposition = []
        if fields[5]:
            parts = fields[5].split()
            if parts[0].startswith('<'):
                tag = parts[0]
                parts = parts[1:]
            decomposition = [int(part, 16) for part in parts]
        upper = None
        if fields[12]:
            upper = int(fields[12], 16)
        lower = None
        if fields[13]:
            lower = int(fields[13], 16)
        if fields[2] not in CATEGORIES:
            sys.exit('U+%04X has the unknown category %s' % (code, fields[2]))
        point = CodePoint(fields[2], int(fields[3]), tag, decomposition,
                          upper, lower)
        if name.endswith(', First>'):
            range_start = code
            continue
        if name.endswith(', Last>'):
            if range_start is None:
                sys.exit('%s without a First line' % name)
            # The code points of a range share everything but the name
            for member in range(range_start, code + 1):
                points[member] = point
            range_start = None
            continue
        points[code] = point
    return points


def read_blocks(directory):
    blocks = []
    for fields in data_lines(directory, 'Blocks.txt'):
        first, last = code_range(fields[0])
        blocks.append(Block(fields[1].replace(' ', ''), first, last))
    blocks.sort(key=lambda block: block.name.encode('ascii'))
    for previous, block in zip(blocks, blocks[1:]):
        if previous.name == block.name:
            sys.exit('two blocks named %s' % block.name)
    return blocks


def read_special_casing(directory):
    """The unconditional full mappings: code point to (lower, upper)."""
    special = {}
    for fields in data_lines(directory, 'SpecialCasing.txt'):
        # code; lower; title; upper; [conditions;]
        if len(fields) > 4 and fields[4]:
            continue
        special[int(fields[0], 16)] = (code_list(fields[1]),
                                       code_list(fields[3]))
    return special


def read_composition_exclusions(directory):
    excluded = set()
    for fields in data_lines(directory, 'DerivedNormalizationProps.txt'):
        if fields[1] == 'Full_Composition_Exclusion':
            first, last = code_range(fields[0])
            excluded.update(range(first, last + 1))
    return excluded


def is_hangul_syllable(code):
    return HANGUL_S_BASE <= code < HANGUL_S_BASE + HANGUL_S_COUNT


def hangul_decomposition(code):
    index = code - HANGUL_S_BASE
    leading = HANGUL_L_BASE + index // HANGUL_N_COUNT
    vowel = HANGUL_V_BASE + (index % HANGUL_N_COUNT) // HANGUL_T_COUNT
    trailing = HANGUL_T_BASE + index % HANGUL_T_COUNT
    if trailing == HANGUL_T_BASE:
        return [leading, vowel]
    return [leading, vowel, trailing]


def full_decomposition(points, code, compatibility):
    """The decomposition of a code point applied until nothing changes,
    canonical only or with the compatibility mappings too."""
    if is_hangul_syllable(code):
        return hangul_decomposition(code)
    point = points.get(code)
    if point is None or not point.decomposition:
        return [code]
    if point.tag is not None and not compatibility:
        return [code]
    result = []
    for part in point.decomposition:
        result.extend(full_decomposition(points, part, compatibility))
    return result


def ranges_of(values):
    """Runs of consecutive code points with the same value, from a dict
    of code point to value."""
    result = []
    for code in sorted(values):
        value = values[code]
        if result and result[-1].last == code - 1 and \
                result[-1].value == value:
            result[-1] = result[-1]._replace(last=code)
        else:
            result.append(Range(code, code, value))
    return result


def run_end(mapping, first, delta, stride):
    """The last code point of a run from first whose mappings add delta,
    to each stride-th code point. A run of every second code point
    stops before one in between that has a mapping, so that runs never
    overlap."""
    last = first
    while True:
        following = last + stride
        if mapping.get(following) != following + delta:
            return last
        if stride == 2 and (last + 1) in mapping:
            return last
        last = following


def delta_ranges(mapping):
    """The mappings of a dict, code point to code point, as runs with
    the same delta, of consecutive code points or of every second one,
    whichever is longer. The runs are sorted and don't overlap, so a
    lookup takes the last one starting at or before a code point."""
    result = []
    covered = set()
    for first in sorted(mapping):
        if first in covered:
            continue
        delta = mapping[first] - first
        best = DeltaRange(first, first, delta, 1)
        for stride in (1, 2):
            last = run_end(mapping, first, delta, stride)
            if last > best.last:
                best = DeltaRange(first, last, delta, stride)
        result.append(best)
        covered.update(range(best.first, best.last + 1, best.stride))
    return result


def pooled(*tables):
    """For dicts of code point to a list of code points, the entries of
    each, pointing into one pool where equal lists are shared, and the
    pool."""
    pool = []
    starts = {}
    entry_lists = []
    for table in tables:
        entries = []
        for code in sorted(table):
            key = tuple(table[code])
            if key not in starts:
                starts[key] = len(pool)
                pool.extend(key)
            entries.append(PoolEntry(code, starts[key], len(key)))
        entry_lists.append(entries)
    if len(pool) > 0xFFFF:
        sys.exit('a pool holds more than 65535 code points')
    return entry_lists, pool


def c_rows(items, indent=4):
    """Initializers packed into lines of at most 79 columns."""
    lines = []
    line = ''
    for item in items:
        text = item + ','
        if line and len(line) + 1 + len(text) > 79:
            lines.append(line)
            line = ''
        if line:
            line += ' ' + text
        else:
            line = ' ' * indent + text
    if line:
        lines.append(line)
    return '\n'.join(lines)


def hex_code(code):
    return '0x%04X' % code


def print_table(declaration, comment, items):
    print('/* %s */' % comment.replace('\n', '\n * '))
    print('static const %s[] = {' % declaration)
    print(c_rows(items))
    print('};')
    print()


def main():
    arguments = sys.argv[1:]
    fetch = False
    if arguments and arguments[0] == '--download':
        fetch = True
        arguments = arguments[1:]
    if len(arguments) != 1:
        sys.exit('usage: genUnicode.py [--download] UCD_DIR')
    directory = arguments[0]
    if fetch:
        download(directory)

    points = read_unicode_data(directory)
    blocks = read_blocks(directory)
    special = read_special_casing(directory)
    excluded = read_composition_exclusions(directory)

    # General categories, without the unassigned code points
    categories = ranges_of({code: point.category
                            for code, point in points.items()})

    # Simple case mappings
    simple_upper = {code: point.upper for code, point in points.items()
                    if point.upper is not None and point.upper != code}
    simple_lower = {code: point.lower for code, point in points.items()
                    if point.lower is not None and point.lower != code}
    upper_ranges = delta_ranges(simple_upper)
    lower_ranges = delta_ranges(simple_lower)

    # Full case mappings of single code points
    def full_upper(code):
        if code in special:
            return special[code][1]
        return [simple_upper.get(code, code)]

    def full_lower(code):
        if code in special:
            return special[code][0]
        return [simple_lower.get(code, code)]

    special_upper = {code: full_upper(code) for code in special
                     if full_upper(code) != [simple_upper.get(code, code)]}
    special_lower = {code: full_lower(code) for code in special
                     if full_lower(code) != [simple_lower.get(code, code)]}

    # Case variants: the code points with the same full lower-case or
    # upper-case mapping. Unassigned code points map to themselves and
    # are the mapping of none, so they have none.
    by_lower = collections.defaultdict(set)
    by_upper = collections.defaultdict(set)
    for code in points:
        by_lower[tuple(full_lower(code))].add(code)
        by_upper[tuple(full_upper(code))].add(code)
    # Most code points have as variants just their simple mappings, as
    # xp3UnicodeCaseVariants() gives them for code points not listed;
    # only the others are listed.
    variant_exceptions = {}
    for code in points:
        found = by_lower[tuple(full_lower(code))] | \
            by_upper[tuple(full_upper(code))]
        found = sorted(found - {code})
        default = {simple_upper.get(code, code), simple_lower.get(code, code)}
        if found != sorted(default - {code}):
            variant_exceptions[code] = found

    # Normalization
    combining = ranges_of({code: point.combining
                           for code, point in points.items()
                           if point.combining != 0})
    canonical = {}
    compatibility = {}
    for code, point in points.items():
        if not point.decomposition:
            continue
        canonical_form = full_decomposition(points, code, False)
        compatibility_form = full_decomposition(points, code, True)
        if canonical_form != [code]:
            canonical[code] = canonical_form
        if compatibility_form != canonical_form:
            compatibility[code] = compatibility_form
    compositions = []
    for code, point in points.items():
        if point.tag is not None or len(point.decomposition) != 2:
            continue
        if code in excluded:
            continue
        compositions.append(Composition(point.decomposition[0],
                                        point.decomposition[1], code))
    compositions.sort()
    composing_starters = sorted({entry.second for entry in compositions
                                 if points[entry.second].combining == 0})

    (canonical_entries, compatibility_entries), decomposition_pool = \
        pooled(canonical, compatibility)
    (variant_entries,), variant_pool = pooled(variant_exceptions)

    longest_case = max(len(value) for value in
                       list(special_upper.values()) +
                       list(special_lower.values()))
    longest_decomposition = max(entry.length for entry in
                                canonical_entries + compatibility_entries)
    # Those not listed have at most a simple upper and lower mapping
    most_variants = max([2] + [entry.length for entry in variant_entries])

    print('/*')
    print(' * Unicode tables, generated by codegen/genUnicode.py from the')
    print(' * Unicode Character Database %s,' % UNICODE_VERSION)
    print(' * %s.' % UCD_URL)
    print(' * Do not edit.')
    print(' *')
    print(' * The tables are derived from the data files of the Unicode')
    print(' * Character Database, under this licence:')
    print(' *')
    for line in UNICODE_LICENSE.split('\n'):
        print((' * ' + line).rstrip())
    print(' */')
    print()
    print('/* The longest full case mapping, decomposition and list of case')
    print(' * variants of one code point */')
    print('#define XP3_UNICODE_TABLES_CASE_MAX %d' % longest_case)
    print('#define XP3_UNICODE_TABLES_DECOMPOSITION_MAX %d' %
          longest_decomposition)
    print('#define XP3_UNICODE_TABLES_VARIANTS_MAX %d' % most_variants)
    print()

    print_table(
        'xp3UnicodeCategoryRange xp3UnicodeCategories',
        'The general categories of the assigned code points; the others\n'
        'are Cn',
        ['{ %s, %s, XP3_UNICODE_%s }' % (hex_code(entry.first),
                                         hex_code(entry.last),
                                         entry.value.upper())
         for entry in categories])
    print_table(
        'xp3UnicodeBlockRange xp3UnicodeBlocks',
        'The blocks, by their names without spaces, sorted by name',
        ['{ "%s", %s, %s }' % (block.name, hex_code(block.first),
                               hex_code(block.last))
         for block in blocks])
    print_table(
        'xp3UnicodeDeltaRange xp3UnicodeUpperDeltas',
        'The simple upper-case mappings: each stride-th code point from\n'
        'first to last maps to itself plus delta',
        ['{ %s, %s, %d, %d }' % (hex_code(entry.first), hex_code(entry.last),
                                 entry.delta, entry.stride)
         for entry in upper_ranges])
    print_table(
        'xp3UnicodeDeltaRange xp3UnicodeLowerDeltas',
        'The simple lower-case mappings, likewise',
        ['{ %s, %s, %d, %d }' % (hex_code(entry.first), hex_code(entry.last),
                                 entry.delta, entry.stride)
         for entry in lower_ranges])
    print_table(
        'xp3UnicodeFullCase xp3UnicodeFullUpper',
        'The full upper-case mappings that differ from the simple ones',
        ['{ %s, %d, { %s } }' % (hex_code(code), len(special_upper[code]),
                                 ', '.join(hex_code(part) for part in
                                           special_upper[code]))
         for code in sorted(special_upper)])
    print_table(
        'xp3UnicodeFullCase xp3UnicodeFullLower',
        'The full lower-case mappings that differ from the simple ones',
        ['{ %s, %d, { %s } }' % (hex_code(code), len(special_lower[code]),
                                 ', '.join(hex_code(part) for part in
                                           special_lower[code]))
         for code in sorted(special_lower)])
    print_table(
        'uint32_t xp3UnicodeVariantPool',
        'The case variants, for xp3UnicodeVariants',
        [hex_code(code) for code in variant_pool])
    print_table(
        'xp3UnicodePoolEntry xp3UnicodeVariants',
        'The case variants of the code points whose variants are not\n'
        'just their simple case mappings: length code points of\n'
        'xp3UnicodeVariantPool from start',
        ['{ %s, %d, %d }' % (hex_code(entry.code), entry.start,
                             entry.length)
         for entry in variant_entries])
    print_table(
        'xp3UnicodeCombiningRange xp3UnicodeCombiningClasses',
        'The canonical combining classes other than 0',
        ['{ %s, %s, %d }' % (hex_code(entry.first), hex_code(entry.last),
                             entry.value)
         for entry in combining])
    print_table(
        'uint32_t xp3UnicodeDecompositionPool',
        'The full decompositions, for xp3UnicodeCanonical and\n'
        'xp3UnicodeCompatibility',
        [hex_code(code) for code in decomposition_pool])
    print_table(
        'xp3UnicodePoolEntry xp3UnicodeCanonical',
        'The full canonical decompositions, without the Hangul syllables:\n'
        'length code points of xp3UnicodeDecompositionPool from start',
        ['{ %s, %d, %d }' % (hex_code(entry.code), entry.start,
                             entry.length)
         for entry in canonical_entries])
    print_table(
        'xp3UnicodePoolEntry xp3UnicodeCompatibility',
        'The full compatibility decompositions that differ from the\n'
        'canonical ones',
        ['{ %s, %d, %d }' % (hex_code(entry.code), entry.start,
                             entry.length)
         for entry in compatibility_entries])
    print_table(
        'xp3UnicodeComposition xp3UnicodeCompositions',
        'The canonical composition pairs, without the Hangul syllables\n'
        'and the composition exclusions, sorted by both code points',
        ['{ %s, %s, %s }' % (hex_code(entry.first), hex_code(entry.second),
                             hex_code(entry.composite))
         for entry in compositions])
    print('/* The starters that compose with a starter before them, other')
    print(' * than the Hangul vowels and trailing consonants */')
    print('static const uint32_t xp3UnicodeComposingStarters[] = {')
    print(c_rows([hex_code(code) for code in composing_starters]))
    print('};')


main()
