#!/usr/bin/env python3
#
# genFunctions.py: generate codegen/functions.inc, the table of
# built-in functions, from the W3C specifications
#
# Usage:
#
#   python3 codegen/genFunctions.py CATALOG FO20 FO30 > codegen/functions.inc
#
# CATALOG  F&O 3.1 function catalog, the XML source of the
#          Recommendation's function descriptions:
#          https://www.w3.org/TR/xpath-functions-31/function-catalog.xml
# FO20     F&O 1.0 Second Edition, the function library of XPath 2.0:
#          https://www.w3.org/TR/2010/REC-xpath-functions-20101214/
# FO30     F&O 3.0:
#          https://www.w3.org/TR/2014/REC-xpath-functions-30-20140408/
#
# The catalog gives every function of XPath 3.1 with its signatures
# and properties. The language level of a signature is the first of
# the three specifications that has the same name and arity, so for
# example fn:round#1 is XPath 2.0 and fn:round#2 is XPath 3.0.
#
# The catalog has a few mistakes, corrected here:
#
# - fn:path, fn:max, fn:min and array:sort label their properties with
#   the wrong arity. The properties are listed in signature order, as
#   the descriptions in the Recommendation show, so they are matched by
#   position.
# - fn:boolean has no properties, and neither has its section in the
#   Recommendation. The effective boolean value depends on nothing in
#   the context, so it is deterministic, context-independent and
#   focus-independent, like fn:not, which is defined through it.
#
# The signature text is always the one of F&O 3.1, also for entries
# at a lower level. A few types changed between the versions, for
# example fn:string-join takes xs:anyAtomicType* in 3.1 but xs:string*
# before: the language level restricts which functions exist, not
# their types (doc/DESIGN.md 5.1).
# - array:sort#3 is described as context-independent, although it takes
#   a collation like array:sort#2 and fn:sort#3. It is marked
#   context-dependent, which can only make constant folding skip it.
# - fn:distinct-values#2 is labelled deterministic while #1 is
#   nondeterministic with respect to ordering. The order of the result
#   is implementation-dependent at both arities (the Recommendation
#   says so for the function as a whole), so #2 gets the same property.
#
# Copyright (C) 2026 Antonio Nešić <nesic.antonio@gmail.com>
# See COPYING for the status of this software.
#
# Author: Antonio Nešić <nesic.antonio@gmail.com>

import collections
import html
import re
import sys
import xml.etree.ElementTree as ET

FOS = '{http://www.w3.org/xpath-functions/spec/namespace}'

# Order of the namespaces, as xp3FuncNamespace in xp3func.h
NAMESPACES = ['fn', 'math', 'map', 'array']

# One entry of the generated table
Signature = collections.namedtuple(
    'Signature', 'namespace name arity level flags text')

PROPERTY_FLAGS = [
    ('focus-dependent', 'XP3_FUNC_FOCUS'),
    ('context-dependent', 'XP3_FUNC_CONTEXT'),
    ('nondeterministic', 'XP3_FUNC_NONDETERMINISTIC'),
    ('higher-order', 'XP3_FUNC_HIGHER_ORDER'),
    ('nondeterministic-wrt-ordering', 'XP3_FUNC_ORDER'),
]


def html_signatures(path):
    """The (prefixed name, arity) pairs of an F&O HTML specification."""
    text = open(path, encoding='utf-8', errors='replace').read()
    result = set()
    for div in re.finditer(r'<div class="proto">(.*?)</div>', text, re.S):
        markup_free = re.sub(r'<[^>]+>', '', div.group(1))
        proto = ' '.join(html.unescape(markup_free).split())
        parts = re.match(r'([a-z]+:[\w-]+)\s*\((.*)\)\s*as', proto)
        if parts is None:
            continue
        qname = parts.group(1)
        args = parts.group(2).strip()
        result.add((qname, args.count('$') if args else 0))
    # With different markup nothing would match, and every function
    # would silently become XPath 3.1.
    if len(result) < 100:
        sys.exit('only %d signatures found in %s' % (len(result), path))
    return result


def properties(qname, index, arity, blocks):
    if not blocks:
        if qname != 'fn:boolean':
            sys.exit('%s has no properties' % qname)
        return {'deterministic', 'context-independent', 'focus-independent'}
    if len(blocks) == 1 and blocks[0].get('arity') is None:
        block = blocks[0]
    elif index < len(blocks):
        block = blocks[index]
    else:
        sys.exit('%s has fewer properties blocks than signatures' % qname)
    result = {prop.text for prop in block}
    if qname == 'array:sort' and arity == 3:
        result.discard('context-independent')
        result.add('context-dependent')
    if qname == 'fn:distinct-values' and arity == 2:
        result.discard('deterministic')
        result.add('nondeterministic-wrt-ordering')
    return result


def c_string(text, indent):
    """A C string literal, split to stay within 79 columns."""
    width = 79 - indent - 5     # quotes, and " }," after the last part
    parts = []
    while len(text) > width:
        cut = text.rfind(' ', 0, width)
        if cut <= 0:
            cut = width
        parts.append(text[:cut + 1])
        text = text[cut + 1:]
    parts.append(text)
    return ('\n' + ' ' * indent).join('"%s"' % part for part in parts)


def main():
    if len(sys.argv) != 4:
        sys.exit('usage: genFunctions.py CATALOG FO20 FO30')
    level20 = html_signatures(sys.argv[2])
    level30 = html_signatures(sys.argv[3])

    rows = []
    root = ET.parse(sys.argv[1]).getroot()
    for func in root.findall(FOS + 'function'):
        prefix = func.get('prefix')
        if prefix not in NAMESPACES:
            continue
        qname = prefix + ':' + func.get('name')
        blocks = func.findall(FOS + 'properties')
        protos = func.find(FOS + 'signatures').findall(FOS + 'proto')
        for index, proto in enumerate(protos):
            args = proto.findall(FOS + 'arg')
            variadic = any(arg.get('name') == '...' for arg in args)
            params = ['$%s as %s' % (arg.get('name'), arg.get('type'))
                      for arg in args if arg.get('name') != '...']
            arity = len(params)
            if variadic:
                params.append('...')
            if (qname, arity) in level20:
                level = 'XP3_XPATH_20'
            elif (qname, arity) in level30:
                level = 'XP3_XPATH_30'
            else:
                level = 'XP3_XPATH_31'
            found = properties(qname, index, arity, blocks)
            flags = [flag for name, flag in PROPERTY_FLAGS if name in found]
            if variadic:
                flags.insert(0, 'XP3_FUNC_VARIADIC')
            signature = '%s(%s) as %s' % (qname, ', '.join(params),
                                          proto.get('return-type'))
            rows.append(Signature(NAMESPACES.index(prefix), func.get('name'),
                                  arity, level, flags, signature))

    # Sorted for binary search, as xp3FuncLookup() expects
    rows.sort(key=lambda row: (row.namespace, row.name.encode('utf-8'),
                               row.arity))
    for previous, row in zip(rows, rows[1:]):
        if ((previous.namespace, previous.name, previous.arity) ==
                (row.namespace, row.name, row.arity)):
            sys.exit('duplicate signature %s' % row.text)

    print('/*')
    print(' * Built-in functions of XPath 3.1, generated by')
    print(' * codegen/genFunctions.py from the F&O 3.1 function catalog.')
    print(' * Do not edit.')
    print(' */')
    print()
    print('static const xp3FuncInfo xp3Functions[] = {')
    for row in rows:
        head = '    { %s, "%s", %d, %s,' % (
            'XP3_FUNC_NS_' + NAMESPACES[row.namespace].upper(), row.name,
            row.arity, row.level)
        flags = ' | '.join(row.flags) if row.flags else '0'
        if len(head) + len(flags) + 2 <= 79:
            print('%s %s,' % (head, flags))
        else:
            print(head)
            print('      %s,' % flags)
        print('      %s },' % c_string(row.text, 6))
    print('};')


main()
