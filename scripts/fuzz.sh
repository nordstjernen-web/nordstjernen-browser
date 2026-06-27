#!/bin/sh
# Build and run the libFuzzer harnesses over the in-tree parsers (lexbor HTML
# and WHATWG URL) under ASan + UBSan, coverage-guided. Requires clang with
# compiler-rt. Override FUZZ_SECONDS, FUZZ_TARGETS, FUZZ_BUILDDIR, CC; the
# default is a short smoke run.
set -eu

root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
build=${FUZZ_BUILDDIR:-$root/builddir-fuzz}
inc=$root/src/lexbor/source
out=$root/fuzz/out
secs=${FUZZ_SECONDS:-30}
targets=${FUZZ_TARGETS:-html url}
cc=${CC:-clang}

# liblexbor instrumented for coverage-guided fuzzing (SanitizerCoverage on top
# of ASan + UBSan), so libFuzzer can actually explore the parser.
if [ ! -d "$build" ]; then
    ( cd "$root" && CC="$cc" meson setup "$build" \
        -Dai=disabled -Dbuildtype=debug -Doptimization=1 -Db_lundef=false \
        -Db_sanitize=address,undefined \
        -Dc_args="-fsanitize=fuzzer-no-link -fno-sanitize=function" )
fi
ninja -C "$build" src/lexbor/liblexbor_static.a
lib=$build/src/lexbor/liblexbor_static.a

mkdir -p "$out"
for t in $targets; do
    echo "== building fuzz_$t =="
    "$cc" -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize=function \
        -I"$inc" "$root/fuzz/fuzz_$t.c" "$lib" -o "$out/fuzz_$t"
done
for t in $targets; do
    echo "== running fuzz_$t for ${secs}s =="
    mkdir -p "$out/corpus_$t"
    "$out/fuzz_$t" -max_total_time="$secs" -timeout=25 -rss_limit_mb=4096 \
        -max_len=8192 "$out/corpus_$t" "$root/fuzz/seeds/$t"
done
echo "fuzz: all targets clean"
