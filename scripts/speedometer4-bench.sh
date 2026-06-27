#!/usr/bin/env bash
# Speedometer 4.0-alpha (WebKit/Speedometer main) per-workload driver for
# Nordstjernen, adapted from scripts/speedometer-bench.sh for the v4 module
# layout (default-tests.mjs / shared/translations.mjs / freezeSuites).
set -euo pipefail
export LC_NUMERIC=C
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN=${NS_BIN:-$ROOT/builddir/src/gtk/nordstjernen}
export NS_ALLOW_ROOT=${NS_ALLOW_ROOT:-1}
WORK=${NS_SPEEDOMETER_DIR:-${TMPDIR:-/tmp}/nd-spdm-main}
PORT=${NS_PORT:-8133}
ITERS=${NS_ITERS:-3}
SETTLE=${NS_SETTLE:-8000}
REPO=${NS_SPEEDOMETER_REPO:-https://github.com/WebKit/Speedometer.git}
FILTER=${1:-}

[ -x "$BIN" ] || { echo "error: browser binary not found at $BIN — run scripts/dev.sh build" >&2; exit 1; }
if [ ! -d "$WORK/resources" ]; then
    echo "Fetching Speedometer main (4.0-alpha) into $WORK ..."
    rm -rf "$WORK"
    git clone --depth 1 --branch main "$REPO" "$WORK"
fi

python3 - "$WORK" <<'PY'
import re, sys, glob
work = sys.argv[1]
tr = open(f'{work}/resources/shared/translations.mjs', encoding='utf-8').read()
tr = re.sub(r'^\s*import .*?;\s*$', '', tr, flags=re.M)
tr = re.sub(r'^\s*export\s+', '', tr, flags=re.M)
tests = open(f'{work}/resources/default-tests.mjs', encoding='utf-8').read()
tests = re.sub(r'^\s*import .*?;\s*$', '', tests, flags=re.M)
tests = re.sub(r'^\s*export\s+', '', tests, flags=re.M)
tests = tests.replace('DefaultSuites', 'Suites')

harness = r'''
"use strict";
var params = { warmupBeforeSync: 0 };
var numberOfItemsToAdd = 100;
function freezeSuites(s){ return s; }
function NDLOG(s){ try { console.log(s); } catch(e){} }
class BenchmarkTestStep { constructor(name, run){ this.name = name; this.run = run; } }
function getParent(start, path){
    let root = start;
    for (const sel of path){ const n = root.querySelector(sel); root = (n && n.shadowRoot) ? n.shadowRoot : n; }
    return root;
}
const NATIVE_OPTIONS = { bubbles: true, cancellable: true };
class PageElement {
    constructor(node){ this._node = node; }
    setValue(v){ this._node.value = v; }
    click(){ this._node.click(); }
    focus(){ this._node.focus(); }
    getElementByMethod(name){ return new PageElement(this._node[name]()); }
    dispatchEvent(name, options = NATIVE_OPTIONS, type = Event){
        if (name === "submit") this._dispatchSubmitEvent();
        else this._node.dispatchEvent(new type(name, options));
    }
    _dispatchSubmitEvent(){ const e = document.createEvent("Event"); e.initEvent("submit", true, true); this._node.dispatchEvent(e); }
    enter(type, options){ return this.dispatchKeyEvent(type, 13, "Enter", options); }
    dispatchKeyEvent(type, keyCode, key, options){
        let o = { bubbles: true, cancelable: true, keyCode, which: keyCode, key };
        if (options !== undefined) o = Object.assign(o, options);
        this._node.dispatchEvent(new KeyboardEvent(type, o));
    }
    dispatchMouseEvent(type, ox, oy, options){
        const r = this._node.getBoundingClientRect(), cw = this._node.ownerDocument.defaultView;
        const cx = ox + r.x, cy = oy + r.y;
        let o = { bubbles: true, cancelable: true, clientX: cx, clientY: cy, screenX: cx + cw.screenX, screenY: cy + cw.screenY };
        if (options !== undefined) o = Object.assign(o, options);
        this._node.dispatchEvent(new cw.MouseEvent(type, o));
    }
    querySelectorInShadowRoot(sel, path = []){
        const start = this._node.shadowRoot ? this._node.shadowRoot : this._node;
        const el = getParent(start, path).querySelector(sel);
        return el === null ? null : new PageElement(el);
    }
    querySelector(sel){ const el = this._node.querySelector(sel); return el === null ? null : new PageElement(el); }
    querySelectorAll(sel){ return Array.from(this._node.querySelectorAll(sel)).map((e) => new PageElement(e)); }
}
class Page {
    layoutFlush(){ return (document.body || document.documentElement).getBoundingClientRect().height; }
    waitForElement(sel, path = []){
        return new Promise((resolve) => {
            let tries = 0;
            const poll = () => {
                const el = getParent(document, path).querySelector(sel);
                if (el) { resolve(new PageElement(el)); return; }
                if (++tries > 1500) { resolve(null); return; }
                requestAnimationFrame(poll);
            };
            poll();
        });
    }
    querySelector(sel, path = []){ const el = getParent(document, path).querySelector(sel); return el === null ? null : new PageElement(el); }
    querySelectorAll(sel, path = []){ return Array.from(getParent(document, path).querySelectorAll(sel)).map((e) => new PageElement(e)); }
    getElementById(id){ const el = document.getElementById(id); return el === null ? null : new PageElement(el); }
}
'''

bootstrap = r'''
const page = new Page();
function runStep(test){
    return new Promise((resolve) => {
        let syncTime, asyncStart, asyncTime;
        const runSync = () => { const t0 = performance.now(); test.run(page); syncTime = performance.now() - t0; asyncStart = performance.now(); };
        const measureAsync = () => { const h = page.layoutFlush(); asyncTime = performance.now() - asyncStart; window._unusedHeightValue = h; };
        requestAnimationFrame(() => runSync());
        requestAnimationFrame(() => { setTimeout(() => { measureAsync(); setTimeout(() => resolve({ name: test.name, sync: syncTime, async: asyncTime, total: syncTime + asyncTime }), 0); }, 0); });
    });
}
async function ndMain(){
    const here = location.pathname;
    const norm = (u) => u.split('#')[0].split('?')[0];
    const suite = Suites.find((s) => here.endsWith(norm(s.url)));
    if (!suite){ NDLOG("NS_BENCH ERROR no-suite-match " + here); return; }
    NDLOG("NS_BENCH SUITE " + suite.name);
    try { await suite.prepare(page); }
    catch (e){ NDLOG("NS_BENCH ERROR prepare " + (e && e.message ? e.message : e)); return; }
    let suiteTotal = 0; const parts = [];
    for (const test of suite.tests){
        let r;
        try { r = await runStep(test); }
        catch (e){ NDLOG("NS_BENCH ERROR step " + test.name + " " + (e && e.message ? e.message : e)); return; }
        suiteTotal += r.total;
        parts.push(test.name + "=" + r.total.toFixed(2) + "(s" + r.sync.toFixed(2) + "/a" + r.async.toFixed(2) + ")");
    }
    NDLOG("NS_BENCH DONE " + suite.name + " | total=" + suiteTotal.toFixed(2) + " | " + parts.join(" ") + " | domNodes=" + document.getElementsByTagName('*').length);
}
if (document.readyState === "complete" || document.readyState === "interactive") requestAnimationFrame(() => ndMain());
else window.addEventListener("DOMContentLoaded", () => requestAnimationFrame(() => ndMain()));
'''

out = "(function(){\n" + harness + "\n/* translations */\n" + tr + "\n/* suites */\n" + tests + "\n/* bootstrap */\n" + bootstrap + "\n})();\n"
open(f'{work}/nd-driver.js', 'w', encoding='utf-8').write(out)

tag = '<script src="/nd-driver.js"></script>'
n = 0
for p in glob.glob(f'{work}/resources/todomvc/**/dist/index.html', recursive=True):
    h = open(p, encoding='utf-8').read()
    if 'nd-driver.js' in h: continue
    h = h.replace('</body>', tag + '</body>', 1) if '</body>' in h else h + tag
    open(p, 'w', encoding='utf-8').write(h)
    n += 1
print(f"  driver injected into {n} suite pages")
PY

cd "$WORK"
python3 -m http.server "$PORT" >/dev/null 2>&1 &
HTTPD=$!
trap 'kill "$HTTPD" 2>/dev/null || true' EXIT
sleep 1

mapfile -t PATHS < <(cd "$WORK" && ls resources/todomvc/*/*/dist/index.html | sed 's#resources/todomvc/##')
printf "%-44s %10s %10s %10s %10s %8s\n" "SUITE" "total(ms)" "add" "complete" "delete" "dom"
for p in "${PATHS[@]}"; do
    [ -n "$FILTER" ] && case "$p" in *"$FILTER"*) ;; *) continue ;; esac
    url="http://localhost:$PORT/resources/todomvc/$p"
    case "$p" in *react*|*preact*) url="$url#/" ;; esac
    tots=(); add=""; comp=""; del=""; dom=""; name=""
    for _ in $(seq 1 "$ITERS"); do
        line=$(NS_ALLOW_ROOT=1 "$BIN" --headless --dump=none --settle-ms="$SETTLE" "$url" 2>&1 \
               | grep -oE "NS_BENCH DONE.*" | head -1 || true)
        [ -z "$line" ] && continue
        name=$(echo "$line" | sed -E 's/NS_BENCH DONE ([^ ]+) .*/\1/')
        tots+=("$(echo "$line" | grep -oE 'total=[0-9.]+' | head -1 | cut -d= -f2)")
        add=$(echo "$line"  | grep -oE 'Adding[0-9]+Items=[0-9.]+' | grep -oE '[0-9.]+$')
        comp=$(echo "$line" | grep -oE 'CompletingAllItems=[0-9.]+' | grep -oE '[0-9.]+$')
        del=$(echo "$line"  | grep -oE 'DeletingAllItems=[0-9.]+'   | grep -oE '[0-9.]+$')
        dom=$(echo "$line"  | grep -oE 'domNodes=[0-9]+' | cut -d= -f2)
    done
    if [ "${#tots[@]}" -gt 0 ]; then
        med=$(printf "%s\n" "${tots[@]}" | sort -n | awk '{a[NR]=$1} END{print (NR%2)?a[(NR+1)/2]:(a[NR/2]+a[NR/2+1])/2}')
        printf "%-44s %10.1f %10.1f %10.1f %10.1f %8s\n" "${name}" "$med" "${add:-0}" "${comp:-0}" "${del:-0}" "${dom:-?}"
    else
        short=$(echo "$p" | sed -E 's#/dist/index.html##; s#.*/##')
        printf "%-44s %10s   (does not load)\n" "$short" "n/a"
    fi
done
