#!/bin/sh
# arraytest — verify the typed-array (Float64Array) AOT path against the
# QuickJS interpreter. aotc emits both the native C and a matching JS
# reference harness (`<out>.ref.js`) that builds identical arrays from the
# same CLI; this script runs both and asserts identical output.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
QJSDIR=$(cd "$HERE/../../src/quickjs" && pwd)
OUT=${OUT:-/tmp/aot-arr}
mkdir -p "$OUT"
AOTC="$OUT/aotc"; QJS="$OUT/qjs"

QF="-D_GNU_SOURCE -funsigned-char -w"
SR="dtoa.c libregexp.c libunicode.c"
echo ">> building qjs + aotc"
( cd "$QJSDIR" && cc -O2 $QF $SR quickjs.c quickjs-libc.c gen/repl.c gen/standalone.c qjs.c -o "$QJS" -lm -lpthread -ldl )
( cd "$QJSDIR" && cc -O2 $QF -I. "$HERE/aotc.c" $SR -o "$AOTC" -lm -lpthread -ldl )

JS="$HERE/tests/arrays.js"

# entry:args (args = array lengths and/or scalars, space-separated)
CASES="
asum:0
asum:1
asum:100
asum:4096
amax:50
amax:1000
dot:80 120
dot:500 500
norm:256
norm:4096
blur:200
blur:5000
variance:333
variance:4096
scale:100 3.5
scale:4096 1.0001
axpy:80 80 2.5
axpy:1000 1000 -1
setall:64 7.25
setall:512 -3.5
clampall:200 -1 1
clampall:999 -5 5
negate:256
negate:4096
cumsum:128
cumsum:4096
"

norm_nan() { printf '%s' "$1" | sed 's/-nan/nan/g'; }

pass=0; fail=0
printf "\n%-10s %-12s %-26s %-26s %s\n" entry args AOT interp result
printf -- "--------------------------------------------------------------------------------\n"

CF=$(mktemp); printf '%s\n' "$CASES" | grep . > "$CF"
while IFS= read -r line <&3; do
  entry=${line%%:*}; args=${line#*:}
  "$AOTC" "$JS" "$OUT/k.c" "$entry" >/dev/null 2>/dev/null
  cc -O2 "$OUT/k.c" -o "$OUT/k.bin" -lm 2>/dev/null
  aot=$("$OUT/k.bin" $args 2>/dev/null | tr '\n' '|')
  ref=$("$QJS" "$OUT/k.c.ref.js" $args 2>/dev/null | tr '\n' '|')
  if [ "$(norm_nan "$aot")" = "$(norm_nan "$ref")" ]; then ok=OK; pass=$((pass+1)); else ok=DIFF; fail=$((fail+1)); fi
  printf "%-10s %-12s %-26s %-26s %s\n" "$entry" "$args" "$aot" "$ref" "$ok"
done 3< "$CF"
rm -f "$CF"

echo
echo "passed: $pass   failed: $fail"
[ "$fail" -eq 0 ]
