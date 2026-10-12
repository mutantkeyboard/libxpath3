#!/bin/sh
#
# Run the Linux CI jobs from .gitlab-ci.yml locally in Docker.
#
# Usage: sh .gitlab-ci/run-local.sh [job...]
#
# Jobs: gcc gcc:c99 clang:asan cmake libxml2:master docs (default: all)
# Set IMAGE to use a different image than fedora:latest.
#
# Keep the jobs in sync with .gitlab-ci.yml.

set -u

ALL_JOBS="gcc gcc:c99 clang:asan cmake libxml2:master docs"

# On the host: start the container and run this script inside it.
if [ "${1:-}" != "--inside" ]; then
    root=$(cd "$(dirname "$0")/.." && pwd)
    exec docker run --rm -v "$root:/src:ro" "${IMAGE:-fedora:latest}" \
        sh /src/.gitlab-ci/run-local.sh --inside "$@"
fi
shift

echo "Installing packages..."
dnf install -y -q --setopt=install_weak_deps=False \
    gcc clang compiler-rt libasan libubsan \
    meson ninja-build cmake pkgconf-pkg-config \
    libxml2-devel doxygen git >/dev/null 2>&1 || {
    echo "dnf install failed"
    exit 1
}
echo "meson $(meson --version), gcc $(gcc -dumpversion)," \
     "clang $(clang -dumpversion), libxml2 $(pkg-config --modversion libxml-2.0)"

export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"

meson_job() {
    meson setup build --werror $1 &&
    meson compile -C build &&
    meson test -C build --print-errorlogs
}

job_gcc() {
    export CC=gcc
    meson_job ""
}

job_gcc_c99() {
    export CC=gcc CFLAGS="-pedantic-errors -Werror=vla"
    meson_job "-Dc_std=c99"
}

job_clang_asan() {
    export CC=clang ASAN_OPTIONS="detect_leaks=1"
    meson_job "-Db_sanitize=address,undefined -Db_lundef=false"
}

job_cmake() {
    cmake -S . -B build-cmake -G Ninja -DCMAKE_COMPILE_WARNING_AS_ERROR=ON &&
    cmake --build build-cmake &&
    ctest --test-dir build-cmake --output-on-failure
}

job_libxml2_master() {
    export CC=gcc LIBXML2_PREFIX="$PWD/libxml2-install"
    git clone -q --depth 1 https://gitlab.gnome.org/GNOME/libxml2.git libxml2-src &&
    meson setup libxml2-build libxml2-src --prefix="$LIBXML2_PREFIX" --libdir=lib \
        -Dpython=disabled -Ddocs=disabled &&
    meson install -C libxml2-build &&
    export PKG_CONFIG_PATH="$LIBXML2_PREFIX/lib/pkgconfig" \
           LD_LIBRARY_PATH="$LIBXML2_PREFIX/lib" &&
    meson setup build --werror &&
    meson compile -C build &&
    meson test -C build --print-errorlogs &&
    ./build/test/testbasic
}

job_docs() {
    export DOXYGEN_WARN_AS_ERROR="FAIL_ON_WARNINGS"
    meson setup build -Ddocs=enabled &&
    meson compile -C build &&
    test -f build/doc/html/index.html
}

[ $# -eq 0 ] && set -- $ALL_JOBS

failed=0
for job in "$@"; do
    fn="job_$(echo "$job" | tr ':' '_')"
    if ! command -v "$fn" >/dev/null 2>&1; then
        echo "Unknown job: $job (jobs: $ALL_JOBS)"
        failed=1
        continue
    fi

    # Fresh copy of the sources for every job, without local build directories.
    rm -rf /work && cp -r /src /work && cd /work &&
        rm -rf build build-* libxml2-src libxml2-build libxml2-install

    log="/tmp/$fn.log"
    if ( "$fn" ) >"$log" 2>&1; then
        echo "PASS $job"
    else
        echo "FAIL $job"
        tail -n 30 "$log"
        failed=1
    fi
done

exit $failed
