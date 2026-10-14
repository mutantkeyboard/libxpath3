project(
    'libxpath3',
    'c',
    version: files('VERSION'),
    license: 'MIT',
    meson_version: '>= 1.3.0',
    default_options: [
        # C99 is the project standard. MSVC has no C99 mode, so it falls back to C11.
        'c_std=c99,c11',
        'warning_level=3',
        'buildtype=debugoptimized',
    ],
)

cc = meson.get_compiler('c')

# Variable-length arrays are not allowed.
add_project_arguments(cc.get_supported_arguments('-Werror=vla'), language: 'c')

version = meson.project_version()
version_parts = version.split('.')

libxml2_dep = dependency('libxml-2.0', version: '>= 2.9.0')

inc = include_directories('include')

subdir('include/libxpath3')

xpath3_src = files(
    'xp3ast.c',
    'xp3chars.c',
    'xp3context.c',
    'xp3decimal.c',
    'xp3double.c',
    'xp3error.c',
    'xp3lexer.c',
    'xp3parser.c',
    'xp3types.c',
    'xp3value.c',
    'xpath3.c',
)

# xp3double.c needs the math library on some systems.
m_dep = cc.find_library('m', required: false)

xpath3_lib = library(
    'xpath3',
    xpath3_src,
    xp3version_h,
    include_directories: inc,
    dependencies: [libxml2_dep, m_dep],
    c_args: ['-DIN_LIBXPATH3'],
    gnu_symbol_visibility: 'hidden',
    version: version,
    install: true,
)

xpath3_dep = declare_dependency(
    link_with: xpath3_lib,
    include_directories: inc,
    sources: xp3version_h,
    dependencies: [libxml2_dep, m_dep],
)
meson.override_dependency('libxpath3', xpath3_dep)

pkg = import('pkgconfig')
pkg.generate(
    xpath3_lib,
    name: 'libxpath3',
    description: 'XPath 3.1 for libxml2 documents',
    requires: 'libxml-2.0',
)

subdir('test')
subdir('qt3')
subdir('fuzz')

doxygen = find_program('doxygen', required: get_option('docs'))
if doxygen.found()
    subdir('doc')
endif

summary({
    'libxml2': libxml2_dep.version(),
    'docs': doxygen.found(),
}, section: 'Configuration')
