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

# MinGW's own stdio is C99-conforming, with %zu and %lld, unlike the
# Microsoft runtime's; the format checks of xp3ErrorNew() rely on it.
if cc.get_define('__MINGW32__') != ''
    add_project_arguments('-D__USE_MINGW_ANSI_STDIO=1', language: 'c')
endif

# The system clock and the local offset (xp3datetime.c), each from the
# first of these that exists; Windows has its own. Without
# localtime_r() and gmtime_r() the local offset is UTC, because
# localtime() shares its result between threads.
posix_time_prefix = '#define _POSIX_C_SOURCE 200809L\n#include <time.h>'
if cc.has_function('clock_gettime', prefix: posix_time_prefix)
    add_project_arguments('-DXP3_HAVE_CLOCK_GETTIME', language: 'c')
endif
if cc.has_function('gettimeofday',
                   prefix: '#define _POSIX_C_SOURCE 200809L\n#include <sys/time.h>')
    add_project_arguments('-DXP3_HAVE_GETTIMEOFDAY', language: 'c')
endif
if (cc.has_function('localtime_r', prefix: posix_time_prefix) and
    cc.has_function('gmtime_r', prefix: posix_time_prefix))
    add_project_arguments('-DXP3_HAVE_LOCALTIME_R', language: 'c')
endif

version = meson.project_version()
version_parts = version.split('.')

libxml2_dep = dependency('libxml-2.0', version: '>= 2.9.0')

inc = include_directories('include')

subdir('include/libxpath3')

xpath3_src = files(
    'xp3ast.c',
    'xp3chars.c',
    'xp3context.c',
    'xp3datetime.c',
    'xp3decimal.c',
    'xp3double.c',
    'xp3error.c',
    'xp3eval.c',
    'xp3fndatetime.c',
    'xp3fnnode.c',
    'xp3fnnumeric.c',
    'xp3fnregex.c',
    'xp3fnseq.c',
    'xp3fnstring.c',
    'xp3func.c',
    'xp3lexer.c',
    'xp3nodes.c',
    'xp3ops.c',
    'xp3parser.c',
    'xp3regex.c',
    'xp3static.c',
    'xp3types.c',
    'xp3unicode.c',
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
