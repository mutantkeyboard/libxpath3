cmake_minimum_required(VERSION 3.18)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" XP3_VERSION)
string(STRIP "${XP3_VERSION}" XP3_VERSION)

project(libxpath3 VERSION ${XP3_VERSION} LANGUAGES C)

set(XP3_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(XP3_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(XP3_VERSION_MICRO ${PROJECT_VERSION_PATCH})

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(XP3_BUILD_DOCS "Build API documentation with Doxygen" OFF)

include(GNUInstallDirs)

# include(CTest) defines the standard BUILD_TESTING option and writes
# DartConfiguration.tcl, which "ctest -T" (used by IDEs) looks for.
# CTEST_TARGETS_ADDED keeps it from adding the CDash dashboard targets.
set_property(GLOBAL PROPERTY CTEST_TARGETS_ADDED 1)
include(CTest)

find_package(LibXml2 2.9.0 REQUIRED)

configure_file(
    include/libxpath3/xp3version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/libxpath3/xp3version.h
    @ONLY
)

set(XP3_SOURCES
    xp3ast.c
    xp3chars.c
    xp3context.c
    xp3decimal.c
    xp3double.c
    xp3error.c
    xp3eval.c
    xp3fnnumeric.c
    xp3fnseq.c
    xp3func.c
    xp3lexer.c
    xp3nodes.c
    xp3ops.c
    xp3parser.c
    xp3static.c
    xp3types.c
    xp3value.c
    xpath3.c
)

# xp3double.c needs the math library on some systems. Elsewhere, as with
# MSVC, the math functions are in the C runtime and find_library() sets
# XP3_LIBM to XP3_LIBM-NOTFOUND, so every target links ${XP3_LIBM_LIBS},
# which is empty then, and never XP3_LIBM directly.
find_library(XP3_LIBM m)
set(XP3_LIBM_LIBS "")
if(XP3_LIBM)
    set(XP3_LIBM_LIBS ${XP3_LIBM})
endif()

add_library(xpath3 ${XP3_SOURCES})
target_include_directories(xpath3 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_definitions(xpath3 PRIVATE IN_LIBXPATH3)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(xpath3 PUBLIC XP3_STATIC)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(xpath3 PRIVATE -Wall -Wextra -Wpedantic -Werror=vla)
endif()
target_link_libraries(xpath3 PUBLIC LibXml2::LibXml2 ${XP3_LIBM_LIBS})
set_target_properties(xpath3 PROPERTIES
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

if(BUILD_TESTING)
    add_executable(testbasic test/testbasic.c)
    target_link_libraries(testbasic PRIVATE xpath3)
    add_test(NAME basic COMMAND testbasic)

    # Several threads evaluating one compiled expression, POSIX only
    find_package(Threads)
    if(CMAKE_USE_PTHREADS_INIT AND NOT WIN32)
        add_executable(testthreads test/testthreads.c)
        target_link_libraries(testthreads PRIVATE xpath3 Threads::Threads)
        add_test(NAME threads COMMAND testthreads)
    endif()

    # Tests of internal functions compile the library sources directly,
    # because the shared library only exports the public API.
    foreach(name error context lexer ast parser decimal value func static nodes eval)
        add_executable(test${name} test/test${name}.c test/testalloc.c ${XP3_SOURCES})
        target_compile_definitions(test${name} PRIVATE XP3_STATIC)
        target_include_directories(test${name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_BINARY_DIR}/include
        )
        target_link_libraries(test${name} PRIVATE LibXml2::LibXml2 ${XP3_LIBM_LIBS})
        add_test(NAME ${name} COMMAND test${name})
    endforeach()

    # Replay the fuzz seed corpus as regression tests
    add_executable(xp3parse-replay fuzz/xp3parse.c test/testalloc.c
        ${XP3_SOURCES})
    target_compile_definitions(xp3parse-replay PRIVATE XP3_STATIC
        XP3_FUZZ_DRIVER)
    target_include_directories(xp3parse-replay PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )
    target_link_libraries(xp3parse-replay PRIVATE LibXml2::LibXml2 ${XP3_LIBM_LIBS})
    file(GLOB XP3_FUZZ_SEED ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/seed/*)
    add_test(NAME fuzz-replay COMMAND xp3parse-replay ${XP3_FUZZ_SEED})

    # The same for the evaluation fuzzer
    add_executable(xp3eval-replay fuzz/xp3eval.c test/testalloc.c
        ${XP3_SOURCES})
    target_compile_definitions(xp3eval-replay PRIVATE XP3_STATIC
        XP3_FUZZ_DRIVER)
    target_include_directories(xp3eval-replay PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )
    target_link_libraries(xp3eval-replay PRIVATE LibXml2::LibXml2 ${XP3_LIBM_LIBS})
    file(GLOB XP3_FUZZ_EVAL_SEED ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/seed-eval/*)
    add_test(NAME fuzz-eval-replay COMMAND xp3eval-replay ${XP3_FUZZ_EVAL_SEED})

    # QT3 runner, and the QT3 tests if XP3_QT3_DIR points to a checkout
    add_executable(qt3run qt3/qt3run.c ${XP3_SOURCES})
    target_compile_definitions(qt3run PRIVATE XP3_STATIC)
    target_include_directories(qt3run PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )
    target_link_libraries(qt3run PRIVATE LibXml2::LibXml2 ${XP3_LIBM_LIBS})
    set(XP3_QT3_DIR "" CACHE PATH "Directory of a QT3 test suite checkout")
    if(XP3_QT3_DIR)
        foreach(spec XP20 XP30 XP31)
            add_test(NAME qt3-${spec} COMMAND qt3run -s ${spec}
                -k ${CMAKE_CURRENT_SOURCE_DIR}/qt3/known-failures-${spec}.txt
                ${XP3_QT3_DIR}/catalog.xml)
        endforeach()
    endif()
endif()

if(XP3_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    add_custom_target(docs ALL
        COMMAND ${CMAKE_COMMAND} -E env
            SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/
            BUILD_ROOT=${CMAKE_CURRENT_BINARY_DIR}/
            ${DOXYGEN_EXECUTABLE} -q ${CMAKE_CURRENT_SOURCE_DIR}/doc/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
endif()

install(TARGETS xpath3
    EXPORT libxpath3-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES
    include/libxpath3/xpath3.h
    ${CMAKE_CURRENT_BINARY_DIR}/include/libxpath3/xp3version.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libxpath3
)

set(prefix ${CMAKE_INSTALL_PREFIX})
set(libdir "\${prefix}/${CMAKE_INSTALL_LIBDIR}")
set(includedir "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
configure_file(libxpath3.pc.in ${CMAKE_CURRENT_BINARY_DIR}/libxpath3.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libxpath3.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
