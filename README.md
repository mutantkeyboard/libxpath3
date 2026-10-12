# libxpath3

libxpath3 is an XPath 3.1 engine in C for documents parsed with [libxml2](https://gitlab.gnome.org/GNOME/libxml2). It evaluates expressions directly on libxml2's tree, so existing libxml2 code can use it without converting documents.

> **Work in progress.** libxpath3 is under active development and not ready for production use yet. The API may change.

## Features and scope

- **XPath 3.1 on libxml2's tree**, using only libxml2's public API.
- **XPath 2.0 and 3.0 too.** XPath 3.1 is backwards compatible, so 2.0 and 3.0 expressions work with the same API. An option restricts the language to a specific version.
- **Exact arithmetic.** `xs:decimal` and `xs:integer` have arbitrary precision.
- **Safe for untrusted input.** Limits on recursion, operation count and sizes, and no file or network access unless the application allows it.
- **A foundation for XSLT 2.0 and 3.0** in libxslt, in coordination with the libxslt maintainer.
- **Close to libxml2.** MIT licensed and following libxml2's conventions: its memory functions, error reporting, and Meson and CMake builds.

Not in scope:

- **libxml2's XPath 1.0 engine.** It stays as it is, and its behaviour does not change.
- **XSLT.** XSLT 2.0 and 3.0 belong in libxslt.
- **Schema-aware processing.** Only the built-in XML Schema types are supported.
- **XQuery and streaming.**

## Conformance

Conformance is measured against the W3C [QT3 test suite](https://github.com/w3c/qt3tests), and the pass rate is published here with each release. Fuzzers with allocation-failure injection run in the same style as libxml2's.

## Building the project

Requirements: Meson 1.3 or later, Ninja, a C99 compiler and libxml2 2.9 or later. Doxygen is optional, for the API documentation. CMake 3.18 or later can be used instead of Meson.

The code is C99, with no variable-length arrays and no compiler extensions.

### macOS

Install the tools and a current libxml2 with Homebrew:

```sh
brew install meson ninja libxml2 doxygen
```

Then build and test:

```sh
export PKG_CONFIG_PATH="$(brew --prefix)/opt/libxml2/lib/pkgconfig"   # Homebrew libxml2, not macOS's 2.9.13

meson setup build            # once
meson compile -C build       # build
meson test -C build          # run tests
```

### Linux

Install the compiler, the build tools and the libxml2 development package.

Debian and Ubuntu:

```sh
sudo apt install build-essential meson ninja-build pkg-config libxml2-dev doxygen
```

Fedora:

```sh
sudo dnf install gcc meson ninja-build pkgconf-pkg-config libxml2-devel doxygen
```

If your distribution's Meson is older than 1.3 (for example Debian 12 or Ubuntu 22.04), install a current one with `pipx install meson`.

Then build and test:

```sh
meson setup build
meson compile -C build
meson test -C build
```

### Windows

Windows builds are not covered by CI yet.

**MSYS2 (MinGW).** In the MSYS2 UCRT64 shell:

```sh
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-meson mingw-w64-ucrt-x86_64-ninja \
          mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-libxml2

meson setup build
meson compile -C build
meson test -C build
```

On Windows on ARM, use the CLANGARM64 shell and the `mingw-w64-clang-aarch64-` packages instead.

**Visual Studio (MSVC).** In a Developer PowerShell or Developer Command Prompt, get libxml2 from [vcpkg](https://vcpkg.io) and build with CMake:

```powershell
vcpkg install libxml2:x64-windows

cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
ctest --test-dir build -C Release
```

MSVC has no separate C99 mode. Meson builds with C11 there, and CMake uses MSVC's default C mode.

### API documentation

With Doxygen installed, configure with `meson setup build -Ddocs=enabled`. The HTML ends up in `build/doc/html/`. With CMake, pass `-DXP3_BUILD_DOCS=ON`.

### CMake

CMake works on every platform:

```sh
cmake -S . -B build-cmake            # on macOS add -DCMAKE_PREFIX_PATH="$(brew --prefix)/opt/libxml2"
cmake --build build-cmake
ctest --test-dir build-cmake
```

## Specifications

- [XML Path Language (XPath) 3.1](https://www.w3.org/TR/xpath-31/)
- [XPath and XQuery Functions and Operators 3.1](https://www.w3.org/TR/xpath-functions-31/)
- [XQuery and XPath Data Model 3.1](https://www.w3.org/TR/xpath-datamodel-31/)

## Contributing

Development happens on GNOME GitLab. Issues and merge requests go to https://gitlab.gnome.org/mutantkeyboard/libxpath3.

The GitHub copy is a read-only mirror, so please open issues and merge requests on GNOME GitLab.

Related upstream discussions:

- [libxml2#31](https://gitlab.gnome.org/GNOME/libxml2/-/issues/31): XPath 3.0/3.1 support in libxml2
- [libxslt#153](https://gitlab.gnome.org/GNOME/libxslt/-/issues/153): XSLT 2.0 and 3.0 support in libxslt

## Licence

MIT. See [COPYING](COPYING).
