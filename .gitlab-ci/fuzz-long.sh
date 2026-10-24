#!/bin/sh
# Run one fuzzer for a long time in Docker, keeping its log and any
# crash inputs. The CI fuzz job only runs two minutes per fuzzer; this
# is for runs of hours, such as overnight.
#
# Usage: sh .gitlab-ci/fuzz-long.sh parse|eval SECONDS OUTDIR
#
#   parse   fuzz/xp3parse.c, compiling, seeded from fuzz/seed
#   eval    fuzz/xp3eval.c, evaluating optimized and unoptimized,
#           seeded from fuzz/seed-eval
#
# OUTDIR gets build.log, fuzz.log (ending in "exit=0" when nothing was
# found) and any crash-*, leak-*, timeout-* or slow-unit-* inputs. The
# fuzzers run with ASan, UBSan, leak detection, allocation-failure
# injection and a 60-second limit per input. Set IMAGE to use another
# image than fedora:latest.

set -e

case "${1:-}" in
    parse) fuzzer=xp3parse-fuzz seeds=fuzz/seed ;;
    eval) fuzzer=xp3eval-fuzz seeds=fuzz/seed-eval ;;
    *) echo "Usage: sh $0 parse|eval SECONDS OUTDIR" >&2; exit 2 ;;
esac
seconds=${2:?seconds missing}
mkdir -p "${3:?output directory missing}"
out=$(cd "$3" && pwd)
root=$(cd "$(dirname "$0")/.." && pwd)

exec docker run --rm -v "$root:/src:ro" -v "$out:/out" \
    "${IMAGE:-fedora:latest}" sh -c "
dnf install -y -q --setopt=install_weak_deps=False clang compiler-rt \
    libasan libubsan meson ninja-build pkgconf-pkg-config libxml2-devel \
    >/dev/null 2>&1 || { echo 'dnf install failed' >/out/build.log; exit 1; }
cp -r /src /work && cd /work && rm -rf build build-* fuzz-out
CC=clang meson setup build --werror -Dfuzzer=true \
    -Db_sanitize=address,undefined -Db_lundef=false >/out/build.log 2>&1 &&
meson compile -C build >>/out/build.log 2>&1 ||
    { echo 'build failed' >>/out/build.log; exit 1; }
mkdir corpus && cp $seeds/* corpus/
cd /out
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
    /work/build/fuzz/$fuzzer -dict=/work/fuzz/xp3.dict -max_len=10000 \
    -timeout=60 -max_total_time=$seconds -print_final_stats=1 \
    -artifact_prefix=/out/ /work/corpus >/out/fuzz.log 2>&1
echo exit=\$? >>/out/fuzz.log
"
