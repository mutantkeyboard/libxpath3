# Build and test libxpath3 with MSVC, for the msvc job on GNOME's Windows
# runners. libxml2 comes from the release that LIBXML2_VERSION names,
# built with the same compiler and installed into libxml2-install, which
# the job caches, so it is only built when the version changes.

$ErrorActionPreference = "Stop"
$generator = "Visual Studio 16 2019"
$libxml2 = Join-Path $PWD "libxml2-install"

function Invoke-Step {
    param([string] $What, [scriptblock] $Command)

    & $Command
    if ($LastExitCode -ne 0) {
        throw "$What failed"
    }
}

if (-not (Test-Path (Join-Path $libxml2 "bin"))) {
    Invoke-Step "cloning libxml2" {
        git clone -q --depth 1 --branch $Env:LIBXML2_VERSION `
            https://gitlab.gnome.org/GNOME/libxml2.git libxml2-src
    }
    Invoke-Step "configuring libxml2" {
        cmake -G $generator -A x64 -S libxml2-src -B libxml2-build `
            -DCMAKE_INSTALL_PREFIX="$libxml2" `
            -DBUILD_SHARED_LIBS=ON `
            -DLIBXML2_WITH_ICONV=OFF `
            -DLIBXML2_WITH_PROGRAMS=OFF `
            -DLIBXML2_WITH_PYTHON=OFF `
            -DLIBXML2_WITH_TESTS=OFF
    }
    Invoke-Step "building libxml2" {
        cmake --build libxml2-build --config Release --target install
    }
}

# Warnings are errors for libxpath3 only. CMake reads CFLAGS when it
# configures, and before 3.24 ignores CMAKE_COMPILE_WARNING_AS_ERROR.
$Env:CFLAGS = "/WX"
# The tests load libxml2's DLL from the PATH.
$Env:Path = (Join-Path $libxml2 "bin") + ";" + $Env:Path

Invoke-Step "configuring libxpath3" {
    cmake -G $generator -A x64 -S . -B build-cmake `
        -DCMAKE_PREFIX_PATH="$libxml2"
}
Invoke-Step "building libxpath3" {
    cmake --build build-cmake --config Release
}
Invoke-Step "testing libxpath3" {
    ctest --test-dir build-cmake -C Release --output-on-failure
}
