#!/bin/sh
# aot-run — run a JS entry function with AOT compilation enabled by default.
#
# Tries to ahead-of-time compile the entry (and its transitive callees) to
# native code. If the whole call graph is in the safe numeric subset it runs
# the native binary; otherwise it transparently falls back to the QuickJS
# interpreter. The result is identical either way — AOT only engages when it
# is provably equivalent.
#
# usage: aot-run.sh FILE.js ENTRY [arg...]
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/aot-run}
mkdir -p "$OUT"
AOTC=${AOTC:-$OUT/aotc}
QJS=${QJS:-$OUT/qjs}

file=$1; entry=$2; shift 2 || true
base=$(basename "$file" .js)

# AOT is on by default; FORCE_INTERP=1 disables it (used to get a reference).
# AOT is used only if aotc accepts the program AND the generated C compiles;
# any failure falls through to the interpreter, so correctness never depends
# on the AOT path succeeding.
aot_ok=0
if [ "${FORCE_INTERP:-0}" != "1" ] && \
   "$AOTC" "$file" "$OUT/$base.c" "$entry" >/dev/null 2>"$OUT/$base.aotlog" && \
   cc -O2 "$OUT/$base.c" -o "$OUT/$base.bin" -lm 2>"$OUT/$base.cclog"; then
    aot_ok=1
fi

if [ "$aot_ok" = "1" ]; then
    "$OUT/$base.bin" "$@" 2>/dev/null
    echo "[aot] native" >&2
else
    cat > "$OUT/$base.harness.js" <<EOF
import * as std from "qjs:std";
const src = std.loadFile("$file");
const f = std.evalScript(src + "\n;$entry;");
const a = scriptArgs.slice(1).map(parseFloat);
std.out.printf("%.17g\n", f.apply(null, a));
EOF
    "$QJS" "$OUT/$base.harness.js" "$@"
    echo "[aot] interpreter (fallback)" >&2
fi
