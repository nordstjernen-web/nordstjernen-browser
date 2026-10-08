#!/usr/bin/env bash
# run.sh — captures Chrome and Nordstjernen over the site list and writes the comparison report.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${OUT:-$ROOT/sitebench-out}
LABEL=${LABEL:-nordstjernen}
LABELS=${LABELS:-$LABEL}
RUNS=${RUNS:-3}
VIEWPORT=${VIEWPORT:-1280x800}
CHROME=${CHROME:-auto}
NS=${NS:-1}
FILTER=()

usage() {
    cat <<EOF
usage: scripts/sitebench/run.sh [--only=id,..] [--category=c,..]

environment:
  OUT=DIR        capture + report root (default: sitebench-out)
  LABEL=NAME     label for this Nordstjernen capture (default: nordstjernen)
  LABELS=a,b     labels to compare in the report, oldest first (default: \$LABEL)
  RUNS=N         cold load runs per site and browser, medians reported (default: 3)
  VIEWPORT=WxH   viewport for both browsers (default: 1280x800)
  CHROME=auto|1|0  capture Chrome: only when missing (auto), always (1), never (0)
  NS=1|0         capture Nordstjernen (default: 1)
  NS_BIN=PATH    Nordstjernen binary (default: builddir/src/gtk/nordstjernen)
  NS_LOCALE=NAME  locale Nordstjernen runs under (default: en_US.UTF-8)
EOF
}

for arg in "$@"; do
    case "$arg" in
        --only=*|--category=*) FILTER+=("$arg") ;;
        -h|--help) usage; exit 0 ;;
        *) echo "run.sh: unknown argument $arg" >&2; usage >&2; exit 2 ;;
    esac
done

mkdir -p "$OUT"

if [ "$CHROME" != 0 ]; then
    skip=()
    [ "$CHROME" = auto ] && skip=(--skip-existing)
    node "$HERE/chrome-capture.js" --out="$OUT" --runs="$RUNS" --viewport="$VIEWPORT" \
        ${skip[@]+"${skip[@]}"} ${FILTER[@]+"${FILTER[@]}"}
fi

if [ "$NS" != 0 ]; then
    python3 "$HERE/ns-capture.py" --out="$OUT" --label="$LABEL" --runs="$RUNS" \
        --viewport="$VIEWPORT" ${FILTER[@]+"${FILTER[@]}"}
fi

python3 "$HERE/compare.py" --out="$OUT" --labels="$LABELS" --viewport="$VIEWPORT"
