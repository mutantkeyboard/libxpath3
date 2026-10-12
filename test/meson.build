testbasic = executable(
    'testbasic',
    'testbasic.c',
    dependencies: xpath3_dep,
)
test('basic', testbasic)

# Tests of internal functions compile the library sources directly,
# because the shared library only exports the public API.
internal_test_args = ['-DXP3_STATIC']

foreach name : ['error', 'context', 'lexer']
    exe = executable(
        'test' + name,
        'test' + name + '.c',
        'testalloc.c',
        xpath3_src,
        xp3version_h,
        include_directories: inc,
        dependencies: libxml2_dep,
        c_args: internal_test_args,
    )
    test(name, exe)
endforeach
