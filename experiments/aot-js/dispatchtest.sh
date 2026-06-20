#!/bin/sh
# dispatchtest — verify the in-engine AOT dispatch wiring (JS_SetFunctionAOT)
# in the real QuickJS engine, with the NS_AOT_DISPATCH switch on:
#   1. dispatch_test: native path is taken for numeric args, and any
#      non-numeric argument falls back to the interpreter bit-identically.
#   2. registry_test: aotc's --registry output links into the engine and its
#      registered kernels match the interpreter across a domain sweep.
# The engine's stock build (without NS_AOT_DISPATCH) is unaffected.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
QJSDIR=$(cd "$HERE/../../src/quickjs" && pwd)
OUT=${OUT:-/tmp/aot-dispatch}
mkdir -p "$OUT"
QF="-DNS_AOT_DISPATCH -D_GNU_SOURCE -funsigned-char -w -I$QJSDIR"
SR="$QJSDIR/dtoa.c $QJSDIR/libregexp.c $QJSDIR/libunicode.c"

echo ">> building aotc + tests (NS_AOT_DISPATCH on)"
cc -O2 -DENABLE_DUMPS $QF "$HERE/aotc.c" $SR -o "$OUT/aotc" -lm -lpthread -ldl

echo ">> dispatch_test"
cc -O2 $QF "$HERE/dispatch_test.c" $SR -o "$OUT/dispatch_test" -lm -lpthread -ldl
"$OUT/dispatch_test"

echo
echo ">> registry_test (aotc --registry over tests/framework.js)"
"$OUT/aotc" "$HERE/tests/framework.js" "$OUT/fwreg.c" --registry
cc -O2 $QF "$HERE/registry_test.c" "$OUT/fwreg.c" "$QJSDIR/quickjs.c" $SR \
   -o "$OUT/registry_test" -lm -lpthread -ldl
( cd "$HERE/../.." && "$OUT/registry_test" )
