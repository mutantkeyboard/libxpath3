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
option(XP3_BUILD_TESTS "Build tests" ON)
option(XP3_BUILD_DOCS "Build API documentation with Doxygen" OFF)

include(GNUInstallDirs)

find_package(LibXml2 2.9.0 REQUIRED)

configure_file(
    include/libxpath3/xp3version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/libxpath3/xp3version.h
    @ONLY
)

add_library(xpath3
    xpath3.c
)
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
target_link_libraries(xpath3 PUBLIC LibXml2::LibXml2)
set_target_properties(xpath3 PROPERTIES
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

if(XP3_BUILD_TESTS)
    enable_testing()
    add_executable(testbasic test/testbasic.c)
    target_link_libraries(testbasic PRIVATE xpath3)
    add_test(NAME basic COMMAND testbasic)
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
