#!/bin/sh
# Configure an ASan + UBSan build and exercise the in-process headless render
# path over the data/render-tests fixtures. This sanitizes the existing run
# path (it is not a new test suite) — a memory-safety net under the project's
# no-automated-test policy. Override SAN_BUILDDIR.
set -eu

root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
build=${SAN_BUILDDIR:-$root/builddir-san}

if [ ! -d "$build" ]; then
    ( cd "$root" && CC="${CC:-clang}" CXX="${CXX:-clang++}" meson setup "$build" \
        -Db_sanitize=address,undefined -Db_lundef=false \
        -Dc_args=-fno-sanitize=function -Dcpp_args=-fno-sanitize=function \
        -Dai=disabled -Doptimization=1 -Dbuildtype=debug )
fi
meson compile -C "$build"

bin="$build/src/gtk/nordstjernen"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1:strict_string_checks=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"
export NS_ALLOW_ROOT=1

fail=0
for f in "$root"/data/render-tests/*.html; do
    [ -e "$f" ] || continue
    if ! "$bin" --headless --url="file://$f" \
            --dump="png:$tmp/out.png" --viewport=1024 --viewport-height=768 \
            --settle-ms=100 >"$tmp/log" 2>&1; then
        echo "SANITIZER FAILURE on $(basename "$f"):"
        cat "$tmp/log"
        fail=1
    fi
done

[ "$fail" -eq 0 ] && echo "sanitize: all fixtures clean"
exit "$fail"
