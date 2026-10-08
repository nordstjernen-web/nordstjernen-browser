#!/usr/bin/env bash
# ab.sh — captures Chrome and two Nordstjernen builds site by site, back to back, and compares them.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${OUT:-$ROOT/sitebench-out/ab}
RUNS=${RUNS:-3}
VIEWPORT=${VIEWPORT:-1280x800}
BEFORE_LABEL=${BEFORE_LABEL:-before}
AFTER_LABEL=${AFTER_LABEL:-after}

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
  VIEWPORT=WxH       viewport for both browsers (default: 1280x800)
  BEFORE_LABEL=NAME  label of the first build (default: before)
  AFTER_LABEL=NAME   label of the second build (default: after)
  NS_LOCALE=NAME     locale both builds run under (default: en_US.UTF-8)
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

mkdir -p "$OUT"
ids=$(python3 - "$HERE/sites.tsv" ${FILTER[@]+"${FILTER[@]}"} <<'EOF'
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
    node "$HERE/chrome-capture.js" --out="$OUT" --runs="$RUNS" --viewport="$VIEWPORT" \
        --only="$id" --skip-existing
    python3 "$HERE/ns-capture.py" --bin="${BINS[0]}" --out="$OUT" --label="$BEFORE_LABEL" \
        --runs="$RUNS" --viewport="$VIEWPORT" --jobs=1 --only="$id" --skip-existing
    python3 "$HERE/ns-capture.py" --bin="${BINS[1]}" --out="$OUT" --label="$AFTER_LABEL" \
        --runs="$RUNS" --viewport="$VIEWPORT" --jobs=1 --only="$id" --skip-existing
done

python3 "$HERE/compare.py" --out="$OUT" --labels="$BEFORE_LABEL,$AFTER_LABEL" \
    --viewport="$VIEWPORT"
