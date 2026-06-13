#!/usr/bin/env bash
# Nordstjernen whole-suite WPT score estimator.
#
# Estimates the total number of passing testharness.js subtests across the
# entire web-platform-tests suite — the scale on which full browsers score
# in the millions — by running a deterministic random sample of tests
# through the headless --wpt mode and extrapolating mean passes per file
# to the whole population. Prints the markdown history row for the
# whole-suite table in docs/wpt-scores.md.
#
# Usage: scripts/wpt-estimate.sh --wpt-root=DIR [--sample=N] [--seed=S]
#   [--base=URL] [--https-base=URL]
# Requires a running WPT server (scripts/wpt-run.sh starts one, or run
# ./wpt serve in the checkout) and the WPT CA trusted by the system if
# https tests should count (tools/certs/cacert.pem in the checkout).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN=${NS_BIN:-$ROOT/builddir/src/gtk/nordstjernen}
export NS_ALLOW_ROOT=${NS_ALLOW_ROOT:-1}
WPT_ROOT=${NS_WPT_ROOT:-}
SAMPLE=250
SEED=nordstjernen
BASE="http://web-platform.test:8000"
HTTPS_BASE="https://web-platform.test:8443"

for arg in "$@"; do
    case "$arg" in
        --wpt-root=*)   WPT_ROOT=${arg#*=} ;;
        --sample=*)     SAMPLE=${arg#*=} ;;
        --seed=*)       SEED=${arg#*=} ;;
        --base=*)       BASE=${arg#*=} ;;
        --https-base=*) HTTPS_BASE=${arg#*=} ;;
        *) echo "error: unknown option $arg" >&2; exit 2 ;;
    esac
done
if [ -z "$WPT_ROOT" ] || [ ! -d "$WPT_ROOT" ]; then
    echo "error: --wpt-root=DIR (or NS_WPT_ROOT) must point at a web-platform-tests checkout" >&2
    exit 2
fi
if [ ! -x "$BIN" ]; then
    echo "error: browser binary not found at $BIN — run scripts/dev.sh build" >&2
    exit 1
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

(cd "$WPT_ROOT" && git ls-files '*.any.js' '*.window.js' \
    | grep -vE '/(resources|support|tools|crashtests)/') > "$work/pop.lst"
(cd "$WPT_ROOT" && git grep -l 'testharness\.js' -- '*.html' '*.htm' \
    | grep -vE '/(resources|support|tools|crashtests)/' \
    | grep -vE -- '-ref\.html$|-manual\.html$') >> "$work/pop.lst"
sort "$work/pop.lst" -o "$work/pop.lst"
pop=$(wc -l < "$work/pop.lst")
echo "population: $pop testharness test files" >&2

shuf --random-source=<(yes "$SEED") -n "$SAMPLE" "$work/pop.lst" > "$work/sample.lst"

n=0
while IFS= read -r rel; do
    n=$((n + 1))
    case "$rel" in
        *.any.js)    url="/${rel%.any.js}.any.html" ;;
        *.window.js) url="/${rel%.window.js}.window.html" ;;
        *)           url="/$rel" ;;
    esac
    case "$rel" in
        *.https.*|*.h2.*) base=$HTTPS_BASE ;;
        *)                base=$BASE ;;
    esac
    out=$(timeout 25 "$BIN" --wpt --wpt-timeout-ms=15000 "$base$url" </dev/null 2>/dev/null) || true
    summary=$(printf '%s\n' "$out" | sed -n 's/^WPT SUMMARY //p' | head -1)
    total=0; pass=0
    for kv in $summary; do
        case "$kv" in
            total=*) total=${kv#*=} ;;
            pass=*)  pass=${kv#*=} ;;
        esac
    done
    printf '%s\t%s\t%s\n' "$rel" "$total" "$pass" >> "$work/results.tsv"
    echo "[$n/$SAMPLE] pass=$pass/$total $url" >&2
done < "$work/sample.lst"

ns_rev=$(git -C "$ROOT" rev-parse --short HEAD)
wpt_rev=$(git -C "$WPT_ROOT" rev-parse --short HEAD)
date=$(date -u +%Y-%m-%d)

python3 - "$work/results.tsv" "$pop" "$ROOT/docs/wpt-scores.md" \
    "$date" "$ns_rev" "$wpt_rev" <<'EOF'
import random, sys

path, pop, doc, date, ns_rev, wpt_rev = sys.argv[1:7]
def read_doc(path):
    try:
        return open(path, encoding='utf-8').read().splitlines()
    except UnicodeDecodeError:
        return open(path, encoding='cp1252').read().splitlines()

pop = int(pop)
passes = [int(line.split('\t')[2]) for line in open(path)]
n = len(passes)
mean = sum(passes) / n
est = round(mean * pop)

rng = random.Random(0)
boots = sorted(round(sum(rng.choices(passes, k=n)) / n * pop)
               for _ in range(10000))
lo, hi = boots[249], boots[9749]

row = "| %s | %s | %s | %d of %d | ~%s (%s – %s) | ~%.1f%% |" % (
    date, ns_rev, wpt_rev, n, pop,
    format(est, ','), format(lo, ','), format(hi, ','),
    100.0 * est / 6_000_000)

lines = read_doc(doc)
wi = lines.index("## Whole-suite score")
last = None
for i in range(wi + 1, len(lines)):
    if lines[i].startswith("## "):
        break
    if lines[i].startswith("|"):
        last = i
lines.insert(last + 1, row)
with open(doc, 'w', encoding='utf-8') as f:
    f.write('\n'.join(lines) + '\n')

print(row)
print("updated: %s" % doc)
EOF
