#!/usr/bin/env bash
# Fetches tc39/test262 (the submodule declared in src/quickjs/.gitmodules
# is intentionally left uninitialized, see docs/quickjs-ecma-specification-
# compliance.md) and runs the full suite through the in-tree run-test262
# harness, building it via the upstream CMake path if needed.
#
# Usage: scripts/test262-run.sh [run-test262 args...]
#   No args: full suite, matches the upstream "test262" Makefile target.
#   -u: regenerate src/quickjs/test262_errors.txt from the current pass/
#       fail state (run after fixing a known failure, or after a sync).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
QJS_DIR="$ROOT/src/quickjs"
BUILD_DIR="$QJS_DIR/build"

if [ ! -d "$QJS_DIR/test262/harness" ]; then
    echo "fetching tc39/test262 (shallow, main) into src/quickjs/test262 ..." >&2
    TMP=$(mktemp -d)
    trap 'rm -rf "$TMP"' EXIT
    curl -sS -L -o "$TMP/test262.tar.gz" \
        https://codeload.github.com/tc39/test262/tar.gz/refs/heads/main
    mkdir -p "$QJS_DIR/test262"
    tar xzf "$TMP/test262.tar.gz" -C "$QJS_DIR/test262" --strip-components=1
fi

cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release "$QJS_DIR" >/dev/null
cmake --build "$BUILD_DIR" -j"$(nproc)" --target qjs run-test262 >&2

cd "$QJS_DIR"
exec "$BUILD_DIR/run-test262" -m -c test262.conf -a "$@"
