#!/usr/bin/env bash
# run.sh — captures Chrome and Nordstjernen over the site list and writes the comparison report.
set -euo pipefail
trap 'exit 2' ERR

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${OUT:-$ROOT/sitebench-out}
LABEL=${LABEL:-nordstjernen}
LABELS=${LABELS:-$LABEL}
RUNS=${RUNS:-3}
VISUAL_RUNS=${VISUAL_RUNS:-3}
VIEWPORT=${VIEWPORT:-1280x800}
SITES=${SITES:-$HERE/sites.tsv}
CHROME=${CHROME:-auto}
NS=${NS:-1}
MIN_PARITY=${MIN_PARITY:-}
MAX_DROP=${MAX_DROP:-}
FILTER=()
CHROME_OPTS=()
[ -n "${CHROME_CHANNEL:-}" ] && CHROME_OPTS+=(--channel="$CHROME_CHANNEL")
[ -n "${CHROME_EXECUTABLE:-}" ] && CHROME_OPTS+=(--executable="$CHROME_EXECUTABLE")
CAP=()
[ -n "${MAX_COMPONENTS:-}" ] && CAP=(--max-components="$MAX_COMPONENTS")

usage() {
    cat <<EOF
usage: scripts/sitebench/run.sh [--only=id,..] [--category=c,..]

environment:
  OUT=DIR        capture + report root (default: sitebench-out)
  LABEL=NAME     label for this Nordstjernen capture (default: nordstjernen)
  LABELS=a,b     labels to compare in the report, oldest first (default: \$LABEL)
  RUNS=N         cold load runs per site and browser, medians reported (default: 3)
  VISUAL_RUNS=N  settled loads per site and browser that are scored (default: 3)
  VIEWPORT=WxH   viewport for both browsers (default: 1280x800)
  SITES=FILE     site list (default: scripts/sitebench/sites.tsv)
  CHROME=auto|1|0  capture Chrome: only when missing (auto), always (1), never (0)
  CHROME_CHANNEL=NAME     Playwright channel to launch, e.g. chrome for the installed
                          Google Chrome (default: Playwright's own Chromium)
  CHROME_EXECUTABLE=PATH  Chrome or Chromium binary to launch instead
  NS=1|0         capture Nordstjernen (default: 1)
  NS_BIN=PATH    Nordstjernen binary (default: builddir/src/gtk/nordstjernen)
  NS_LOCALE=NAME  locale Nordstjernen runs under (default: en_US.UTF-8)
  MIN_PARITY=P   fail when the newest label's mean visual parity is below P
  MAX_DROP=D     fail when the newest label's parity is more than D below the oldest's,
                 in the mean over the sites both have or, beyond the site's noise, on a
                 stable site, or when it failed in more of a site's visual runs
  MAX_COMPONENTS=N  most components inventoried per page in each browser (default: 3000)

Exits with compare.py's status: 1 when a threshold fails, 2 on usage errors,
missing captures or a capture or comparison that fails, 0 otherwise.
EOF
}

for arg in "$@"; do
    case "$arg" in
        --only=*|--category=*) FILTER+=("$arg") ;;
        -h|--help) usage; exit 0 ;;
        *) echo "run.sh: unknown argument $arg" >&2; usage >&2; exit 2 ;;
    esac
done

for name in MIN_PARITY MAX_DROP; do
    value=${!name}
    if [ -n "$value" ] && ! [[ $value =~ ^[0-9]+([.][0-9]+)?$ ]]; then
        echo "run.sh: $name wants a number of 0 or more, not $value" >&2
        exit 2
    fi
done
if [ -n "$MIN_PARITY" ] && awk "BEGIN { exit !($MIN_PARITY > 100) }"; then
    echo "run.sh: MIN_PARITY wants a number from 0 to 100, not $MIN_PARITY" >&2
    exit 2
fi
if [ -n "$MAX_DROP" ] && ! [[ $LABELS =~ [^,],+[^,] ]]; then
    echo "run.sh: MAX_DROP wants two labels to compare in LABELS, not $LABELS" >&2
    exit 2
fi

mkdir -p "$OUT"

if [ "$CHROME" != 0 ]; then
    skip=()
    [ "$CHROME" = auto ] && skip=(--skip-existing)
    node "$HERE/chrome-capture.js" --out="$OUT" --runs="$RUNS" --visual-runs="$VISUAL_RUNS" \
        --viewport="$VIEWPORT" --sites="$SITES" ${CHROME_OPTS[@]+"${CHROME_OPTS[@]}"} \
        ${skip[@]+"${skip[@]}"} ${CAP[@]+"${CAP[@]}"} ${FILTER[@]+"${FILTER[@]}"}
fi

if [ "$NS" != 0 ]; then
    python3 "$HERE/ns-capture.py" --out="$OUT" --label="$LABEL" --runs="$RUNS" \
        --visual-runs="$VISUAL_RUNS" --viewport="$VIEWPORT" --sites="$SITES" ${CAP[@]+"${CAP[@]}"} \
        ${FILTER[@]+"${FILTER[@]}"}
fi

CHECKS=()
[ -n "$MIN_PARITY" ] && CHECKS+=(--min-parity="$MIN_PARITY")
[ -n "$MAX_DROP" ] && CHECKS+=(--max-drop="$MAX_DROP")
exec python3 "$HERE/compare.py" --out="$OUT" --labels="$LABELS" --viewport="$VIEWPORT" \
    ${CHECKS[@]+"${CHECKS[@]}"}
