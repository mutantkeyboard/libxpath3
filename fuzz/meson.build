# The replay driver runs the seed corpus as regression tests with any
# compiler. The real fuzz target needs libFuzzer, which comes with
# compiler-rt: clang on Linux has it, Apple's clang ships without the
# fuzzer runtime (ld: library 'libclang_rt.fuzzer_osx.a' not found).
# The usual way to fuzz is therefore Docker (.gitlab-ci/run-local.sh
# fuzz) and CI; on a Mac, the workaround below fuzzes natively.
#
# Homebrew LLVM's compiler-rt is complete:
# brew install llvm, then configure this option with
# CC=$(brew --prefix llvm)/bin/clang. If linking fails with Apple's ld,
# add -fuse-ld=lld, and a "duplicate libraries: -lc++" warning is a
# known harmless one (https://github.com/llvm/llvm-project/issues/102630).
# See https://llvm.org/docs/LibFuzzer.html and the same situation in
# Bitcoin Core's fuzzing guide,
# https://github.com/bitcoin/bitcoin/blob/master/doc/fuzzing.md#macos-hints-for-libfuzzer
fuzz_seed = files(
    'seed/arith20',
    'seed/err_lex',
    'seed/err_syntax',
    'seed/flwor31',
    'seed/hof31',
    'seed/inject',
    'seed/kinds20',
    'seed/noctx',
    'seed/path31',
    'seed/quant30',
    'seed/types31',
)

fuzz_replay = executable(
    'xp3parse',
    'xp3parse.c',
    '../test/testalloc.c',
    xpath3_src,
    xp3version_h,
    include_directories: inc,
    dependencies: [libxml2_dep, m_dep],
    c_args: ['-DXP3_STATIC', '-DXP3_FUZZ_DRIVER'],
)
test('fuzz-replay', fuzz_replay, args: fuzz_seed)

if get_option('fuzzer')
    executable(
        'xp3parse-fuzz',
        'xp3parse.c',
        '../test/testalloc.c',
        xpath3_src,
        xp3version_h,
        include_directories: inc,
        dependencies: [libxml2_dep, m_dep],
        c_args: ['-DXP3_STATIC', '-fsanitize=fuzzer'],
        link_args: ['-fsanitize=fuzzer'],
    )
endif
