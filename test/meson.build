# The unit tests report each test function as a TAP test, which Meson
# lists one by one in its JUnit report (meson-logs/testlog.junit.xml).
testbasic = executable(
    'testbasic',
    'testbasic.c',
    'testtap.c',
    dependencies: xpath3_dep,
)
test('basic', testbasic, protocol: 'tap')

# Tests of internal functions compile the library sources directly,
# because the shared library only exports the public API.
internal_test_args = ['-DXP3_STATIC']

foreach name : ['error', 'context', 'lexer', 'ast', 'parser', 'decimal',
                'value', 'func', 'static', 'nodes', 'eval', 'unicode',
                'regex']
    exe = executable(
        'test' + name,
        'test' + name + '.c',
        'testalloc.c',
        'testtap.c',
        xpath3_src,
        xp3version_h,
        include_directories: inc,
        dependencies: [libxml2_dep, m_dep],
        c_args: internal_test_args,
    )
    test(name, exe, protocol: 'tap')
endforeach

# Several threads evaluating one compiled expression; the clang:tsan CI
# job runs it under ThreadSanitizer. POSIX threads only.
threads_dep = dependency('threads', required: false)
if threads_dep.found() and host_machine.system() != 'windows'
    testthreads = executable(
        'testthreads',
        'testthreads.c',
        'testtap.c',
        dependencies: [xpath3_dep, threads_dep],
    )
    test('threads', testthreads, protocol: 'tap')
endif
