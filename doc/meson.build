# Doxygen, set up the same way as in libxml2

custom_target(
    'Doxygen documentation',
    input: [
        xpath3_src,
        files('../include/libxpath3/xpath3.h', 'mainpage.md', 'Doxyfile'),
        xp3version_h,
    ],
    output: 'html',
    command: [doxygen, '-q', files('Doxyfile')],
    env: {
        'SOURCE_ROOT': meson.project_source_root() + '/',
        'BUILD_ROOT':  meson.project_build_root()  + '/',
    },
    build_by_default: true,
    install: get_option('docs').enabled(),
    install_dir: get_option('datadir') / 'doc' / 'libxpath3',
)
