#!/usr/bin/env bash
# Lightweight local WPT runner for quick iteration: serves a WPT checkout with
# a plain static HTTP server and runs each testharness test through the
# browser's --wpt mode, printing one per-file summary line. Unlike wpt-run.sh
# it needs no `./wpt serve` infrastructure, so multi-origin and server-side
# tests are unsupported; use wpt-run.sh for the authoritative slice.
#
# Usage: wpt-local.sh <test-path-relative-to-wpt-root> ...
#   NS_WPT_ROOT  WPT checkout (default: $HOME/wpt)
#   NS_BIN       browser binary (default: builddir/src/gtk/nordstjernen)
#   NS_WPT_PORT  static server port (default: 8000)
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WPT=${NS_WPT_ROOT:-$HOME/wpt}
BIN=${NS_BIN:-$ROOT/builddir/src/gtk/nordstjernen}
PORT=${NS_WPT_PORT:-8000}
BASE="http://127.0.0.1:$PORT"
export NS_ALLOW_ROOT=${NS_ALLOW_ROOT:-1}

started=""
if ! curl -sf -o /dev/null "$BASE/resources/testharness.js" 2>/dev/null; then
  ( cd "$WPT" && exec python3 -m http.server "$PORT" --bind 127.0.0.1 ) \
    >/dev/null 2>&1 &
  started=$!
  for _ in $(seq 1 50); do
    curl -sf -o /dev/null "$BASE/resources/testharness.js" 2>/dev/null && break
    sleep 0.1
  done
fi
trap '[ -n "$started" ] && kill "$started" 2>/dev/null' EXIT

for t in "$@"; do
  out=$(timeout 40 "$BIN" --wpt --wpt-timeout-ms=8000 "$BASE/$t" 2>/dev/null)
  sum=$(echo "$out" | grep '^WPT SUMMARY')
  if [ -z "$sum" ]; then
    printf '%-70s HARNESS-BROKEN\n' "$t"
  else
    printf '%-70s %s\n' "$t" "${sum#WPT SUMMARY }"
  fi
done
