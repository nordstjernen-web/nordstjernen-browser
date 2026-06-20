#!/bin/sh
# selfcheck — prove the AOT compiler is sound.
#
# For every test program and input it runs the entry function both through
# the pure QuickJS interpreter (reference) and through aot-run (AOT enabled
# by default, automatic fallback), then asserts the numeric results are
# identical. It also asserts the expected path was taken: numeric programs
# must AOT-compile to native; out-of-subset programs must fall back to the
# interpreter — and still be correct.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
QJSDIR=$(cd "$HERE/../../src/quickjs" && pwd)
OUT=${OUT:-/tmp/aot-run}
mkdir -p "$OUT"
export OUT AOTC="$OUT/aotc" QJS="$OUT/qjs"

QF="-DENABLE_DUMPS -D_GNU_SOURCE -funsigned-char -w"
SR="dtoa.c libregexp.c libunicode.c"

echo ">> building qjs + aotc"
( cd "$QJSDIR" && cc -O2 $QF $SR quickjs.c quickjs-libc.c gen/repl.c gen/standalone.c qjs.c \
    -o "$QJS" -lm -lpthread -ldl )
( cd "$QJSDIR" && cc -O2 $QF -I. "$HERE/aotc.c" $SR -o "$AOTC" -lm -lpthread -ldl )

# file:entry:expectpath:arg-sets(comma-separated, each space-separated)
CASES="
arith:arith:native:3 4|7 2|0 5|-3 6|10 10
shortcircuit:shortcircuit:native:1 1|200 0|3 9|-5 -5|50 60
ternary:ternary:native:10|101|0|1|1000
dowhile:dowhile:native:1|5|20|100
mutual:mutual:native:0|1|7|20|21
floatmath:floatmath:native:5|50|200|1
notop:notop:native:0|1|2|-3
fib:fib:native:5|10|20|25
sumloop:sumloop:native:10|1000|100000
collatz:collatz:native:2|100|2000
mandel:mandel:native:8|32|64
strings:strings:interpreter:0|3|10
mathlib:mathlib:interpreter:4|9|16|2
array:array:interpreter:0|5|50
"

pass=0; fail=0
printf "\n%-14s %-10s %-22s %-22s %-8s %s\n" test args reference aot path result
printf -- "------------------------------------------------------------------------------------------------\n"

norm() { printf '%s' "$1" | sed 's/^-nan$/nan/'; }

CASEFILE=$(mktemp)
printf '%s\n' "$CASES" | grep . > "$CASEFILE"

while IFS= read -r line; do
  name=${line%%:*}; rest=${line#*:}
  entry=${rest%%:*}; rest=${rest#*:}
  expect=${rest%%:*}; argsets=${rest#*:}

  js=""
  [ -f "$HERE/tests/$name.js" ] && js="$HERE/tests/$name.js"
  [ -f "$HERE/bench/$name.js" ] && js="$HERE/bench/$name.js"

  OLDIFS=$IFS; IFS='|'
  for args in $argsets; do
    IFS=$OLDIFS
    ref=$(FORCE_INTERP=1 sh "$HERE/aot-run.sh" "$js" "$entry" $args 2>/dev/null)
    aot=$(sh "$HERE/aot-run.sh" "$js" "$entry" $args 2>"$OUT/path.txt")
    path=$(sed -n 's/^\[aot\] //p' "$OUT/path.txt" | sed 's/ .*//')

    ok="OK"
    [ "$(norm "$ref")" = "$(norm "$aot")" ] || ok="VALUE!"
    [ "$path" = "$expect" ] || ok="PATH($path)!"

    if [ "$ok" = "OK" ]; then pass=$((pass+1)); else fail=$((fail+1)); fi
    printf "%-14s %-10s %-22s %-22s %-8s %s\n" "$name" "$args" "$ref" "$aot" "$path" "$ok"
  done
  IFS=$OLDIFS
done < "$CASEFILE"
rm -f "$CASEFILE"

echo
echo "passed: $pass   failed: $fail"
[ "$fail" -eq 0 ]
