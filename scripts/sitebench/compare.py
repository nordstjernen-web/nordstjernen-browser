#!/usr/bin/env python3
"""compare.py — scores Nordstjernen captures against the Chrome baseline and writes an HTML + Markdown report."""

import argparse
import html
import json
import math
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

try:
    import numpy as np
    from PIL import Image
except ImportError:
    sys.exit("compare: needs Pillow and numpy (pip install pillow numpy)")

CHALLENGE = re.compile(
    r"just a moment|attention required|access denied|403 forbidden|confirm you are human|"
    r"security verification|are you a robot|robot check|pardon our interruption|request blocked|"
    r"verify you are human|you've been blocked|blocked by network security|captcha|"
    r"click the button below to continue", re.I)
ERROR_PAGE = re.compile(r"^something went wrong|^sorry, something went wrong|^an error occurred", re.I)

WEIGHTS = {"ssim": 0.30, "hist": 0.15, "layout": 0.20, "components": 0.25, "text": 0.10}
FILM_MS = [500, 1000, 2000, 3000, 5000]


def load_json(path):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def load_rgb(path, size=None):
    try:
        with Image.open(path) as im:
            im = im.convert("RGB")
            if size:
                im = im.resize(size, Image.BILINEAR)
            return np.asarray(im, dtype=np.float64)
    except OSError:
        return None


def box_mean(x, k):
    pad = np.pad(x, ((1, 0), (1, 0)))
    c = pad.cumsum(0).cumsum(1)
    return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)


def ssim(a, b, k=8):
    ga = a @ [0.299, 0.587, 0.114]
    gb = b @ [0.299, 0.587, 0.114]
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    ma, mb = box_mean(ga, k), box_mean(gb, k)
    va = box_mean(ga * ga, k) - ma * ma
    vb = box_mean(gb * gb, k) - mb * mb
    cov = box_mean(ga * gb, k) - ma * mb
    s = ((2 * ma * mb + c1) * (2 * cov + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2))
    return float(np.clip(s.mean(), 0, 1))


def histogram(a, bins=4):
    q = np.clip((a / 256.0 * bins).astype(int), 0, bins - 1)
    idx = q[..., 0] * bins * bins + q[..., 1] * bins + q[..., 2]
    h = np.bincount(idx.ravel(), minlength=bins ** 3).astype(np.float64)
    return h / max(h.sum(), 1)


def hist_similarity(a, b):
    return float(np.minimum(histogram(a), histogram(b)).sum())


def content_mask(a):
    flat = a.reshape(-1, 3).astype(int)
    keys = (flat[:, 0] // 8) * 1024 + (flat[:, 1] // 8) * 32 + flat[:, 2] // 8
    bg_key = np.bincount(keys).argmax()
    bg = np.array([(bg_key // 1024) * 8 + 4, ((bg_key // 32) % 32) * 8 + 4, (bg_key % 32) * 8 + 4])
    return np.abs(a - bg).sum(axis=2) > 36


def layout_iou(a, b):
    ma, mb = content_mask(a), content_mask(b)
    union = np.logical_or(ma, mb).sum()
    if union == 0:
        return 1.0
    return float(np.logical_and(ma, mb).sum() / union)


def diff_heatmap(a, b):
    d = np.abs(a - b).sum(axis=2) / 3.0
    d = np.clip(d * 2.0, 0, 255)
    base = (a @ [0.299, 0.587, 0.114]) * 0.35 + 160
    out = np.stack([np.maximum(base, d), base * (1 - d / 255.0), base * (1 - d / 255.0)], axis=2)
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))


def speed_index(frames_dir):
    index = load_json(Path(frames_dir) / "index.json")
    if not index or len(index) < 2:
        return None, None
    hists = []
    for f in index:
        img = load_rgb(Path(frames_dir) / f["file"], (160, 100))
        if img is None:
            return None, None
        hists.append((f["ms"], histogram(img, 8)))
    final = hists[-1][1]
    start = np.zeros_like(final)
    start[-1] = 1.0
    total = np.abs(start - final).sum()
    si = 0.0
    visually_complete = None
    prev_t, prev_p = 0, 0.0
    for t, h in hists:
        p = 1.0 - np.abs(h - final).sum() / total if total > 0 else 1.0
        p = float(np.clip(p, 0, 1))
        si += (1 - prev_p) * (t - prev_t)
        prev_t, prev_p = t, p
        if visually_complete is None and p >= 0.98:
            visually_complete = t
    return round(si), visually_complete


def blocked_reason(probe, status):
    if not probe:
        return None
    first_text = " ".join(c.get("text") or "" for c in (probe.get("components") or [])[:12])
    if CHALLENGE.search(probe.get("title") or "") or CHALLENGE.search(first_text):
        return "bot challenge"
    if any(ERROR_PAGE.search(c.get("text") or "") for c in (probe.get("components") or [])[:6]):
        return "error"
    if status in (401, 403, 429) and (probe.get("nodes") or 0) < 200:
        return f"HTTP {status}"
    return None


def identifiable(c):
    return bool(c.get("text") or c.get("id"))


def component_key(c):
    return (c["tag"], re.sub(r"\d+", "#", c.get("text") or ""), c.get("id") or "")


def component_scores(chrome_probe, ns_probe):
    if not chrome_probe or not ns_probe:
        return None
    vh = chrome_probe.get("vh") or 800
    base = [c for c in chrome_probe.get("components", [])
            if identifiable(c) and c["y"] < vh and c["w"] * c["h"] >= 16]
    if not base:
        return None
    cand = {}
    for c in ns_probe.get("components", []):
        cand.setdefault(component_key(c), []).append(c)
    found = placed = 0
    style_checks = style_ok = 0
    offsets = []
    style_diffs = []
    for c in base:
        options = cand.get(component_key(c))
        if not options:
            offsets.append({"tag": c["tag"], "text": c.get("text", "")[:40], "missing": True,
                            "chrome": [c["x"], c["y"], c["w"], c["h"]]})
            continue
        found += 1
        best = max(options, key=lambda o: rect_iou(c, o))
        iou = rect_iou(c, best)
        if iou >= 0.5:
            placed += 1
        offsets.append({"tag": c["tag"], "text": c.get("text", "")[:40], "iou": round(iou, 2),
                        "dx": best["x"] - c["x"], "dy": best["y"] - c["y"],
                        "dw": best["w"] - c["w"], "dh": best["h"] - c["h"],
                        "chrome": [c["x"], c["y"], c["w"], c["h"]],
                        "ns": [best["x"], best["y"], best["w"], best["h"]]})
        if c.get("text"):
            for key in ("fs", "fw", "color", "bg", "ff"):
                style_checks += 1
                if style_equal(key, c.get(key), best.get(key)):
                    style_ok += 1
                elif len(style_diffs) < 60:
                    style_diffs.append({"tag": c["tag"], "text": c.get("text", "")[:30], "prop": key,
                                        "chrome": c.get(key), "ns": best.get(key)})
    offsets.sort(key=lambda o: (0 if o.get("missing") else 1, -(abs(o.get("dy", 0)) + abs(o.get("dx", 0)))))
    return {
        "count": len(base), "found": found, "placed": placed,
        "foundRate": found / len(base), "placedRate": placed / len(base),
        "styleRate": style_ok / style_checks if style_checks else None,
        "worst": offsets[:15], "styleDiffs": style_diffs[:25],
    }


def rect_iou(a, b):
    x1, y1 = max(a["x"], b["x"]), max(a["y"], b["y"])
    x2 = min(a["x"] + a["w"], b["x"] + b["w"])
    y2 = min(a["y"] + a["h"], b["y"] + b["h"])
    inter = max(0, x2 - x1) * max(0, y2 - y1)
    union = a["w"] * a["h"] + b["w"] * b["h"] - inter
    return inter / union if union > 0 else 0.0


def style_equal(key, a, b):
    if a is None or b is None:
        return a == b
    if key == "fs":
        return abs(float(a) - float(b)) <= 0.5
    if key == "ff":
        return str(a).strip().lower() == str(b).strip().lower()
    if key in ("color", "bg"):
        return normalize_color(a) == normalize_color(b)
    if key == "fw":
        return {"normal": "400", "bold": "700"}.get(str(a), str(a)) == {"normal": "400", "bold": "700"}.get(str(b), str(b))
    return a == b


def normalize_color(c):
    s = str(c).replace(" ", "").lower()
    if s in ("transparent", "rgba(0,0,0,0)"):
        return "transparent"
    if s.startswith("rgba(") and s.endswith(",1)"):
        s = "rgb(" + s[5:-3] + ")"
    return s


def ratio(a, b):
    if not isinstance(a, (int, float)) or not isinstance(b, (int, float)) or a <= 0 or b <= 0:
        return None
    return a / b


def closeness(a, b):
    if not a or not b:
        return None
    return min(a, b) / max(a, b)


def parity(v):
    parts = {"ssim": v.get("ssim"), "hist": v.get("hist"), "layout": v.get("layout"),
             "components": (v.get("components") or {}).get("placedRate"), "text": v.get("textRatio")}
    total = sum(WEIGHTS[k] for k, x in parts.items() if x is not None)
    if total == 0:
        return None
    return round(100 * sum(WEIGHTS[k] * x for k, x in parts.items() if x is not None) / total, 1)


def save_jpeg(arr_or_img, path, size=None, quality=80):
    img = arr_or_img if isinstance(arr_or_img, Image.Image) else Image.fromarray(arr_or_img.astype(np.uint8))
    if size:
        img = img.resize(size, Image.BILINEAR)
    img.save(path, "JPEG", quality=quality)


def full_thumb(src, dest, width=320, max_h=2400):
    try:
        with Image.open(src) as im:
            im = im.convert("RGB")
            h = int(im.height * width / im.width)
            im = im.resize((width, h), Image.BILINEAR)
            im.crop((0, 0, width, min(h, max_h))).save(dest, "JPEG", quality=75)
            return True
    except OSError:
        return False


def analyse_site(site_id, out, base_label, labels, img_dir, vw, vh):
    chrome_dir = out / base_label / site_id
    chrome = load_json(chrome_dir / "metrics.json")
    chrome_probe = load_json(chrome_dir / "probe.json")
    row = {"id": site_id, "chrome": None, "engines": {}}
    if chrome:
        cs = chrome["summary"]
        si, vc = speed_index(chrome_dir / "frames")
        row["site"] = chrome["site"]
        row["chrome"] = dict(cs, status=chrome.get("status"), error=chrome.get("error"),
                             speedIndex=si, visuallyComplete=vc,
                             docH=(chrome_probe or {}).get("docH"),
                             textLen=(chrome_probe or {}).get("textLen"),
                             blocked=blocked_reason(chrome_probe, chrome.get("status")))
    size = (vw // 2, vh // 2)
    chrome_img = load_rgb(chrome_dir / "viewport.png", size)
    if chrome_img is not None:
        save_jpeg(chrome_img, img_dir / f"{site_id}-{base_label}.jpg")
        full_thumb(chrome_dir / "full.png", img_dir / f"{site_id}-{base_label}-full.jpg")
    film = []
    index = load_json(chrome_dir / "frames" / "index.json") or []
    for ms in FILM_MS:
        pick = [f for f in index if f["ms"] <= ms]
        if pick:
            dest = img_dir / f"{site_id}-{base_label}-film-{ms}.jpg"
            img = load_rgb(chrome_dir / "frames" / pick[-1]["file"], (vw // 4, vh // 4))
            if img is not None:
                save_jpeg(img, dest)
                film.append({"ms": ms, "file": dest.name})
    row["film"] = film

    for label in labels:
        ns_dir = out / label / site_id
        ns = load_json(ns_dir / "metrics.json")
        if not ns:
            continue
        if "site" not in row:
            row["site"] = ns["site"]
        ns_probe = load_json(ns_dir / "probe.json")
        s = dict(ns["summary"])
        vt = ns.get("visualTiming") or {}
        if isinstance(vt.get("cpu_ms"), (int, float)):
            s["settledMainThreadMs"] = round(max(vt["cpu_ms"] - (vt.get("encode_ms") or 0), 0), 1)
        entry = dict(s, status=ns.get("status"), error=ns.get("error"),
                     docH=(ns_probe or {}).get("docH"), textLen=(ns_probe or {}).get("textLen"),
                     jsErrorSample=ns.get("jsErrorSample", []),
                     blocked=blocked_reason(ns_probe, ns.get("status")))
        ns_img = load_rgb(ns_dir / "viewport.png", size)
        visual = {}
        if ns_img is not None:
            save_jpeg(ns_img, img_dir / f"{site_id}-{label}.jpg")
            full_thumb(ns_dir / "full.png", img_dir / f"{site_id}-{label}-full.jpg")
            initial = ns_dir / "full-initial.png"
            if initial.exists():
                with Image.open(initial) as im:
                    im = im.convert("RGB").crop((0, 0, vw, vh)).resize((vw // 4, vh // 4), Image.BILINEAR)
                    im.save(img_dir / f"{site_id}-{label}-initial.jpg", "JPEG", quality=75)
        if ns_img is not None and chrome_img is not None:
            visual["ssim"] = round(ssim(chrome_img, ns_img), 3)
            visual["hist"] = round(hist_similarity(chrome_img, ns_img), 3)
            visual["layout"] = round(layout_iou(chrome_img, ns_img), 3)
            diff_heatmap(chrome_img, ns_img).save(img_dir / f"{site_id}-{label}-diff.jpg", "JPEG", quality=75)
        visual["components"] = component_scores(chrome_probe, ns_probe)
        visual["textRatio"] = closeness((chrome_probe or {}).get("textLen"), (ns_probe or {}).get("textLen"))
        visual["heightRatio"] = closeness((chrome_probe or {}).get("docH"), (ns_probe or {}).get("docH"))
        visual["parity"] = parity(visual) if ns_img is not None else 0.0
        entry["visual"] = visual
        c = row["chrome"] or {}
        first_paint = s.get("firstPaintMs") or s.get("firstRenderMs")
        entry["firstPaintMs"] = first_paint
        entry["vsChrome"] = {
            "firstRender": ratio(first_paint, c.get("fcp")),
            "imagesLoaded": ratio(s.get("firstRenderMs"), c.get("load")),
            "mainThread": ratio(s.get("settledMainThreadMs"), c.get("mainThreadMs")),
            "memory": ratio(s.get("settledMaxRssMb"), c.get("browserRssMb")),
        }
        row["engines"][label] = entry
    return row


def geomean(xs):
    xs = [x for x in xs if isinstance(x, (int, float)) and x > 0]
    if not xs:
        return None
    return math.exp(sum(math.log(x) for x in xs) / len(xs))


def med(xs):
    xs = sorted(x for x in xs if isinstance(x, (int, float)))
    if not xs:
        return None
    m = len(xs) // 2
    return xs[m] if len(xs) % 2 else (xs[m - 1] + xs[m]) / 2


def comparable(row):
    return bool(row.get("chrome")) and not row["chrome"].get("blocked")


def aggregate(rows, labels):
    agg = {}
    blocked = [r["id"] for r in rows if not comparable(r)]
    rows = [r for r in rows if comparable(r)]
    ns_blocked = [r["id"] for r in rows
                  if any((r["engines"].get(l) or {}).get("blocked") for l in labels)]
    common = [r for r in rows if r["id"] not in ns_blocked]
    for label in labels:
        es = [r["engines"][label] for r in common if label in r["engines"]]
        ok = [e for e in es if not e.get("error")]
        agg[label] = {
            "sites": len(es), "loaded": len(ok),
            "parityMean": round(sum(e["visual"]["parity"] or 0 for e in es) / len(es), 1) if es else None,
            "ssimMedian": med([e["visual"].get("ssim") for e in es]),
            "componentsPlacedMedian": med([(e["visual"].get("components") or {}).get("placedRate") for e in es]),
            "firstRenderMedianMs": med([e.get("firstPaintMs") for e in ok]),
            "imagesLoadedMedianMs": med([e.get("firstRenderMs") for e in ok]),
            "imagesLoadedVsLoadGeomean": geomean([e["vsChrome"]["imagesLoaded"] for e in ok]),
            "firstRenderVsFcpGeomean": geomean([e["vsChrome"]["firstRender"] for e in ok]),
            "mainThreadVsChromeGeomean": geomean([e["vsChrome"]["mainThread"] for e in ok]),
            "memoryVsChromeGeomean": geomean([e["vsChrome"]["memory"] for e in ok]),
            "fasterFirstRender": sum(1 for e in ok if (e["vsChrome"]["firstRender"] or 9) < 1.0),
            "jsErrors": sum(e.get("jsErrors") or 0 for e in es),
            "nsBlocked": sum(1 for r in rows if (r["engines"].get(label) or {}).get("blocked")),
        }
    chrome = [r["chrome"] for r in rows if r.get("chrome")]
    agg["chrome"] = {
        "sites": len(chrome),
        "excluded": blocked,
        "nsExcluded": ns_blocked,
        "fcpMedianMs": med([c.get("fcp") for c in chrome]),
        "lcpMedianMs": med([c.get("lcp") for c in chrome]),
        "loadMedianMs": med([c.get("load") for c in chrome]),
        "mainThreadMedianMs": med([c.get("mainThreadMs") for c in chrome]),
    }
    return agg


def fmt(v, digits=0, suffix=""):
    if v is None:
        return "–"
    if isinstance(v, float):
        v = round(v, digits)
        if digits == 0:
            v = int(v)
    return f"{v}{suffix}"


def fmt_ratio(v):
    return "–" if v is None else f"{v:.2f}×"


def write_markdown(rows, agg, labels, base_label, meta, path):
    lines = ["# Site benchmark: Nordstjernen vs Chrome", ""]
    lines.append(f"Chrome {meta.get('chromeVersion', '?')} · viewport {meta['viewport']} · "
                 f"{len(rows)} sites · generated {meta['generated']}")
    lines.append("")
    lines.append("Parity is a 0–100 visual similarity score against Chrome "
                 "(SSIM, colour histogram, content layout, component placement, text). "
                 "Time ratios are Nordstjernen ÷ Chrome; below 1.00× means Nordstjernen is faster.")
    lines.append("")
    lines.append("| | " + " | ".join(labels) + " |")
    lines.append("|---|" + "---|" * len(labels))
    keys = [("Sites loaded", "loaded", lambda v: fmt(v)),
            ("Mean visual parity", "parityMean", lambda v: fmt(v, 1)),
            ("Median viewport SSIM", "ssimMedian", lambda v: fmt(v, 3)),
            ("Median components placed", "componentsPlacedMedian",
             lambda v: "–" if v is None else f"{v * 100:.0f}%"),
            ("Median first paint (ms)", "firstRenderMedianMs", lambda v: fmt(v)),
            ("First paint ÷ Chrome FCP (geomean)", "firstRenderVsFcpGeomean", fmt_ratio),
            ("Sites painting before Chrome FCP", "fasterFirstRender", lambda v: fmt(v)),
            ("Images loaded ÷ Chrome load event (geomean)", "imagesLoadedVsLoadGeomean", fmt_ratio),
            ("Main-thread CPU ÷ Chrome (geomean)", "mainThreadVsChromeGeomean", fmt_ratio),
            ("Peak memory ÷ Chrome (geomean)", "memoryVsChromeGeomean", fmt_ratio),
            ("JS errors (all sites)", "jsErrors", lambda v: fmt(v)),
            ("Sites showing a bot challenge", "nsBlocked", lambda v: fmt(v))]
    for title, key, f in keys:
        lines.append(f"| {title} | " + " | ".join(f(agg[l].get(key)) for l in labels) + " |")
    c = agg["chrome"]
    lines.append("")
    lines.append(f"Chrome medians: FCP {fmt(c['fcpMedianMs'])} ms, LCP {fmt(c['lcpMedianMs'])} ms, "
                 f"load {fmt(c['loadMedianMs'])} ms, main thread {fmt(c['mainThreadMedianMs'])} ms.")
    if c.get("excluded"):
        lines.append("")
        lines.append("Left out of the aggregates because headless Chrome was shown a bot challenge or "
                     "an error page instead of the site: " + ", ".join(c["excluded"]) + ".")
    if c.get("nsExcluded"):
        lines.append("")
        lines.append("Also left out, so every column averages the same sites, because at least one "
                     "Nordstjernen run was shown a bot challenge or an error page: "
                     + ", ".join(c["nsExcluded"]) + ".")
    lines.append("")
    head = "| Site | Chrome FCP | Chrome main | " + " | ".join(
        f"{l} parity | {l} first paint | {l} main CPU" for l in labels) + " |"
    lines.append(head)
    lines.append("|---|---:|---:|" + "---:|---:|---:|" * len(labels))
    for r in rows:
        ch = r.get("chrome") or {}
        site_id = r["id"] + (" (Chrome blocked)" if ch.get("blocked") else "")
        cells = [site_id, fmt(ch.get("fcp")), fmt(ch.get("mainThreadMs"))]
        for l in labels:
            e = r["engines"].get(l)
            if not e:
                cells += ["–", "–", "–"]
                continue
            mark = " ⚠" if e.get("error") or e.get("blocked") else ""
            cells += [fmt(e["visual"].get("parity"), 1) + mark,
                      fmt(e.get("firstPaintMs")), fmt(e.get("settledMainThreadMs"))]
        lines.append("| " + " | ".join(cells) + " |")
    lines.append("")
    Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8", errors="replace")


CSS = """
:root{--bg:#fbfaf7;--fg:#1d1d1b;--muted:#6b6a65;--line:#e4e1d8;--card:#fff;--good:#1f7a3f;--bad:#b3261e;--accent:#2d5bd7}
@media (prefers-color-scheme:dark){:root{--bg:#1b1b1a;--fg:#ecebe6;--muted:#a3a29b;--line:#3a3935;--card:#242422;--good:#5cc184;--bad:#ff8a80;--accent:#8fb0ff}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}
main{max-width:1500px;margin:0 auto;padding:24px 16px 80px}h1{font-size:22px;margin:0 0 4px}h2{font-size:18px;margin:32px 0 8px}
.muted{color:var(--muted)}table{border-collapse:collapse;width:100%;background:var(--card)}
th,td{border-bottom:1px solid var(--line);padding:5px 8px;text-align:right;white-space:nowrap}
th:first-child,td:first-child{text-align:left}th{cursor:pointer;font-weight:600;position:sticky;top:0;background:var(--card)}
.wrap{overflow-x:auto;border:1px solid var(--line);border-radius:8px}
.good{color:var(--good)}.bad{color:var(--bad)}
.site{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px;margin:18px 0}
.shots{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:10px}
.shots figure{margin:0}.shots img{width:100%;border:1px solid var(--line);border-radius:4px}
figcaption{font-size:12px;color:var(--muted)}.film{display:flex;gap:6px;flex-wrap:wrap}.film img{width:160px;border:1px solid var(--line)}
.fulls{display:flex;gap:10px;overflow-x:auto}.fulls img{width:220px;border:1px solid var(--line)}
details{margin-top:8px}code{font-size:12px}a{color:var(--accent)}
"""

SORT_JS = """
document.querySelectorAll('th').forEach(function(th){th.addEventListener('click',function(){
var t=th.closest('table'),i=[].indexOf.call(th.parentNode.children,th),b=t.tBodies[0],r=[].slice.call(b.rows),
d=th.dataset.d=th.dataset.d==='a'?'d':'a';r.sort(function(x,y){var p=x.cells[i].dataset.v||x.cells[i].textContent,
q=y.cells[i].dataset.v||y.cells[i].textContent,m=parseFloat(p),n=parseFloat(q);var c=isNaN(m)||isNaN(n)?p.localeCompare(q):m-n;
return d==='a'?c:-c});r.forEach(function(x){b.appendChild(x)})})});
"""


def cell(v, digits=0, cls=""):
    num = "" if v is None else f' data-v="{v}"'
    return f'<td{num} class="{cls}">{html.escape(fmt(v, digits))}</td>'


def ratio_cell(v):
    if v is None:
        return '<td data-v="">–</td>'
    cls = "good" if v < 1 else "bad"
    return f'<td data-v="{v:.4f}" class="{cls}">{v:.2f}×</td>'


def write_html(rows, agg, labels, base_label, meta, path):
    e = html.escape
    out = [f"<!doctype html><html lang=en><head><meta charset=utf-8>"
           f"<meta name=viewport content='width=device-width,initial-scale=1'>"
           f"<title>Site Benchmark Report</title><style>{CSS}</style></head><body><main>"]
    out.append("<h1>Nordstjernen vs Chrome — site benchmark</h1>")
    out.append(f"<p class=muted>{e(meta.get('chromeVersion', ''))} · {e(', '.join(meta.get('nsVersions', [])))} · "
               f"viewport {e(meta['viewport'])} · {len(rows)} sites · {e(meta['generated'])}</p>")
    out.append("<h2>Summary</h2><div class=wrap><table><thead><tr><th>Metric</th>" +
               "".join(f"<th>{e(l)}</th>" for l in labels) + "</tr></thead><tbody>")
    for title, key, digits in [("Sites loaded", "loaded", 0), ("Mean visual parity", "parityMean", 1),
                               ("Median SSIM", "ssimMedian", 3), ("Median first paint ms", "firstRenderMedianMs", 0),
                               ("First paint ÷ Chrome FCP", "firstRenderVsFcpGeomean", 2),
                               ("Images loaded ÷ Chrome load", "imagesLoadedVsLoadGeomean", 2),
                               ("Main-thread CPU ÷ Chrome", "mainThreadVsChromeGeomean", 2),
                               ("Peak memory ÷ Chrome", "memoryVsChromeGeomean", 2),
                               ("Sites painting before Chrome FCP", "fasterFirstRender", 0),
                               ("JS errors", "jsErrors", 0)]:
        out.append(f"<tr><td>{e(title)}</td>" + "".join(cell(agg[l].get(key), digits) for l in labels) + "</tr>")
    out.append("</tbody></table></div>")
    c = agg["chrome"]
    if c.get("excluded"):
        out.append("<p class=muted>Left out of the summary because headless Chrome was shown a bot challenge "
                   "or an error page: " + e(", ".join(c["excluded"])) + ".</p>")
    if c.get("nsExcluded"):
        out.append("<p class=muted>Also left out, so every column averages the same sites, because at least "
                   "one Nordstjernen run was shown a bot challenge or an error page: "
                   + e(", ".join(c["nsExcluded"])) + ".</p>")

    out.append("<h2>Per site</h2><div class=wrap><table><thead><tr><th>Site</th><th>Cat</th>"
               "<th>Chrome FCP</th><th>LCP</th><th>Load</th><th>Speed idx</th><th>Main ms</th><th>RSS MB</th>")
    for l in labels:
        out.append(f"<th>{e(l)} parity</th><th>SSIM</th><th>Placed</th><th>First paint</th>"
                   f"<th>÷FCP</th><th>Main CPU</th><th>÷Chrome</th><th>RSS MB</th><th>JS err</th>")
    out.append("</tr></thead><tbody>")
    for r in rows:
        ch = r.get("chrome") or {}
        site = r.get("site") or {}
        out.append(f"<tr><td><a href='#{e(r['id'])}'>{e(r['id'])}</a></td><td>{e(site.get('category', ''))}</td>")
        out.append(cell(ch.get("fcp")) + cell(ch.get("lcp")) + cell(ch.get("load")) + cell(ch.get("speedIndex")) +
                   cell(ch.get("mainThreadMs")) + cell(ch.get("browserRssMb")))
        for l in labels:
            en = r["engines"].get(l)
            if not en:
                out.append("<td>–</td>" * 9)
                continue
            v = en["visual"]
            comp = v.get("components") or {}
            placed = comp.get("placedRate")
            out.append(cell(v.get("parity"), 1, "bad" if en.get("error") else "") + cell(v.get("ssim"), 3) +
                       cell(None if placed is None else round(placed * 100), 0) +
                       cell(en.get("firstPaintMs")) + ratio_cell(en["vsChrome"]["firstRender"]) +
                       cell(en.get("settledMainThreadMs")) + ratio_cell(en["vsChrome"]["mainThread"]) +
                       cell(en.get("settledMaxRssMb")) + cell(en.get("jsErrors")))
        out.append("</tr>")
    out.append("</tbody></table></div>")

    for r in rows:
        ch = r.get("chrome") or {}
        site = r.get("site") or {}
        out.append(f"<section class=site id='{e(r['id'])}'><h2>{e(r['id'])} "
                   f"<span class=muted>· <a href='{e(site.get('url', ''))}'>{e(site.get('url', ''))}</a></span></h2>")
        if ch.get("error"):
            out.append(f"<p class=bad>Chrome: {e(ch['error'])}</p>")
        if ch.get("blocked"):
            out.append(f"<p class=bad>Chrome was shown a {e(ch['blocked'])} page; this site is left out "
                       f"of the aggregates.</p>")
        for l in labels:
            en = r["engines"].get(l)
            if en and en.get("blocked"):
                out.append(f"<p class=bad>{e(l)} was shown a {e(en['blocked'])} page.</p>")
        out.append("<div class=shots>")
        out.append(f"<figure><img loading=lazy src='img/{e(r['id'])}-{e(base_label)}.jpg' alt=''>"
                   f"<figcaption>Chrome · FCP {fmt(ch.get('fcp'))} ms · LCP {fmt(ch.get('lcp'))} ms · "
                   f"load {fmt(ch.get('load'))} ms · CLS {fmt(ch.get('cls'), 3)}</figcaption></figure>")
        for l in labels:
            en = r["engines"].get(l)
            if not en:
                continue
            v = en["visual"]
            out.append(f"<figure><img loading=lazy src='img/{e(r['id'])}-{e(l)}.jpg' alt=''>"
                       f"<figcaption>{e(l)} · parity {fmt(v.get('parity'), 1)} · SSIM {fmt(v.get('ssim'), 3)} · "
                       f"first paint {fmt(en.get('firstPaintMs'))} ms · images loaded {fmt(en.get('firstRenderMs'))} ms"
                       + (f" · <span class=bad>{e(en['error'])}</span>" if en.get("error") else "") +
                       "</figcaption></figure>")
        if labels and labels[-1] in r["engines"]:
            out.append(f"<figure><img loading=lazy src='img/{e(r['id'])}-{e(labels[-1])}-diff.jpg' alt=''>"
                       f"<figcaption>difference vs Chrome ({e(labels[-1])})</figcaption></figure>")
        out.append("</div>")
        if r.get("film"):
            out.append("<p class=muted>Chrome filmstrip</p><div class=film>" + "".join(
                f"<figure><img loading=lazy src='img/{e(f['file'])}' alt=''><figcaption>{f['ms']} ms</figcaption></figure>"
                for f in r["film"]) + "</div>")
        out.append("<details><summary>Full page</summary><div class=fulls>")
        out.append(f"<figure><img loading=lazy src='img/{e(r['id'])}-{e(base_label)}-full.jpg' alt=''>"
                   f"<figcaption>Chrome · {fmt(ch.get('docH'))} px</figcaption></figure>")
        for l in labels:
            en = r["engines"].get(l)
            if en:
                out.append(f"<figure><img loading=lazy src='img/{e(r['id'])}-{e(l)}-full.jpg' alt=''>"
                           f"<figcaption>{e(l)} · {fmt(en.get('docH'))} px</figcaption></figure>")
        out.append("</div></details>")
        for l in labels:
            en = r["engines"].get(l)
            if not en:
                continue
            comp = en["visual"].get("components") or {}
            out.append(f"<details><summary>{e(l)}: phases, components and styles</summary>")
            out.append("<p><code>" + e(" · ".join(f"{k} {fmt(en.get(k))}" for k in (
                "fetchMs", "parseMs", "styleMs", "scriptMs", "layoutMs", "imagesMs", "paintMs",
                "netWaitMs", "processCpuMs", "maxRssMb", "nodes"))) + "</code></p>")
            if comp:
                style_rate = comp.get("styleRate")
                style_txt = "–" if style_rate is None else f"{style_rate * 100:.0f}%"
                out.append(f"<p>{comp['found']}/{comp['count']} Chrome components found, {comp['placed']} placed "
                           f"(IoU ≥ 0.5); style agreement {style_txt}</p>")
                out.append("<div class=wrap><table><thead><tr><th>Component</th><th>Chrome x,y,w,h</th>"
                           "<th>NS x,y,w,h</th><th>IoU</th></tr></thead><tbody>")
                for w in comp.get("worst", []):
                    out.append(f"<tr><td>{e(w['tag'])} “{e(w['text'])}”</td><td>{e(str(w['chrome']))}</td>"
                               f"<td>{e(str(w.get('ns', 'missing')))}</td><td>{fmt(w.get('iou'), 2)}</td></tr>")
                out.append("</tbody></table></div>")
                if comp.get("styleDiffs"):
                    out.append("<div class=wrap><table><thead><tr><th>Element</th><th>Property</th><th>Chrome</th>"
                               "<th>NS</th></tr></thead><tbody>")
                    for d in comp["styleDiffs"]:
                        out.append(f"<tr><td>{e(d['tag'])} “{e(d['text'])}”</td><td>{e(d['prop'])}</td>"
                                   f"<td>{e(str(d['chrome']))}</td><td>{e(str(d['ns']))}</td></tr>")
                    out.append("</tbody></table></div>")
            if en.get("jsErrorSample"):
                out.append("<pre>" + e("\n".join(en["jsErrorSample"])) + "</pre>")
            out.append("</details>")
        out.append("</section>")
    out.append(f"<script>{SORT_JS}</script></main></body></html>")
    Path(path).write_text("".join(out), encoding="utf-8", errors="replace")


def main():
    p = argparse.ArgumentParser(description="Compare Nordstjernen captures with the Chrome baseline.")
    p.add_argument("--out", default="sitebench-out", help="capture root containing one directory per label")
    p.add_argument("--base", default="chrome")
    p.add_argument("--labels", default="nordstjernen", help="comma-separated Nordstjernen labels, oldest first")
    p.add_argument("--report", default=None, help="report directory (default OUT/report)")
    p.add_argument("--viewport", default="1280x800")
    a = p.parse_args()
    out = Path(a.out)
    labels = [l for l in a.labels.split(",") if l]
    report = Path(a.report) if a.report else out / "report"
    img_dir = report / "img"
    img_dir.mkdir(parents=True, exist_ok=True)
    vw, vh = (int(x) for x in a.viewport.split("x"))

    ids = []
    for d in [out / a.base] + [out / l for l in labels]:
        if d.is_dir():
            for sub in sorted(d.iterdir()):
                if (sub / "metrics.json").exists() and sub.name not in ids:
                    ids.append(sub.name)
    if not ids:
        sys.exit(f"compare: no captures under {out}")
    rows = [analyse_site(i, out, a.base, labels, img_dir, vw, vh) for i in ids]
    agg = aggregate(rows, labels)

    chrome_versions = {(load_json(out / a.base / i / "metrics.json") or {}).get("version") for i in ids} - {None}
    ns_versions = sorted({(load_json(out / l / i / "metrics.json") or {}).get("version")
                          for l in labels for i in ids} - {None})
    meta = {"generated": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
            "chromeVersion": "Chrome " + ", ".join(sorted(chrome_versions)) if chrome_versions else "",
            "nsVersions": ns_versions, "viewport": a.viewport, "labels": labels}
    (report / "summary.json").write_text(json.dumps({"meta": meta, "aggregate": agg, "sites": rows}, indent=1),
                                         encoding="utf-8", errors="replace")
    write_markdown(rows, agg, labels, a.base, meta, report / "summary.md")
    write_html(rows, agg, labels, a.base, meta, report / "index.html")
    for l in labels:
        g = agg[l]
        print(f"{l}: loaded {g['loaded']}/{g['sites']}, parity {g['parityMean']}, "
              f"first paint ÷ FCP {fmt_ratio(g['firstRenderVsFcpGeomean'])}, "
              f"main CPU ÷ Chrome {fmt_ratio(g['mainThreadVsChromeGeomean'])}, "
              f"memory ÷ Chrome {fmt_ratio(g['memoryVsChromeGeomean'])}")
    print(f"report: {report / 'index.html'}")


if __name__ == "__main__":
    main()
