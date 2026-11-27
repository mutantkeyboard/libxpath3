# The tutorial programs of doc/tutorial/, built against the library as
# an application would build them. Each runs as a test that compares
# its output with expected/NAME.txt, and a second test checks that the
# code shown on its page is taken from it (runexample.c).

examples = [
    '01-first-query',
    '02-namespaces-variables',
    '03-numbers',
    '04-strings-regex',
    '05-dates-times',
    '06-extension-functions',
    '07-untrusted-expressions',
]

runexample = executable(
    'runexample',
    'runexample.c',
)

examples_data = meson.current_source_dir() / 'data'

foreach name : examples
    exe = executable(
        name,
        name + '.c',
        'common.c',
        dependencies: xpath3_dep,
    )
    test(
        name,
        runexample,
        args: ['output', exe, examples_data, files('expected' / name + '.txt')],
        depends: exe,
        suite: 'examples',
        timeout: 120,
    )
    test(
        name + '-page',
        runexample,
        args: [
            'excerpts',
            files('../doc/tutorial' / name + '.md'),
            files(name + '.c', 'common.c'),
        ],
        suite: 'examples',
    )
endforeach
