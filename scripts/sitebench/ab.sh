#!/usr/bin/env bash
# ab.sh — captures Chrome and two Nordstjernen builds site by site, back to back, and compares them.
set -euo pipefail
trap 'exit 2' ERR

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${OUT:-$ROOT/sitebench-out/ab}
RUNS=${RUNS:-3}
VISUAL_RUNS=${VISUAL_RUNS:-3}
VIEWPORT=${VIEWPORT:-1280x800}
SITES=${SITES:-$HERE/sites.tsv}
BEFORE_LABEL=${BEFORE_LABEL:-before}
AFTER_LABEL=${AFTER_LABEL:-after}
MIN_PARITY=${MIN_PARITY:-}
MAX_DROP=${MAX_DROP:-}
CHROME_OPTS=()
[ -n "${CHROME_CHANNEL:-}" ] && CHROME_OPTS+=(--channel="$CHROME_CHANNEL")
[ -n "${CHROME_EXECUTABLE:-}" ] && CHROME_OPTS+=(--executable="$CHROME_EXECUTABLE")

usage() {
    cat <<EOF
usage: scripts/sitebench/ab.sh BEFORE_BIN AFTER_BIN [--only=id,..] [--category=c,..]

Loads each site in Chrome, then in BEFORE_BIN, then in AFTER_BIN before
moving to the next site, so all three see the live page within minutes of
each other. Sites already captured under OUT are skipped, so an
interrupted run can be resumed by running it again.

environment:
  OUT=DIR            capture + report root (default: sitebench-out/ab)
  RUNS=N             cold load runs per site and browser (default: 3)
  VISUAL_RUNS=N      settled loads per site and browser that are scored (default: 3)
  VIEWPORT=WxH       viewport for both browsers (default: 1280x800)
  SITES=FILE         site list (default: scripts/sitebench/sites.tsv)
  BEFORE_LABEL=NAME  label of the first build (default: before)
  AFTER_LABEL=NAME   label of the second build (default: after)
  NS_LOCALE=NAME     locale both builds run under (default: en_US.UTF-8)
  CHROME_CHANNEL=NAME     Playwright channel to launch, e.g. chrome for the installed
                          Google Chrome (default: Playwright's own Chromium)
  CHROME_EXECUTABLE=PATH  Chrome or Chromium binary to launch instead
  MIN_PARITY=P       fail when AFTER_BIN's mean visual parity is below P
  MAX_DROP=D         fail when AFTER_BIN's parity is more than D below BEFORE_BIN's,
                     in the mean or, beyond the site's noise, on a stable site, or
                     when it failed in more of a site's visual runs

Exits with compare.py's status: 1 when a threshold fails, 2 on usage errors,
missing captures or a capture or comparison that fails, 0 otherwise.
EOF
}

FILTER=()
BINS=()
for arg in "$@"; do
    case "$arg" in
        --only=*|--category=*) FILTER+=("$arg") ;;
        -h|--help) usage; exit 0 ;;
        -*) echo "ab.sh: unknown argument $arg" >&2; usage >&2; exit 2 ;;
        *) BINS+=("$arg") ;;
    esac
done
if [ "${#BINS[@]}" -ne 2 ]; then usage >&2; exit 2; fi
for name in MIN_PARITY MAX_DROP; do
    value=${!name}
    if [ -n "$value" ] && ! [[ $value =~ ^[0-9]+([.][0-9]+)?$ ]]; then
        echo "ab.sh: $name wants a number of 0 or more, not $value" >&2
        exit 2
    fi
done
if [ -n "$MIN_PARITY" ] && awk "BEGIN { exit !($MIN_PARITY > 100) }"; then
    echo "ab.sh: MIN_PARITY wants a number from 0 to 100, not $MIN_PARITY" >&2
    exit 2
fi

mkdir -p "$OUT"
ids=$(python3 - "$SITES" ${FILTER[@]+"${FILTER[@]}"} <<'EOF'
import sys
path, args = sys.argv[1], sys.argv[2:]
only = {x for a in args if a.startswith("--only=") for x in a[7:].split(",") if x}
cats = {x for a in args if a.startswith("--category=") for x in a[11:].split(",") if x}
for line in open(path, encoding="utf-8"):
    if line.startswith("#") or not line.strip():
        continue
    cols = line.rstrip("\n").split("\t")
    if len(cols) < 3:
        continue
    if (only and cols[0] not in only) or (cats and cols[1] not in cats):
        continue
    print(cols[0])
EOF
)

for id in $ids; do
    echo "== $id"
    node "$HERE/chrome-capture.js" --out="$OUT" --runs="$RUNS" --visual-runs="$VISUAL_RUNS" \
        --viewport="$VIEWPORT" --only="$id" --skip-existing --sites="$SITES" ${CHROME_OPTS[@]+"${CHROME_OPTS[@]}"}
    python3 "$HERE/ns-capture.py" --bin="${BINS[0]}" --out="$OUT" --label="$BEFORE_LABEL" \
        --runs="$RUNS" --visual-runs="$VISUAL_RUNS" --viewport="$VIEWPORT" --jobs=1 --only="$id" \
        --skip-existing --sites="$SITES"
    python3 "$HERE/ns-capture.py" --bin="${BINS[1]}" --out="$OUT" --label="$AFTER_LABEL" \
        --runs="$RUNS" --visual-runs="$VISUAL_RUNS" --viewport="$VIEWPORT" --jobs=1 --only="$id" \
        --skip-existing --sites="$SITES"
done

CHECKS=()
[ -n "$MIN_PARITY" ] && CHECKS+=(--min-parity="$MIN_PARITY")
[ -n "$MAX_DROP" ] && CHECKS+=(--max-drop="$MAX_DROP")
exec python3 "$HERE/compare.py" --out="$OUT" --labels="$BEFORE_LABEL,$AFTER_LABEL" \
    --viewport="$VIEWPORT" ${CHECKS[@]+"${CHECKS[@]}"}
