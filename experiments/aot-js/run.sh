#!/bin/sh
# Build the AOT compiler, compile every benchmark both ways, verify the
# AOT result against the QuickJS interpreter, and print a timing table.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
QJSDIR=$(cd "$HERE/../../src/quickjs" && pwd)
OUT=${OUT:-/tmp/aot-bench}
mkdir -p "$OUT"

QJS_FLAGS="-DENABLE_DUMPS -D_GNU_SOURCE -funsigned-char -w"
QJS_SRCS="dtoa.c libregexp.c libunicode.c"

echo ">> building qjs interpreter"
( cd "$QJSDIR" && cc -O2 $QJS_FLAGS $QJS_SRCS quickjs.c quickjs-libc.c \
    gen/repl.c gen/standalone.c qjs.c -o "$OUT/qjs" -lm -lpthread -ldl )

echo ">> building aotc"
( cd "$QJSDIR" && cc -O2 $QJS_FLAGS -I. "$HERE/aotc.c" $QJS_SRCS \
    -o "$OUT/aotc" -lm -lpthread -ldl )

# name  arg  reps
BENCHES="fib:32:1 sumloop:1000000:50 collatz:20000:5 mandel:300:3"

printf "\n%-10s %-9s %-7s %14s %14s %9s %12s\n" \
  bench arg reps "interp(ms)" "aot(ms)" "speedup" "result"
printf -- "----------------------------------------------------------------------------------\n"

for spec in $BENCHES; do
  name=${spec%%:*}; rest=${spec#*:}; arg=${rest%%:*}; reps=${rest##*:}
  js="$HERE/bench/$name.js"

  "$OUT/aotc" "$js" "$OUT/$name.c" "$name" >/dev/null 2>&1
  cc -O2 "$OUT/$name.c" -o "$OUT/$name" -lm

  aot_out=$("$OUT/$name" "$arg" "$reps" 2>"$OUT/$name.aterr")
  aot_ms=$(sed -n 's/^ms=//p' "$OUT/$name.aterr")

  int_out=$("$OUT/qjs" "$HERE/bench/time_interp.js" "$js" "$arg" "$reps" 2>"$OUT/$name.ierr")
  int_ms=$(sed -n 's/^ms=//p' "$OUT/$name.ierr")

  if [ "$aot_out" = "$int_out" ]; then ok="OK"; else ok="MISMATCH"; fi
  speedup=$(awk "BEGIN{ if ($aot_ms>0) printf \"%.1fx\", $int_ms/$aot_ms; else print \"-\" }")

  printf "%-10s %-9s %-7s %14s %14s %9s %12s\n" \
    "$name" "$arg" "$reps" "$int_ms" "$aot_ms" "$speedup" "$ok"
done
echo
