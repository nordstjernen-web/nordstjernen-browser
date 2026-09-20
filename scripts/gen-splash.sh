#!/usr/bin/env bash
# gen-splash.sh — regenerate src/about_splash_gif.h, the about:start splash animation.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 -c 'import numpy, PIL' 2>/dev/null || {
    echo "splash rendering needs python3 with numpy and pillow" >&2; exit 1; }
exec python3 scripts/gen-splash.py "$@"
