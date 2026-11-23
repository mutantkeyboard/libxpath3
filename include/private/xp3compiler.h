/**
 * @file
 *
 * @brief Compiler-specific attributes (internal)
 *
 * @copyright Copyright (C) 2026 Antonio Nešić (nesic.antonio@gmail.com).
 * See COPYING for the status of this software.
 *
 * @author Antonio Nešić (nesic.antonio@gmail.com)
 */

#ifndef XP3_XP3COMPILER_H
#define XP3_XP3COMPILER_H

/*
 * Keep a function out of its callers. The parser, the analysis and the
 * evaluator recurse once per level of nesting, and a helper inlined
 * into a recursive function adds its locals to every level of the
 * stack, although only one level at a time runs it (doc/DESIGN.md
 * 7.3).
 */
#if defined(_MSC_VER)
  #define XP3_NOINLINE __declspec(noinline)
#elif defined(__GNUC__)
  #define XP3_NOINLINE __attribute__((noinline))
#else
  #define XP3_NOINLINE
#endif

#endif /* XP3_XP3COMPILER_H */
