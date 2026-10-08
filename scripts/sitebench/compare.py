#!/usr/bin/env python3
"""compare.py — scores Nordstjernen captures against the Chrome baseline and writes an HTML + Markdown report."""

import argparse
import bisect
import html
import json
import math
import re
import sys
import traceback
from datetime import datetime, timezone
from pathlib import Path

try:
    import numpy as np
    from PIL import Image
except ImportError:
    print("compare: needs Pillow and numpy (pip install pillow numpy)", file=sys.stderr)
    sys.exit(2)

CHALLENGE = re.compile(
    r"just a moment|attention required|access denied|403 forbidden|confirm you are human|"
    r"security verification|are you a robot|robot check|pardon our interruption|request blocked|"
    r"verify you are human|you've been blocked|blocked by network security|captcha|"
    r"click the button below to continue", re.I)
CHALLENGE_URL = re.compile(r"[?&](js_challenge|__cf_chl\w*|captcha\w*)=", re.I)
ERROR_PAGE = re.compile(r"^something went wrong|^sorry, something went wrong|^an error occurred|"
                        r"^this page (couldn.t|could not|can.t) (load|be reached)", re.I)

WEIGHTS = {"ssim": 0.30, "hist": 0.15, "layout": 0.20, "components": 0.25, "text": 0.10}
FILM_MS = [500, 1000, 2000, 3000, 5000]
UNSTABLE_SPREAD = 2.0
BAND = 256


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


def load_viewport(capture_dir, vw, vh, size):
    shot = load_rgb(capture_dir / "viewport.png", size)
    if shot is not None:
        return shot
    try:
        with Image.open(capture_dir / "full.png") as im:
            canvas = Image.new("RGB", (vw, vh), (255, 255, 255))
            canvas.paste(im.convert("RGB").crop((0, 0, min(vw, im.width), min(vh, im.height))), (0, 0))
            return np.asarray(canvas.resize(size, Image.BILINEAR), dtype=np.float64)
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
    small = (probe.get("nodes") or 0) < 200
    if (CHALLENGE.search(probe.get("title") or "") or CHALLENGE.search(first_text)
            or (small and CHALLENGE_URL.search(probe.get("url") or ""))):
        return "bot challenge"
    if any(ERROR_PAGE.search(c.get("text") or "") for c in (probe.get("components") or [])[:6]):
        return "error"
    if status in (401, 403, 429) and small:
        return f"HTTP {status}"
    return None


def identifiable(c):
    return bool(c.get("text") or c.get("id"))


def component_key(c):
    return (c["tag"], re.sub(r"\d+", "#", c.get("text") or ""), c.get("id") or "")


def scored(c):
    return identifiable(c) and c["w"] * c["h"] >= 16


def overlapping_pairs(chrome, ns, ci, nj):
    bands = {}
    for j in nj:
        for b in range(ns[j]["y"] // BAND, (ns[j]["y"] + ns[j]["h"]) // BAND + 1):
            bands.setdefault(b, set()).add(j)
    ranked = []
    for i in ci:
        c = chrome[i]
        near = set()
        for b in range(c["y"] // BAND, (c["y"] + c["h"]) // BAND + 1):
            near |= bands.get(b, set())
        for j in near:
            iou = rect_iou(c, ns[j])
            if iou > 0:
                ranked.append((-iou, i, j))
    return sorted(ranked)


def pair_by_key(chrome, ns, ci, nj):
    pairs = {}
    taken = set()
    for _, i, j in overlapping_pairs(chrome, ns, ci, nj):
        if i not in pairs and j not in taken:
            pairs[i] = j
            taken.add(j)
    rest = [j for j in nj if j not in taken]
    pairs.update(zip([i for i in ci if i not in pairs], rest))
    return pairs


def match_components(chrome, ns):
    by_path = {(o["path"], o["tag"]): j for j, o in enumerate(ns) if o.get("path")}
    same_path = {i: by_path.get((c.get("path"), c["tag"])) for i, c in enumerate(chrome) if c.get("path")}
    pairs = {i: (j, "path") for i, j in same_path.items()
             if j is not None and component_key(ns[j]) == component_key(chrome[i])}
    taken = {j for j, _ in pairs.values()}
    groups = {}
    for i, c in enumerate(chrome):
        if i not in pairs:
            groups.setdefault(component_key(c), ([], []))[0].append(i)
    for j, o in enumerate(ns):
        if j not in taken and component_key(o) in groups:
            groups[component_key(o)][1].append(j)
    for ci, nj in groups.values():
        for i, j in pair_by_key(chrome, ns, ci, nj).items():
            pairs[i] = (j, "key")
    taken = {j for j, _ in pairs.values()}
    for i, j in same_path.items():
        if j is not None and i not in pairs and j not in taken:
            pairs[i] = (j, "path only")
    return pairs


def offset_record(c, o, how):
    rec = {"tag": c["tag"], "text": c.get("text", "")[:40], "chrome": [c["x"], c["y"], c["w"], c["h"]],
           "path": c.get("path")}
    if o is None:
        return dict(rec, missing=True)
    return dict(rec, iou=round(rect_iou(c, o), 2), dx=o["x"] - c["x"], dy=o["y"] - c["y"],
                dw=o["w"] - c["w"], dh=o["h"] - c["h"], ns=[o["x"], o["y"], o["w"], o["h"]],
                nsPath=o.get("path"), by=how)


def compare_styles(c, o, diffs):
    ok = 0
    for key in ("fs", "fw", "color", "bg", "ff"):
        if style_equal(key, c.get(key), o.get(key)):
            ok += 1
        elif len(diffs) < 60:
            diffs.append({"tag": c["tag"], "text": c.get("text", "")[:30], "prop": key,
                          "chrome": c.get(key), "ns": o.get(key)})
    return ok


def placement(chrome, ns, pairs, base):
    found = placed = style_checks = style_ok = 0
    by = {"path": 0, "key": 0, "path only": 0}
    offsets, style_diffs = [], []
    for i in base:
        c = chrome[i]
        j, how = pairs.get(i, (None, None))
        o = ns[j] if j is not None else None
        offsets.append(offset_record(c, o, how))
        if o is None:
            continue
        found += 1
        by[how] += 1
        placed += rect_iou(c, o) >= 0.5
        if c.get("text"):
            style_checks += 5
            style_ok += compare_styles(c, o, style_diffs)
    offsets.sort(key=lambda o: (0 if o.get("missing") else 1, -(abs(o.get("dy", 0)) + abs(o.get("dx", 0)))))
    return {
        "count": len(base), "found": found, "placed": placed,
        "foundRate": found / len(base), "placedRate": placed / len(base),
        "styleRate": style_ok / style_checks if style_checks else None,
        "byPath": by["path"], "byKey": by["key"], "byPathOnly": by["path only"], "unmatched": len(base) - found,
        "worst": offsets[:15], "styleDiffs": style_diffs[:25],
    }


def ns_unpaired(ns, pairs, in_region):
    paired = {j for j, _ in pairs.values()}
    region = [j for j, o in enumerate(ns) if scored(o) and in_region(o)]
    return {"nsCount": len(region), "nsUnmatched": sum(1 for j in region if j not in paired)}


def component_scores(chrome_probe, ns_probe):
    if not chrome_probe or not ns_probe:
        return None
    vh = chrome_probe.get("vh") or 800
    chrome = [c for c in chrome_probe.get("components", []) if scored(c)]
    base = [i for i, c in enumerate(chrome) if c["y"] < vh]
    if not base:
        return None
    ns = ns_probe.get("components", [])
    pairs = match_components(chrome, ns)
    ns_vh = ns_probe.get("vh") or vh
    return dict(placement(chrome, ns, pairs, base), **ns_unpaired(ns, pairs, lambda o: o["y"] < ns_vh))


def on_page(c, vw):
    return c["x"] < vw and c["x"] + c["w"] > 0


def inventory_cap(probe):
    inv = (probe or {}).get("inventory") or {}
    return inv.get("max") if inv.get("capped") else None


def within_reach(probe, other):
    more = probe.get("moreComponents") or []
    if not inventory_cap(other):
        return more
    held = {(o.get("path"), o["tag"]) for o in other.get("moreComponents") or []}
    last = max((k for k, c in enumerate(more) if (c.get("path"), c["tag"]) in held), default=-1)
    return more[:last + 1] if last >= 0 else more


def left_out(probe, kept, vw):
    return sum(1 for c in (probe.get("moreComponents") or [])[len(kept):] if scored(c) and on_page(c, vw))


def parent_path(c):
    return (c.get("path") or "").rpartition(">")[0]


def container_offset(c, offset, by_path):
    path = c.get("path") or ""
    while ">" in path:
        path = path.rsplit(">", 1)[0]
        if by_path.get(path) in offset:
            return offset[by_path[path]]
    return 0, 0


def horizontal_gap(a, b):
    return max(a["x"] - b["x"] - b["w"], b["x"] - a["x"] - a["w"], 0)


def nearest_above(c, siblings, chrome):
    bottoms, ids = siblings
    k = bisect.bisect_right(bottoms, c["y"])
    if not k:
        return None
    level = ids[bisect.bisect_left(bottoms, bottoms[k - 1]):k]
    return min(level, key=lambda j: (horizontal_gap(chrome[j], c), j))


def placed_in_surroundings(chrome, ns, pairs, base):
    offset = {i: (ns[j]["x"] - chrome[i]["x"], ns[j]["y"] - chrome[i]["y"]) for i, (j, _) in pairs.items()}
    by_path = {c["path"]: i for i, c in enumerate(chrome) if c.get("path")}
    siblings = {}
    for i in sorted(offset, key=lambda k: (chrome[k]["y"] + chrome[k]["h"], k)):
        bottoms, ids = siblings.setdefault(parent_path(chrome[i]), ([], []))
        bottoms.append(chrome[i]["y"] + chrome[i]["h"])
        ids.append(i)
    placed = 0
    for i in base:
        if i not in pairs:
            continue
        c, o = chrome[i], ns[pairs[i][0]]
        k = nearest_above(c, siblings.get(parent_path(c), ([], [])), chrome)
        dx, dy = offset[k] if k is not None else container_offset(c, offset, by_path)
        placed += rect_iou(c, o) >= 0.5 or rect_iou(c, dict(o, x=o["x"] - dx, y=o["y"] - dy)) >= 0.5
    return placed


def page_scores(chrome_probe, ns_probe):
    if not (chrome_probe or {}).get("inventory") or not (ns_probe or {}).get("inventory"):
        return None
    vw = chrome_probe.get("vw") or 1280
    ns_vw = ns_probe.get("vw") or vw
    chrome_more, ns_more = within_reach(chrome_probe, ns_probe), within_reach(ns_probe, chrome_probe)
    chrome = [c for c in (chrome_probe.get("components") or []) + chrome_more if scored(c)]
    base = [i for i, c in enumerate(chrome) if on_page(c, vw)]
    if not base:
        return None
    ns = (ns_probe.get("components") or []) + ns_more
    pairs = match_components(chrome, ns)
    res = {k: v for k, v in placement(chrome, ns, pairs, base).items() if k not in ("worst", "styleDiffs")}
    placed = placed_in_surroundings(chrome, ns, pairs, base)
    return dict(res, placed=placed, placedRate=placed / len(base),
                placedOnPage=res["placed"], placedOnPageRate=res["placedRate"],
                leftOut=left_out(chrome_probe, chrome_more, vw), nsLeftOut=left_out(ns_probe, ns_more, ns_vw),
                **ns_unpaired(ns, pairs, lambda o: on_page(o, ns_vw)))


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


def visual_scores(chrome_img, chrome_probe, img, probe):
    visual = {}
    if img is not None and chrome_img is not None:
        visual["ssim"] = round(ssim(chrome_img, img), 3)
        visual["hist"] = round(hist_similarity(chrome_img, img), 3)
        visual["layout"] = round(layout_iou(chrome_img, img), 3)
    visual["components"] = component_scores(chrome_probe, probe)
    visual["page"] = page_scores(chrome_probe, probe)
    visual["textRatio"] = closeness((chrome_probe or {}).get("textLen"), (probe or {}).get("textLen"))
    visual["heightRatio"] = closeness((chrome_probe or {}).get("docH"), (probe or {}).get("docH"))
    visual["parity"] = parity(visual) if img is not None else 0.0
    return visual


def load_runs(capture_dir, metrics, records_key, vw, vh):
    metrics = metrics or {}
    records = metrics.get(records_key) or []
    count = (metrics.get("settings") or {}).get("visualRuns") or 1
    runs = []
    for k in range(1, count + 1):
        run_dir = capture_dir if k == 1 else capture_dir / f"visual-{k}"
        record = (records[k - 1] if k <= len(records) else None) or {}
        runs.append({"run": k, "img": load_viewport(run_dir, vw, vh, (vw // 2, vh // 2)),
                     "probe": load_json(run_dir / "probe.json"),
                     "status": metrics.get("status") if k == 1 else record.get("status") or metrics.get("status"),
                     "error": metrics.get("error") if k == 1 else record.get("error")})
    return runs


def chrome_reference(runs):
    shot = [r for r in runs if r["img"] is not None]
    if len(shot) < 2:
        return (shot or runs)[0], None
    scores = {(a["run"], b["run"]): visual_scores(a["img"], a["probe"], b["img"], b["probe"])["parity"] or 0.0
              for a in shot for b in shot if a is not b}
    ref = max(shot, key=lambda a: sum(scores[(a["run"], b["run"])] for b in shot if b is not a))
    for r in runs:
        r["parity"] = 100.0 if r is ref else scores.get((ref["run"], r["run"]))
    return ref, round(100.0 - min(r["parity"] for r in shot), 1)


def run_failure(r):
    if r["error"]:
        return r["error"]
    return None if r["img"] is not None else "no screenshot"


def median_run(runs):
    scored = sorted((r for r in runs if r["visual"]["parity"] is not None),
                    key=lambda r: (r["visual"]["parity"], r["run"]))
    if not scored:
        return runs[0], None, None
    values = [r["visual"]["parity"] for r in scored]
    loaded = [r["visual"]["parity"] for r in scored if not run_failure(r)]
    spread = round(loaded[-1] - loaded[0], 1) if len(loaded) > 1 else None
    middle = values[(len(values) - 1) // 2]
    shown = next(r for r in scored if r["visual"]["parity"] == middle)
    return shown, round(med(values), 1), spread


def run_records(runs, scores):
    return [{"run": r["run"], "parity": scores(r), "screenshot": r["img"] is not None, "error": r["error"],
             "blocked": blocked_reason(r["probe"], r["status"])} for r in runs]


def save_run_thumbs(runs, img_dir, name, vw, vh):
    for r in runs:
        if r["img"] is not None:
            save_jpeg(r["img"], img_dir / f"{name}-run{r['run']}.jpg", (vw // 4, vh // 4))


def save_filmstrip(chrome_dir, img_dir, name, vw, vh):
    film = []
    index = load_json(chrome_dir / "frames" / "index.json") or []
    for ms in FILM_MS:
        pick = [f for f in index if f["ms"] <= ms]
        if pick:
            dest = img_dir / f"{name}-film-{ms}.jpg"
            img = load_rgb(chrome_dir / "frames" / pick[-1]["file"], (vw // 4, vh // 4))
            if img is not None:
                save_jpeg(img, dest)
                film.append({"ms": ms, "file": dest.name})
    return film


def save_label_images(site_id, label, ns_dir, shown, ref, img_dir, vw, vh):
    if shown["img"] is None:
        return
    save_jpeg(shown["img"], img_dir / f"{site_id}-{label}.jpg")
    full_thumb(ns_dir / "full.png", img_dir / f"{site_id}-{label}-full.jpg")
    initial = ns_dir / "full-initial.png"
    if initial.exists():
        with Image.open(initial) as im:
            im = im.convert("RGB").crop((0, 0, vw, vh)).resize((vw // 4, vh // 4), Image.BILINEAR)
            im.save(img_dir / f"{site_id}-{label}-initial.jpg", "JPEG", quality=75)
    if ref["img"] is not None:
        diff_heatmap(ref["img"], shown["img"]).save(img_dir / f"{site_id}-{label}-diff.jpg", "JPEG", quality=75)


def analyse_label(site_id, label, ns_dir, ns, chrome, ref, img_dir, vw, vh):
    runs = load_runs(ns_dir, ns, "visualRuns", vw, vh)
    for r in runs:
        r["visual"] = visual_scores(ref["img"], ref["probe"], r["img"], r["probe"])
    shown, median, spread = median_run(runs)
    failed = [r for r in runs if run_failure(r)]
    error = runs[0]["error"]
    if len(runs) > 1 and failed and not error:
        error = f"run {failed[0]['run']}: {run_failure(failed[0])}"
    ns_probe = runs[0]["probe"]
    s = dict(ns["summary"])
    vt = ns.get("visualTiming") or {}
    probe_ms = (ns_probe or {}).get("probeMs")
    if isinstance(vt.get("cpu_ms"), (int, float)):
        s["settledMainThreadMs"] = round(max(vt["cpu_ms"] - (vt.get("encode_ms") or 0) - (probe_ms or 0), 0), 1)
    entry = dict(s, status=ns.get("status"), error=error, probeMs=probe_ms,
                 docH=(ns_probe or {}).get("docH"), textLen=(ns_probe or {}).get("textLen"),
                 jsErrorSample=ns.get("jsErrorSample", []),
                 blocked=blocked_reason(shown["probe"], shown["status"]),
                 inventoryCap=inventory_cap(shown["probe"]))
    save_label_images(site_id, label, ns_dir, shown, ref, img_dir, vw, vh)
    entry["visual"] = dict(shown["visual"], parity=median)
    c = chrome or {}
    first_paint = s.get("firstPaintMs") or s.get("firstRenderMs")
    entry["firstPaintMs"] = first_paint
    entry["vsChrome"] = {
        "firstRender": ratio(first_paint, c.get("fcp")),
        "imagesLoaded": ratio(s.get("firstRenderMs"), c.get("load")),
        "mainThread": ratio(s.get("settledMainThreadMs"), c.get("mainThreadMs")),
        "memory": ratio(s.get("settledMaxRssMb"), c.get("browserRssMb")),
    }
    if len(runs) > 1:
        entry.update(paritySpread=spread, shownRun=shown["run"], failedRuns=len(failed),
                     visualRuns=run_records(runs, lambda r: r["visual"]["parity"]))
        save_run_thumbs(runs, img_dir, f"{site_id}-{label}", vw, vh)
    return entry


def more_failed_runs(first, last):
    if "failedRuns" not in first or "failedRuns" not in last:
        return 0
    return last["failedRuns"] - first["failedRuns"]


def mark_stability(row, labels):
    chrome_spread = (row.get("chrome") or {}).get("paritySpread")
    spreads = [chrome_spread] + [e.get("paritySpread") for e in row["engines"].values()]
    known = [s for s in spreads if s is not None]
    if known:
        row["unstable"] = max(known) > UNSTABLE_SPREAD
    if len(labels) < 2:
        return
    first, last = row["engines"].get(labels[0]), row["engines"].get(labels[-1])
    if not first or not last:
        return
    a, b = first["visual"].get("parity"), last["visual"].get("parity")
    noise = [s for s in (chrome_spread, first.get("paritySpread"), last.get("paritySpread")) if s is not None]
    more = more_failed_runs(first, last)
    if a is None or b is None or not (noise or more):
        return
    change = round(b - a, 1)
    noise = max(noise) if noise else None
    row["parityChange"] = {"change": change, "noise": noise,
                           "beyondNoise": bool(more) or (noise is not None and abs(change) > noise)}
    if more:
        row["parityChange"]["moreFailedRuns"] = more


def analyse_site(site_id, out, base_label, labels, img_dir, vw, vh):
    chrome_dir = out / base_label / site_id
    chrome = load_json(chrome_dir / "metrics.json")
    runs = load_runs(chrome_dir, chrome, "runs", vw, vh)
    ref, spread = chrome_reference(runs)
    chrome_probe = runs[0]["probe"]
    row = {"id": site_id, "chrome": None, "engines": {}}
    if chrome:
        cs = chrome["summary"]
        si, vc = speed_index(chrome_dir / "frames")
        row["site"] = chrome["site"]
        row["chrome"] = dict(cs, status=chrome.get("status"), error=chrome.get("error"),
                             speedIndex=si, visuallyComplete=vc,
                             docH=(chrome_probe or {}).get("docH"),
                             textLen=(chrome_probe or {}).get("textLen"),
                             blocked=blocked_reason(ref["probe"], ref["status"]),
                             inventoryCap=inventory_cap(ref["probe"]))
        if len(runs) > 1:
            row["chrome"].update(paritySpread=spread, reference=ref["run"],
                                 visualRuns=run_records(runs, lambda r: r.get("parity")))
    if ref["img"] is not None:
        save_jpeg(ref["img"], img_dir / f"{site_id}-{base_label}.jpg")
        full_thumb(chrome_dir / "full.png", img_dir / f"{site_id}-{base_label}-full.jpg")
    if len(runs) > 1:
        save_run_thumbs(runs, img_dir, f"{site_id}-{base_label}", vw, vh)
    row["film"] = save_filmstrip(chrome_dir, img_dir, f"{site_id}-{base_label}", vw, vh)

    for label in labels:
        ns_dir = out / label / site_id
        ns = load_json(ns_dir / "metrics.json")
        if not ns:
            continue
        if "site" not in row:
            row["site"] = ns["site"]
        row["engines"][label] = analyse_label(site_id, label, ns_dir, ns, row["chrome"], ref, img_dir, vw, vh)
    mark_stability(row, labels)
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


def mean_parity(es):
    return round(sum(e["visual"]["parity"] or 0 for e in es) / len(es), 1) if es else None


def aggregate(rows, labels):
    agg = {}
    repeats = any("unstable" in r for r in rows)
    blocked = [r["id"] for r in rows if not comparable(r)]
    rows = [r for r in rows if comparable(r)]
    ns_blocked = [r["id"] for r in rows
                  if any((r["engines"].get(label) or {}).get("blocked") for label in labels)]
    common = [r for r in rows if r["id"] not in ns_blocked]
    stable = [r for r in common if not r.get("unstable")]
    unstable = [r for r in common if r.get("unstable")]
    for label in labels:
        es = [r["engines"][label] for r in common if label in r["engines"]]
        vs = [r["engines"][label] for r in stable if label in r["engines"]]
        ok = [e for e in es if not e.get("error")]
        agg[label] = {
            "sites": len(es), "loaded": len(ok),
            "parityMean": mean_parity(vs),
            "ssimMedian": med([e["visual"].get("ssim") for e in vs]),
            "componentsPlacedMedian": med([(e["visual"].get("components") or {}).get("placedRate") for e in vs]),
            "pagePlacedMedian": med([(e["visual"].get("page") or {}).get("placedRate") for e in vs]),
            "pageOnPageMedian": med([(e["visual"].get("page") or {}).get("placedOnPageRate") for e in vs]),
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
        if repeats:
            us = [r["engines"][label] for r in unstable if label in r["engines"]]
            agg[label].update(stableSites=len(vs), unstableSites=len(us), parityMeanUnstable=mean_parity(us))
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
    if repeats:
        changes = [r for r in common if r.get("parityChange")]
        agg["chrome"].update(stableSites=len(stable), unstable=[r["id"] for r in unstable],
                             changed=[r["id"] for r in changes if r["parityChange"]["beyondNoise"]],
                             withinNoise=[r["id"] for r in changes if not r["parityChange"]["beyondNoise"]])
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


def capture_info(label_dir, ids):
    metrics = [load_json(label_dir / i / "metrics.json") or {} for i in ids]
    locales = sorted({m.get("locale") for m in metrics} - {None})
    if any(m and "locale" not in m for m in metrics):
        locales.append("not recorded")
    return {"versions": sorted({m.get("version") for m in metrics} - {None}), "locales": locales}


def with_locale(text, locale):
    return f"{text} ({locale})" if locale else text


def describe_builds(meta):
    parts = [with_locale(meta.get("chromeVersion") or "Chrome", meta.get("chromeLocale"))]
    for label in meta["labels"]:
        build = meta["builds"].get(label) or {}
        if build.get("versions"):
            parts.append(with_locale(f"{label}: " + ", ".join(build["versions"]),
                                     ", ".join(build.get("locales") or [])))
    return " · ".join(parts)


def sites(n, noun="site"):
    return f"{n} {noun}" + ("" if n == 1 else "s")


def spread_text(r, labels):
    spreads = [("Chrome", (r.get("chrome") or {}).get("paritySpread"))]
    spreads += [(label, (r["engines"].get(label) or {}).get("paritySpread")) for label in labels]
    return ", ".join(f"{name} {fmt(s, 1)}" for name, s in spreads if s is not None)


def change_text(change):
    if not change:
        return "–"
    text = "0.0" if change["change"] == 0 else f"{change['change']:+.1f}"
    more = change.get("moreFailedRuns")
    if more:
        return text + f" ({abs(more)} {'more' if more > 0 else 'fewer'} failed run{'' if abs(more) == 1 else 's'})"
    if change["change"] == 0:
        return text
    return text + ("" if change["beyondNoise"] else " (noise)")


def failed_runs_note(rows, labels):
    failed = []
    for r in rows:
        counts = []
        for label in labels:
            e = r["engines"].get(label) or {}
            if e.get("failedRuns"):
                counts.append(f"{label} {e['failedRuns']} of {len(e['visualRuns'])}")
        if counts:
            failed.append(f"{r['id']} ({', '.join(counts)})")
    if not failed:
        return []
    return ["Failed visual runs, which count in the median but not in the spread: " + ", ".join(failed) + "."]


def stability_notes(rows, agg, labels):
    c = agg["chrome"]
    if "unstable" not in c:
        return []
    by_id = {r["id"]: r for r in rows}
    notes = ["A site's parity is the median over its visual runs, each scored against Chrome's reference run, "
             "the Chrome run closest to Chrome's other runs. A build's failed run, one with a load error or "
             "without a screenshot, counts in the median (as 0 without a screenshot) but not in the spread. "
             "A site is unstable when the runs of one build, or "
             f"Chrome's runs against its reference, spread over more than {UNSTABLE_SPREAD:g} parity points; "
             "unstable sites are left out of the mean visual parity, median SSIM and both medians of components "
             "placed, and averaged on a row of their own."]
    if c["unstable"]:
        notes.append("Unstable, with the spreads in parity points: " + ", ".join(
            f"{i} ({spread_text(by_id[i], labels)})" for i in c["unstable"]) + ".")
    else:
        notes.append("No site was unstable.")
    notes += failed_runs_note(rows, labels)
    if len(labels) > 1 and (c["changed"] or c["withinNoise"]):
        notes.append(change_note(by_id, c, labels))
    return notes


def change_note(by_id, c, labels):
    head = f"{labels[-1]} against {labels[0]}"
    within = len(c["withinNoise"])
    if not c["changed"]:
        return (f"{head}: parity stayed within each site's noise, the largest of its spreads "
                f"({sites(within)} compared).")
    changes = [by_id[i]["parityChange"] for i in c["changed"]]
    moved = [f"{i} {change_text(ch)}" + ("" if ch.get("moreFailedRuns") else f" (noise {fmt(ch['noise'], 1)})")
             for i, ch in zip(c["changed"], changes)]
    failed = any(ch.get("moreFailedRuns") for ch in changes)
    failures = ", or the number of failed visual runs changed," if failed else ","
    return (f"{head}: parity moved by more than the site's noise, the largest of its spreads{failures} on "
            f"{sites(len(c['changed']))}: " + ", ".join(moved) + f"; {within} stayed within it.")


def markdown_site_table(rows, labels, repeats):
    both = repeats and len(labels) > 1
    spread_head = "Chrome spread | " if repeats else ""
    lines = ["| Site | Chrome FCP | Chrome main | " + spread_head + " | ".join(
        f"{label} parity | " + (f"{label} spread | " if repeats else "") + f"{label} first paint | {label} main CPU"
        for label in labels) + (f" | {labels[-1]} − {labels[0]}" if both else "") + " |"]
    col = "---:|" if repeats else ""
    lines.append("|---|---:|---:|" + col + ("---:|" + col + "---:|---:|") * len(labels) + ("---:|" if both else ""))
    for r in rows:
        ch = r.get("chrome") or {}
        site_id = (r["id"] + (" (Chrome blocked)" if ch.get("blocked") else "")
                   + (" (unstable)" if r.get("unstable") else ""))
        cells = [site_id, fmt(ch.get("fcp")), fmt(ch.get("mainThreadMs"))]
        if repeats:
            cells.append(fmt(ch.get("paritySpread"), 1))
        for label in labels:
            e = r["engines"].get(label)
            if not e:
                cells += ["–"] * (4 if repeats else 3)
                continue
            mark = " ⚠" if e.get("error") or e.get("blocked") else ""
            cells.append(fmt(e["visual"].get("parity"), 1) + mark)
            if repeats:
                cells.append(fmt(e.get("paritySpread"), 1))
            cells += [fmt(e.get("firstPaintMs")), fmt(e.get("settledMainThreadMs"))]
        if both:
            cells.append(change_text(r.get("parityChange")))
        lines.append("| " + " | ".join(cells) + " |")
    return lines


def parity_rows(agg, labels):
    c = agg["chrome"]
    if "unstable" not in c:
        return [("Mean visual parity", "parityMean", None)]
    kinds = [("stable", "parityMean", "stableSites")]
    if c["unstable"]:
        kinds.append(("unstable", "parityMeanUnstable", "unstableSites"))
    rows = []
    for kind, key, count_key in kinds:
        counts = {agg[label][count_key] for label in labels}
        if len(counts) == 1:
            rows.append((f"Mean visual parity ({sites(counts.pop(), kind + ' site')})", key, None))
        else:
            rows.append((f"Mean visual parity ({kind} sites)", key, count_key))
    return rows


def parity_note(g, count_key):
    return f" ({sites(g[count_key])})" if count_key else ""


def fmt_pct(v):
    return "–" if v is None else f"{v * 100:.0f}%"


def inventory_caps(r, labels):
    caps = [("Chrome", (r.get("chrome") or {}).get("inventoryCap"))]
    caps += [(label, (r["engines"].get(label) or {}).get("inventoryCap")) for label in labels]
    return [(name, cap) for name, cap in caps if cap]


def cap_note(rows, labels):
    capped = [f"{r['id']} (" + ", ".join(f"{name} at {cap}" for name, cap in inventory_caps(r, labels)) + ")"
              for r in rows if inventory_caps(r, labels)]
    if not capped:
        return None
    return ("The component inventory reached its cap on " + "; ".join(capped) + ". There the whole-page "
            "placement covers the page only up to where the first of the two compared inventories stopped, "
            "and leaves out the components past that point.")


def cpu_note(rows, labels):
    measured = [(label, r["id"], en) for label in labels for r in rows for en in [r["engines"].get(label)]
                if en and not en.get("error") and en.get("settledMainThreadMs") is not None]
    older = [(label, site) for label, site, en in measured if en.get("probeMs") is None]
    if not older or len(older) == len(measured):
        return None
    parts = []
    for label in labels:
        sites = [site for name, site in older if name == label]
        total = sum(1 for name, _, _ in measured if name == label)
        if sites:
            parts.append(f"{label} (all {total} sites)" if len(sites) == total else f"{label} on " + ", ".join(sites))
    return ("Nordstjernen's main-thread CPU leaves out the time the probe takes, except in captures made before "
            "the probe recorded it, which count it in and are not comparable with the rest: " + "; ".join(parts) + ".")


def write_markdown(rows, agg, labels, base_label, meta, path):
    c = agg["chrome"]
    repeats = "unstable" in c
    lines = ["# Site benchmark: Nordstjernen vs Chrome", ""]
    lines.append(f"{describe_builds(meta)} · viewport {meta['viewport']} · "
                 f"{len(rows)} sites · generated {meta['generated']}")
    lines.append("")
    lines.append("Parity is a 0–100 visual similarity score against Chrome "
                 "(SSIM, colour histogram, content layout, component placement, text). "
                 "Time ratios are Nordstjernen ÷ Chrome; below 1.00× means Nordstjernen is faster.")
    lines.append("")
    lines.append("| | " + " | ".join(labels) + " |")
    lines.append("|---|" + "---|" * len(labels))
    parity = parity_rows(agg, labels)
    counts = {title: count_key for title, _, count_key in parity}
    keys = [("Sites loaded", "loaded", lambda v: fmt(v))] + [(title, key, lambda v: fmt(v, 1))
                                                           for title, key, _ in parity] + [
            ("Median viewport SSIM", "ssimMedian", lambda v: fmt(v, 3)),
            ("Median components placed", "componentsPlacedMedian", fmt_pct),
            ("Median components placed in their surroundings, whole page", "pagePlacedMedian", fmt_pct),
            ("Median components at the same place on the page, whole page", "pageOnPageMedian", fmt_pct),
            ("Median first paint (ms)", "firstRenderMedianMs", lambda v: fmt(v)),
            ("First paint ÷ Chrome FCP (geomean)", "firstRenderVsFcpGeomean", fmt_ratio),
            ("Sites painting before Chrome FCP", "fasterFirstRender", lambda v: fmt(v)),
            ("Images loaded ÷ Chrome load event (geomean)", "imagesLoadedVsLoadGeomean", fmt_ratio),
            ("Main-thread CPU ÷ Chrome (geomean)", "mainThreadVsChromeGeomean", fmt_ratio),
            ("Peak memory ÷ Chrome (geomean)", "memoryVsChromeGeomean", fmt_ratio),
            ("JS errors (all sites)", "jsErrors", lambda v: fmt(v)),
            ("Sites showing a bot challenge", "nsBlocked", lambda v: fmt(v))]
    for title, key, f in keys:
        lines.append(f"| {title} | " + " | ".join(f(agg[label].get(key)) + parity_note(agg[label], counts.get(title))
                                                   for label in labels) + " |")
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
    for note in stability_notes(rows, agg, labels) + [n for n in (cap_note(rows, labels), cpu_note(rows, labels)) if n]:
        lines += ["", note]
    lines.append("")
    lines += markdown_site_table(rows, labels, repeats)
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


def cell(v, digits=0, cls="", note=""):
    num = "" if v is None else f' data-v="{v}"'
    return f'<td{num} class="{cls}">{html.escape(fmt(v, digits) + note)}</td>'


def ratio_cell(v):
    if v is None:
        return '<td data-v="">–</td>'
    cls = "good" if v < 1 else "bad"
    return f'<td data-v="{v:.4f}" class="{cls}">{v:.2f}×</td>'


def change_cell(change):
    if not change:
        return '<td data-v="">–</td>'
    more = change.get("moreFailedRuns") or 0
    if not change["beyondNoise"]:
        cls = "muted"
    elif more:
        cls = "bad" if more > 0 else "good"
    else:
        cls = "good" if change["change"] > 0 else "bad"
    return f'<td data-v="{change["change"]}" class="{cls}">{html.escape(change_text(change))}</td>'


SUMMARY_ROWS = [("Sites loaded", "loaded", 0), ("Mean visual parity", "parityMean", 1),
                ("Median SSIM", "ssimMedian", 3),
                ("Median components placed, first screen %", "componentsPlacedMedian", "%"),
                ("Median components placed in their surroundings, whole page %", "pagePlacedMedian", "%"),
                ("Median components at the same place on the page, whole page %", "pageOnPageMedian", "%"),
                ("Median first paint ms", "firstRenderMedianMs", 0),
                ("First paint ÷ Chrome FCP", "firstRenderVsFcpGeomean", 2),
                ("Images loaded ÷ Chrome load", "imagesLoadedVsLoadGeomean", 2),
                ("Main-thread CPU ÷ Chrome", "mainThreadVsChromeGeomean", 2),
                ("Peak memory ÷ Chrome", "memoryVsChromeGeomean", 2),
                ("Sites painting before Chrome FCP", "fasterFirstRender", 0),
                ("JS errors", "jsErrors", 0)]


def summary_rows(agg, labels):
    return ([row + (None,) for row in SUMMARY_ROWS[:1]]
            + [(title, key, 1, count_key) for title, key, count_key in parity_rows(agg, labels)]
            + [row + (None,) for row in SUMMARY_ROWS[2:]])


PHASE_KEYS = ("fetchMs", "parseMs", "styleMs", "scriptMs", "layoutMs", "imagesMs", "paintMs",
              "netWaitMs", "processCpuMs", "maxRssMb", "nodes")


def percent(v):
    return None if v is None else round(v * 100)


def summary_cell(v, digits, note=""):
    return cell(percent(v), 0, note=note) if digits == "%" else cell(v, digits, note=note)


def html_summary(rows, agg, labels, meta):
    e = html.escape
    out = [f"<!doctype html><html lang=en><head><meta charset=utf-8>"
           f"<meta name=viewport content='width=device-width,initial-scale=1'>"
           f"<title>Site Benchmark Report</title><style>{CSS}</style></head><body><main>"]
    out.append("<h1>Nordstjernen vs Chrome — site benchmark</h1>")
    out.append(f"<p class=muted>{e(describe_builds(meta))} · "
               f"viewport {e(meta['viewport'])} · {len(rows)} sites · {e(meta['generated'])}</p>")
    out.append("<h2>Summary</h2><div class=wrap><table><thead><tr><th>Metric</th>" +
               "".join(f"<th>{e(label)}</th>" for label in labels) + "</tr></thead><tbody>")
    c = agg["chrome"]
    for title, key, digits, count_key in summary_rows(agg, labels):
        out.append(f"<tr><td>{e(title)}</td>" + "".join(summary_cell(agg[label].get(key), digits,
                                                                      parity_note(agg[label], count_key))
                                                         for label in labels) + "</tr>")
    out.append("</tbody></table></div>")
    if c.get("excluded"):
        out.append("<p class=muted>Left out of the summary because headless Chrome was shown a bot challenge "
                   "or an error page: " + e(", ".join(c["excluded"])) + ".</p>")
    if c.get("nsExcluded"):
        out.append("<p class=muted>Also left out, so every column averages the same sites, because at least "
                   "one Nordstjernen run was shown a bot challenge or an error page: "
                   + e(", ".join(c["nsExcluded"])) + ".</p>")
    for note in stability_notes(rows, agg, labels) + [n for n in (cap_note(rows, labels), cpu_note(rows, labels)) if n]:
        out.append(f"<p class=muted>{e(note)}</p>")
    return out


def html_engine_cells(en, repeats):
    if not en:
        return "<td>–</td>" * (11 if repeats else 10)
    v = en["visual"]
    spread = en.get("paritySpread")
    spread_cell = cell(spread, 1, "bad" if (spread or 0) > UNSTABLE_SPREAD else "") if repeats else ""
    return (cell(v.get("parity"), 1, "bad" if en.get("error") else "") + spread_cell + cell(v.get("ssim"), 3) +
            cell(percent((v.get("components") or {}).get("placedRate")), 0) +
            cell(percent((v.get("page") or {}).get("placedRate")), 0) +
            cell(en.get("firstPaintMs")) + ratio_cell(en["vsChrome"]["firstRender"]) +
            cell(en.get("settledMainThreadMs")) + ratio_cell(en["vsChrome"]["mainThread"]) +
            cell(en.get("settledMaxRssMb")) + cell(en.get("jsErrors")))


def html_site_table(rows, labels, repeats):
    e = html.escape
    both = repeats and len(labels) > 1
    spread_head = "<th>Spread</th>" if repeats else ""
    out = ["<h2>Per site</h2><div class=wrap><table><thead><tr><th>Site</th><th>Cat</th>"
           "<th>Chrome FCP</th><th>LCP</th><th>Load</th><th>Speed idx</th><th>Main ms</th><th>RSS MB</th>" + spread_head]
    for label in labels:
        out.append(f"<th>{e(label)} parity</th>" + spread_head + "<th>SSIM</th><th>Placed</th>"
                   "<th>Page placed</th><th>First paint</th><th>÷FCP</th><th>Main CPU</th><th>÷Chrome</th>"
                   "<th>RSS MB</th><th>JS err</th>")
    if both:
        out.append(f"<th>{e(labels[-1])} − {e(labels[0])}</th>")
    out.append("</tr></thead><tbody>")
    for r in rows:
        ch = r.get("chrome") or {}
        site = r.get("site") or {}
        mark = " <span class=bad>unstable</span>" if r.get("unstable") else ""
        out.append(f"<tr><td><a href='#{e(r['id'])}'>{e(r['id'])}</a>{mark}</td>"
                   f"<td>{e(site.get('category', ''))}</td>")
        out.append(cell(ch.get("fcp")) + cell(ch.get("lcp")) + cell(ch.get("load")) + cell(ch.get("speedIndex")) +
                   cell(ch.get("mainThreadMs")) + cell(ch.get("browserRssMb")) +
                   (cell(ch.get("paritySpread"), 1) if repeats else ""))
        for label in labels:
            out.append(html_engine_cells(r["engines"].get(label), repeats))
        if both:
            out.append(change_cell(r.get("parityChange")))
        out.append("</tr>")
    out.append("</tbody></table></div>")
    return out


def html_site_notes(r, ch, labels):
    e = html.escape
    out = []
    if ch.get("error"):
        out.append(f"<p class=bad>Chrome: {e(ch['error'])}</p>")
    if ch.get("blocked"):
        out.append(f"<p class=bad>Chrome was shown a {e(ch['blocked'])} page; this site is left out "
                   f"of the aggregates.</p>")
    for label in labels:
        en = r["engines"].get(label)
        if en and en.get("blocked"):
            out.append(f"<p class=bad>{e(label)} was shown a {e(en['blocked'])} page.</p>")
        if en and en.get("failedRuns"):
            count = "it counts" if en["failedRuns"] == 1 else "they count"
            out.append(f"<p class=bad>{e(label)} failed in {en['failedRuns']} of {len(en['visualRuns'])} visual runs "
                       f"({e(en['error'])}); {count} in its median but not in its spread.</p>")
    if r.get("unstable"):
        out.append(f"<p class=bad>Unstable: its visual runs spread over more than {UNSTABLE_SPREAD:g} parity points "
                   f"({e(spread_text(r, labels))}); this site is left out of the visual aggregates.</p>")
    for name, cap in inventory_caps(r, labels):
        out.append(f"<p class=muted>{e(name)}'s component inventory stopped at its cap of {cap}; the whole-page "
                   f"placement leaves out the part of the page past that point.</p>")
    return out


def html_engine_figure(site_id, label, en):
    e = html.escape
    v = en["visual"]
    error = f" · <span class=bad>{e(en['error'])}</span>" if en.get("error") else ""
    runs = en.get("visualRuns")
    shown = f" · median of {len(runs)} runs, run {en['shownRun']} shown" if runs else ""
    return (f"<figure><img loading=lazy src='img/{e(site_id)}-{e(label)}.jpg' alt=''>"
            f"<figcaption>{e(label)} · parity {fmt(v.get('parity'), 1)} · SSIM {fmt(v.get('ssim'), 3)} · "
            f"first paint {fmt(en.get('firstPaintMs'))} ms · images loaded {fmt(en.get('firstRenderMs'))} ms"
            + shown + error + "</figcaption></figure>")


def html_site_shots(r, ch, labels, base_label):
    e = html.escape
    runs = ch.get("visualRuns")
    reference = f" · reference run {ch['reference']} of {len(runs)}" if runs else ""
    out = ["<div class=shots>",
           f"<figure><img loading=lazy src='img/{e(r['id'])}-{e(base_label)}.jpg' alt=''>"
           f"<figcaption>Chrome · FCP {fmt(ch.get('fcp'))} ms · LCP {fmt(ch.get('lcp'))} ms · "
           f"load {fmt(ch.get('load'))} ms · CLS {fmt(ch.get('cls'), 3)}{reference}</figcaption></figure>"]
    for label in labels:
        en = r["engines"].get(label)
        if en:
            out.append(html_engine_figure(r["id"], label, en))
    if labels and labels[-1] in r["engines"]:
        out.append(f"<figure><img loading=lazy src='img/{e(r['id'])}-{e(labels[-1])}-diff.jpg' alt=''>"
                   f"<figcaption>difference vs Chrome ({e(labels[-1])})</figcaption></figure>")
    out.append("</div>")
    if r.get("film"):
        out.append("<p class=muted>Chrome filmstrip</p><div class=film>" + "".join(
            f"<figure><img loading=lazy src='img/{e(f['file'])}' alt=''><figcaption>{f['ms']} ms</figcaption></figure>"
            for f in r["film"]) + "</div>")
    return out


def html_full_pages(r, ch, labels, base_label):
    e = html.escape
    out = ["<details><summary>Full page</summary><div class=fulls>",
           f"<figure><img loading=lazy src='img/{e(r['id'])}-{e(base_label)}-full.jpg' alt=''>"
           f"<figcaption>Chrome · {fmt(ch.get('docH'))} px</figcaption></figure>"]
    for label in labels:
        en = r["engines"].get(label)
        if en:
            out.append(f"<figure><img loading=lazy src='img/{e(r['id'])}-{e(label)}-full.jpg' alt=''>"
                       f"<figcaption>{e(label)} · {fmt(en.get('docH'))} px</figcaption></figure>")
    out.append("</div></details>")
    return out


def html_run_figure(name, title, run, caption):
    e = html.escape
    img = f"<img loading=lazy src='img/{e(name)}-run{run['run']}.jpg' alt=''>" if run.get("screenshot") else ""
    notes = [f"{run['blocked']} page" if run.get("blocked") else None, run.get("error")]
    extra = "".join(f" · <span class=bad>{e(n)}</span>" for n in notes if n)
    return f"<figure>{img}<figcaption>{e(title)} run {run['run']} · {e(caption)}{extra}</figcaption></figure>"


def html_visual_runs(r, labels, base_label):
    ch = r.get("chrome") or {}
    groups = []
    if ch.get("visualRuns"):
        groups.append((base_label, "Chrome", [
            (x, "reference" if x["run"] == ch.get("reference") else f"{fmt(x.get('parity'), 1)} against the reference")
            for x in ch["visualRuns"]]))
    for label in labels:
        en = r["engines"].get(label) or {}
        if en.get("visualRuns"):
            groups.append((label, label, [
                (x, f"parity {fmt(x.get('parity'), 1)}" + (", shown" if x["run"] == en.get("shownRun") else ""))
                for x in en["visualRuns"]]))
    if not groups:
        return []
    failed = any(e.get("failedRuns") for e in r["engines"].values())
    out = [f"<details{' open' if r.get('unstable') or failed else ''}><summary>Visual runs</summary>"]
    for name, title, runs in groups:
        out.append("<div class=film>" + "".join(html_run_figure(f"{r['id']}-{name}", title, x, caption)
                                                 for x, caption in runs) + "</div>")
    out.append("</details>")
    return out


def short_path(path):
    steps = path.split(">")
    return path if len(steps) <= 4 else "…>" + ">".join(steps[-4:])


def path_cell(path, ns_path=None):
    e = html.escape
    parts = [f'<code title="{e(path)}">{e(short_path(path))}</code>'] if path else []
    if ns_path and ns_path != path:
        parts.append(f'<code class=muted title="{e(ns_path)}">NS {e(short_path(ns_path))}</code>')
    return "<td>" + ("<br>".join(parts) or "–") + "</td>"


def pairing_text(comp):
    return (f"; paired {comp['byPath']} by DOM path, {comp['byKey']} by key and {comp['byPathOnly']} by DOM path "
            f"alone, left unpaired {comp['unmatched']} in Chrome and {comp['nsUnmatched']} of {comp['nsCount']} "
            f"in Nordstjernen")


def placement_text(title, comp):
    style_rate = comp.get("styleRate")
    style_txt = "–" if style_rate is None else f"{style_rate * 100:.0f}%"
    placed = f"{comp['placed']} placed"
    if "placedOnPage" in comp:
        placed += f" in their surroundings and {comp['placedOnPage']} at the same place on the page"
    left = ""
    if comp.get("leftOut") or comp.get("nsLeftOut"):
        left = (f"; left out {comp['leftOut']} Chrome and {comp['nsLeftOut']} Nordstjernen components past "
                f"the point where the other browser's inventory stopped at its cap")
    return (f"{title}: {comp['found']}/{comp['count']} Chrome components found, {placed} "
            f"(IoU ≥ 0.5){pairing_text(comp)}{left}; style agreement {style_txt}")


def html_component_tables(comp, page):
    e = html.escape
    texts = [placement_text(title, c) for title, c in (("First screen", comp), ("Whole page", page)) if c]
    out = ["<p>" + "<br>".join(e(t) for t in texts) + "</p>"]
    if not comp:
        return out
    out.append("<div class=wrap><table><thead><tr><th>Component</th><th>DOM path</th><th>Chrome x,y,w,h</th>"
               "<th>NS x,y,w,h</th><th>IoU</th><th>Paired by</th></tr></thead><tbody>")
    for w in comp.get("worst", []):
        out.append(f"<tr><td>{e(w['tag'])} “{e(w['text'])}”</td>{path_cell(w.get('path'), w.get('nsPath'))}"
                   f"<td>{e(str(w['chrome']))}</td>"
                   f"<td>{e(str(w.get('ns', 'missing')))}</td><td>{fmt(w.get('iou'), 2)}</td>"
                   f"<td>{e(w.get('by') or '–')}</td></tr>")
    out.append("</tbody></table></div>")
    if comp.get("styleDiffs"):
        out.append("<div class=wrap><table><thead><tr><th>Element</th><th>Property</th><th>Chrome</th>"
                   "<th>NS</th></tr></thead><tbody>")
        for d in comp["styleDiffs"]:
            out.append(f"<tr><td>{e(d['tag'])} “{e(d['text'])}”</td><td>{e(d['prop'])}</td>"
                       f"<td>{e(str(d['chrome']))}</td><td>{e(str(d['ns']))}</td></tr>")
        out.append("</tbody></table></div>")
    return out


def html_engine_details(label, en):
    e = html.escape
    comp = en["visual"].get("components") or {}
    page = en["visual"].get("page")
    out = [f"<details><summary>{e(label)}: phases, components and styles</summary>",
           "<p><code>" + e(" · ".join(f"{k} {fmt(en.get(k))}" for k in PHASE_KEYS)) + "</code></p>"]
    if comp or page:
        out.extend(html_component_tables(comp, page))
    if en.get("jsErrorSample"):
        out.append("<pre>" + e("\n".join(en["jsErrorSample"])) + "</pre>")
    out.append("</details>")
    return out


def html_site_section(r, labels, base_label):
    e = html.escape
    ch = r.get("chrome") or {}
    site = r.get("site") or {}
    out = [f"<section class=site id='{e(r['id'])}'><h2>{e(r['id'])} "
           f"<span class=muted>· <a href='{e(site.get('url', ''))}'>{e(site.get('url', ''))}</a></span></h2>"]
    out.extend(html_site_notes(r, ch, labels))
    out.extend(html_site_shots(r, ch, labels, base_label))
    out.extend(html_full_pages(r, ch, labels, base_label))
    out.extend(html_visual_runs(r, labels, base_label))
    for label in labels:
        en = r["engines"].get(label)
        if en:
            out.extend(html_engine_details(label, en))
    out.append("</section>")
    return out


def write_html(rows, agg, labels, base_label, meta, path):
    out = html_summary(rows, agg, labels, meta)
    out.extend(html_site_table(rows, labels, "unstable" in agg["chrome"]))
    for r in rows:
        out.extend(html_site_section(r, labels, base_label))
    out.append(f"<script>{SORT_JS}</script></main></body></html>")
    Path(path).write_text("".join(out), encoding="utf-8", errors="replace")


def gated_rows(rows, labels):
    return [r for r in rows if comparable(r) and not r.get("unstable")
            and not any((r["engines"].get(label) or {}).get("blocked") for label in labels)]


def site_drops(rows, labels, max_drop):
    oldest, newest = labels[0], labels[-1]
    gated = {r["id"] for r in gated_rows(rows, labels)}
    lines = []
    for r in rows:
        first, last = r["engines"].get(oldest), r["engines"].get(newest)
        if not first or not last:
            continue
        if more_failed_runs(first, last) > 0:
            lines.append(f"--max-drop={max_drop:g}: {r['id']}: {newest} failed in {last['failedRuns']} of "
                         f"{len(last['visualRuns'])} visual runs, {oldest} in {first['failedRuns']} of "
                         f"{len(first['visualRuns'])}")
        if r["id"] not in gated:
            continue
        a, b = first["visual"].get("parity"), last["visual"].get("parity")
        if a is None or b is None:
            continue
        noise = (r.get("parityChange") or {}).get("noise") or 0.0
        beyond = round(a - b - noise, 1)
        if beyond > max_drop:
            lines.append(f"--max-drop={max_drop:g}: {r['id']}: {newest} parity {b} is {round(a - b, 1)} below "
                         f"{oldest}'s {a}, {beyond} beyond the site's noise of {noise}")
    return lines


def threshold_failures(rows, agg, labels, min_parity, max_drop):
    oldest, newest = labels[0], labels[-1]
    failures = []
    if min_parity is not None:
        mean = agg[newest]["parityMean"]
        count = sum(1 for r in gated_rows(rows, labels) if newest in r["engines"])
        if mean is None:
            failures.append(f"--min-parity={min_parity:g}: {newest} has no stable site to average")
        elif mean < min_parity:
            failures.append(f"--min-parity={min_parity:g}: {newest} mean visual parity {mean} over "
                            f"{sites(count, 'stable site')} is below {min_parity:g}")
    if max_drop is not None:
        both = [r for r in gated_rows(rows, labels) if oldest in r["engines"] and newest in r["engines"]]
        before = mean_parity([r["engines"][oldest] for r in both])
        after = mean_parity([r["engines"][newest] for r in both])
        if not both:
            failures.append(f"--max-drop={max_drop:g}: no stable site that both {oldest} and {newest} captured")
        elif round(before - after, 1) > max_drop:
            failures.append(f"--max-drop={max_drop:g}: {newest} mean visual parity {after} is "
                            f"{round(before - after, 1)} below {oldest}'s {before} over the "
                            f"{sites(len(both), 'stable site')} both captured")
        failures += site_drops(rows, labels, max_drop)
    return failures


def site_set_warning(rows, labels):
    oldest, newest = labels[0], labels[-1]
    gaps = []
    for have, lack in ((oldest, newest), (newest, oldest)):
        ids = [r["id"] for r in rows if comparable(r) and have in r["engines"] and lack not in r["engines"]]
        if ids:
            gaps.append(f"only {have} has {', '.join(ids)}")
    if gaps:
        print(f"compare: {'; '.join(gaps)}; --max-drop compares the sites both have", file=sys.stderr)


def check_thresholds(rows, agg, labels, min_parity, max_drop):
    if min_parity is None and max_drop is None:
        return 0
    checked = [labels[-1]] if max_drop is None else [labels[0], labels[-1]]
    missing = [label for label in dict.fromkeys(checked) if not agg[label]["sites"]]
    if missing:
        print(f"compare: no comparable captures of {', '.join(missing)} to check the thresholds against",
              file=sys.stderr)
        return 2
    if max_drop is not None:
        site_set_warning(rows, labels)
    failures = threshold_failures(rows, agg, labels, min_parity, max_drop)
    for line in failures:
        print(f"FAIL {line}")
    if not failures:
        given = [f"--min-parity={min_parity:g}" if min_parity is not None else "",
                 f"--max-drop={max_drop:g}" if max_drop is not None else ""]
        print("thresholds met: " + " ".join(x for x in given if x))
    return 1 if failures else 0


def parse_args():
    p = argparse.ArgumentParser(description="Compare Nordstjernen captures with the Chrome baseline. Exits 1 when "
                                            "a threshold fails, 2 on usage errors, missing captures or an "
                                            "unexpected error.")
    p.add_argument("--out", default="sitebench-out", help="capture root containing one directory per label")
    p.add_argument("--base", default="chrome")
    p.add_argument("--labels", default="nordstjernen", help="comma-separated Nordstjernen labels, oldest first")
    p.add_argument("--report", default=None, help="report directory (default OUT/report)")
    p.add_argument("--viewport", default="1280x800")
    p.add_argument("--min-parity", type=float, default=None, metavar="P",
                   help="fail when the newest label's mean visual parity is below P")
    p.add_argument("--max-drop", type=float, default=None, metavar="D",
                   help="fail when the newest label's mean visual parity over the stable sites both labels have "
                        "is more than D below the oldest's, or a stable site's parity is, beyond the site's "
                        "noise, or when a site failed in more of the newest label's visual runs")
    a = p.parse_args()
    a.labels = [label for label in a.labels.split(",") if label]
    m = re.fullmatch(r"(\d+)x(\d+)", a.viewport)
    if not m:
        p.error("--viewport wants WxH")
    a.width, a.height = int(m.group(1)), int(m.group(2))
    if a.min_parity is not None and not (a.labels and 0 <= a.min_parity <= 100):
        p.error("--min-parity wants a value from 0 to 100 and at least one label")
    if a.max_drop is not None and not (len(a.labels) >= 2 and a.max_drop >= 0):
        p.error("--max-drop wants a value of 0 or more and two labels to compare")
    return a


def main():
    a = parse_args()
    out = Path(a.out)
    labels = a.labels
    report = Path(a.report) if a.report else out / "report"
    img_dir = report / "img"
    img_dir.mkdir(parents=True, exist_ok=True)
    vw, vh = a.width, a.height

    ids = []
    for d in [out / a.base] + [out / label for label in labels]:
        if d.is_dir():
            for sub in sorted(d.iterdir()):
                if (sub / "metrics.json").exists() and sub.name not in ids:
                    ids.append(sub.name)
    if not ids:
        print(f"compare: no captures under {out}", file=sys.stderr)
        sys.exit(2)
    rows = [analyse_site(i, out, a.base, labels, img_dir, vw, vh) for i in ids]
    agg = aggregate(rows, labels)

    chrome = capture_info(out / a.base, ids)
    builds = {label: capture_info(out / label, ids) for label in labels}
    meta = {"generated": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
            "chromeVersion": "Chrome " + ", ".join(chrome["versions"]) if chrome["versions"] else "",
            "chromeLocale": ", ".join(chrome["locales"]),
            "nsVersions": sorted({v for b in builds.values() for v in b["versions"]}), "builds": builds,
            "viewport": a.viewport, "labels": labels}
    (report / "summary.json").write_text(json.dumps({"meta": meta, "aggregate": agg, "sites": rows}, indent=1),
                                         encoding="utf-8", errors="replace")
    write_markdown(rows, agg, labels, a.base, meta, report / "summary.md")
    write_html(rows, agg, labels, a.base, meta, report / "index.html")
    for label in labels:
        g = agg[label]
        print(f"{label}: loaded {g['loaded']}/{g['sites']}, parity {g['parityMean']}, "
              f"first paint ÷ FCP {fmt_ratio(g['firstRenderVsFcpGeomean'])}, "
              f"main CPU ÷ Chrome {fmt_ratio(g['mainThreadVsChromeGeomean'])}, "
              f"memory ÷ Chrome {fmt_ratio(g['memoryVsChromeGeomean'])}")
    print(f"report: {report / 'index.html'}")
    sys.exit(check_thresholds(rows, agg, labels, a.min_parity, a.max_drop))


if __name__ == "__main__":
    try:
        main()
    except Exception:
        traceback.print_exc()
        sys.exit(2)
