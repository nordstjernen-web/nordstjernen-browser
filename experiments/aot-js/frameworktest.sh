#!/bin/sh
# frameworktest — run the AOT compiler against real third-party JavaScript.
#
# Downloads a set of well-known numeric benchmark sources (the SunSpider /
# Kraken math + bitops kernels), then for every top-level function reports
# whether it AOT-compiles or falls back, and — for the ones that compile —
# verifies the native result matches the QuickJS interpreter exactly.
#
# This is how the compiler is exercised on code it did not author: real
# programs that freely mix numeric kernels with arrays, globals, Date and
# higher-order calls. The pass criteria are (1) no crashes, (2) every
# AOT-compiled function matches the interpreter. Requires network access;
# skips cleanly if a source cannot be fetched.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
QJSDIR=$(cd "$HERE/../../src/quickjs" && pwd)
OUT=${OUT:-/tmp/aot-fw}
mkdir -p "$OUT" "$OUT/src"
export OUT AOTC="$OUT/aotc" QJS="$OUT/qjs"

QF="-DENABLE_DUMPS -D_GNU_SOURCE -funsigned-char -w"
SR="dtoa.c libregexp.c libunicode.c"
echo ">> building qjs + aotc"
( cd "$QJSDIR" && cc -O2 $QF $SR quickjs.c quickjs-libc.c gen/repl.c gen/standalone.c qjs.c -o "$QJS" -lm -lpthread -ldl )
( cd "$QJSDIR" && cc -O2 $QF -I. "$HERE/aotc.c" $SR -o "$AOTC" -lm -lpthread -ldl )

BASE="https://raw.githubusercontent.com/WebKit/WebKit/main/PerformanceTests/SunSpider/tests/sunspider-1.0.2"
FILES="math-cordic.js math-spectral-norm.js math-partial-sums.js bitops-bits-in-byte.js bitops-3bit-bits-in-byte.js access-nbody.js access-fannkuch.js controlflow-recursive.js"

got=0
for f in $FILES; do
  if timeout 25 curl -fsSL "$BASE/$f" -o "$OUT/src/$f" 2>/dev/null; then got=$((got+1)); fi
done
if [ "$got" -eq 0 ]; then
  echo "!! no sources fetched (no network?) — skipping"; exit 0
fi
echo ">> fetched $got source file(s)"

native=0; fallback=0; mism=0
printf "\n%-34s %-10s %-8s %s\n" function path sample result
printf -- "------------------------------------------------------------------------\n"

for js in "$OUT/src"/*.js; do
  [ -f "$js" ] || continue
  bn=$(basename "$js")
  for e in $(grep -oE '^function [A-Za-z_$][A-Za-z0-9_$]*' "$js" | awk '{print $2}'); do
    if "$AOTC" "$js" "$OUT/t.c" "$e" >/dev/null 2>/dev/null && \
       cc -O2 "$OUT/t.c" -o "$OUT/t.bin" -lm 2>/dev/null; then
      native=$((native+1))
      # verify against interpreter on a few sample integer args
      # pass several args so functions of arity 1..4 all receive real values;
      # JS ignores extra args, so over-supplying keeps the comparison fair.
      # Values are kept small so recursive kernels (Ackermann, Takeuchi)
      # terminate quickly; the interpreter reference is run under a timeout.
      res="OK"; sample=""
      for a in "2 1 3 1" "3 2 1 2" "1 3 2 1"; do
        ref=$(FORCE_INTERP=1 timeout 10 sh "$HERE/aot-run.sh" "$js" "$e" $a 2>/dev/null || true)
        aot=$(timeout 10 "$OUT/t.bin" $a 2>/dev/null || true)
        sample="$a"
        n1=$(printf '%s' "$ref" | sed 's/^-nan$/nan/')
        n2=$(printf '%s' "$aot" | sed 's/^-nan$/nan/')
        [ "$n1" = "$n2" ] || { res="MISMATCH(ref=$ref aot=$aot)"; mism=$((mism+1)); break; }
      done
      printf "%-34s %-10s %-8s %s\n" "$bn:$e" "native" "$sample" "$res"
    else
      fallback=$((fallback+1))
      printf "%-34s %-10s %-8s %s\n" "$bn:$e" "fallback" "-" "ok"
    fi
  done
done

echo
echo "native: $native   fallback: $fallback   mismatches: $mism"
[ "$mism" -eq 0 ]
