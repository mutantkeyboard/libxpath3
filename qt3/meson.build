# The QT3 runner uses internal functions, so it compiles the library
# sources directly, like the tests.
qt3run = executable(
    'qt3run',
    'qt3run.c',
    xpath3_src,
    xp3version_h,
    include_directories: inc,
    dependencies: libxml2_dep,
    c_args: ['-DXP3_STATIC'],
)

qt3_dir = get_option('qt3')
if qt3_dir != ''
    foreach spec : ['XP20', 'XP30', 'XP31']
        test(
            'qt3-' + spec,
            qt3run,
            args: [
                '-s', spec,
                '-k', files('known-failures-' + spec + '.txt'),
                qt3_dir / 'catalog.xml',
            ],
            suite: 'qt3',
            timeout: 600,
        )
    endforeach
endif
