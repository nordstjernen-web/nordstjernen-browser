#!/usr/bin/env python3
"""ns-capture.py — records screenshots, phase timings and resource use for each site in Nordstjernen."""

import argparse
import concurrent.futures
import json
import os
import platform
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def default_binary():
    for name in ("nordstjernen", "nordstjernen.exe"):
        p = ROOT / "builddir" / "src" / "gtk" / name
        if p.exists():
            return str(p)
    return str(ROOT / "builddir" / "src" / "gtk" / "nordstjernen")


def read_sites(path, only, category):
    sites = []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        site = {"id": parts[0], "category": parts[1], "url": parts[2]}
        if only and site["id"] not in only:
            continue
        if category and site["category"] not in category:
            continue
        sites.append(site)
    return sites


def engine_version(binary):
    try:
        rev = subprocess.run(["git", "-C", str(Path(binary).resolve().parent), "describe", "--always",
                              "--dirty", "--tags"], capture_output=True, text=True, timeout=10).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        rev = ""
    return f"nordstjernen {rev}".strip()


def run_measured(cmd, env, timeout_s):
    start = time.monotonic()
    proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            start_new_session=True)
    timed_out = False
    try:
        out, err = proc.communicate(timeout=timeout_s)
    except subprocess.TimeoutExpired:
        timed_out = True
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except (OSError, AttributeError):
            proc.kill()
        out, err = proc.communicate()
    wall = (time.monotonic() - start) * 1000.0
    return {
        "rc": proc.returncode, "timed_out": timed_out, "wall_ms": round(wall, 1),
        "stdout": out.decode("utf-8", "replace"), "stderr": err.decode("utf-8", "replace"),
        "usage": None,
    }


def run_with_rusage(cmd, env, timeout_s):
    if not hasattr(os, "wait4"):
        return run_measured(cmd, env, timeout_s)
    out_f = tempfile.TemporaryFile()
    err_f = tempfile.TemporaryFile()
    start = time.monotonic()
    proc = subprocess.Popen(cmd, env=env, stdout=out_f, stderr=err_f, start_new_session=True)
    deadline = start + timeout_s
    timed_out = False
    status = None
    ru = None
    while True:
        pid, status, ru = os.wait4(proc.pid, os.WNOHANG)
        if pid:
            break
        if time.monotonic() > deadline:
            timed_out = True
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except OSError:
                proc.kill()
            pid, status, ru = os.wait4(proc.pid, 0)
            break
        time.sleep(0.01)
    wall = (time.monotonic() - start) * 1000.0
    proc.returncode = os.waitstatus_to_exitcode(status) if status is not None else None
    out_f.seek(0)
    err_f.seek(0)
    rss_kb = ru.ru_maxrss / (1024 if sys.platform == "darwin" else 1)
    return {
        "rc": proc.returncode, "timed_out": timed_out, "wall_ms": round(wall, 1),
        "stdout": out_f.read().decode("utf-8", "replace"),
        "stderr": err_f.read().decode("utf-8", "replace"),
        "usage": {"cpu_ms": round((ru.ru_utime + ru.ru_stime) * 1000.0, 1),
                  "user_ms": round(ru.ru_utime * 1000.0, 1),
                  "sys_ms": round(ru.ru_stime * 1000.0, 1),
                  "max_rss_mb": round(rss_kb / 1024.0, 1)},
    }


def parse_prefixed_json(text, prefix):
    for line in text.splitlines():
        if line.startswith(prefix):
            try:
                return json.loads(line[len(prefix):])
            except json.JSONDecodeError:
                return None
    return None


JS_ERROR = re.compile(r"^\[js\] .*(Error|rejected|unhandled|not a function|is not defined|undefined)", re.I)


def js_errors(stderr):
    errs = [l[5:].strip() for l in stderr.splitlines() if JS_ERROR.match(l)]
    seen = []
    for e in errs:
        if e not in seen:
            seen.append(e[:300])
    return len(errs), seen[:8]


def fetch_error(stderr):
    m = re.search(r"headless: fetch (?:error|failed): (.*)", stderr)
    return m.group(1).strip() if m else None


def crop_viewport(full, dest, width, height):
    try:
        from PIL import Image
    except ImportError:
        shutil.copyfile(full, dest)
        return
    with Image.open(full) as im:
        im = im.convert("RGB")
        canvas = Image.new("RGB", (width, height), (255, 255, 255))
        canvas.paste(im.crop((0, 0, min(width, im.width), min(height, im.height))), (0, 0))
        canvas.save(dest)


def median(values):
    v = sorted(x for x in values if isinstance(x, (int, float)))
    if not v:
        return None
    mid = len(v) // 2
    return v[mid] if len(v) % 2 else (v[mid - 1] + v[mid]) / 2


def isolated_env(home):
    env = dict(os.environ)
    env["HOME"] = home
    env["XDG_CACHE_HOME"] = os.path.join(home, ".cache")
    env["XDG_CONFIG_HOME"] = os.path.join(home, ".config")
    env["XDG_DATA_HOME"] = os.path.join(home, ".local", "share")
    if hasattr(os, "geteuid") and os.geteuid() == 0:
        env.setdefault("NS_ALLOW_ROOT", "1")
    for key in ("XDG_CACHE_HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME"):
        os.makedirs(env[key], exist_ok=True)
    return env


def capture_site(site, a, probe_src):
    out_dir = Path(a.out) / a.label / site["id"]
    if a.skip_existing and (out_dir / "metrics.json").exists():
        return None
    out_dir.mkdir(parents=True, exist_ok=True)
    viewport = f"--viewport={a.width}x{a.height}"
    perf_runs = []
    for _ in range(a.runs):
        home = tempfile.mkdtemp(prefix="ns-sitebench-")
        try:
            png = os.path.join(home, "perf.png")
            cmd = [a.bin, "--headless", "--timing", viewport, "--settle-ms=0", "--time-ms=0",
                   f"--dump=png:{png}", site["url"]]
            r = run_with_rusage(cmd, isolated_env(home), a.timeout)
        finally:
            shutil.rmtree(home, ignore_errors=True)
        timing = parse_prefixed_json(r["stdout"], "timing: ") or {}
        perf_runs.append({
            "rc": r["rc"], "timed_out": r["timed_out"], "wall_ms": r["wall_ms"],
            "usage": r["usage"], "timing": timing,
        })

    home = tempfile.mkdtemp(prefix="ns-sitebench-")
    try:
        full = out_dir / "full.png"
        for stale in ("full.png", "full-initial.png", "viewport.png"):
            (out_dir / stale).unlink(missing_ok=True)
        cmd = [a.bin, "--headless", "--timing", viewport, f"--settle-ms={a.settle_ms}",
               f"--time-ms={a.time_ms}", f"--dump=png:{full}", f"--eval={probe_src}", site["url"]]
        visual = run_with_rusage(cmd, isolated_env(home), a.timeout)
    finally:
        shutil.rmtree(home, ignore_errors=True)
    (out_dir / "stderr.log").write_text(visual["stderr"][-200000:], encoding="utf-8")
    probe = parse_prefixed_json(visual["stdout"], "eval: ")
    vtiming = parse_prefixed_json(visual["stdout"], "timing: ") or {}
    if probe is not None:
        (out_dir / "probe.json").write_text(json.dumps(probe), encoding="utf-8")
    if full.exists():
        crop_viewport(full, out_dir / "viewport.png", a.width, a.height)
    n_err, err_sample = js_errors(visual["stderr"])

    summary = {
        "firstPaintMs": median(p["timing"].get("first_paint_ms") for p in perf_runs),
        "firstPaintCpuMs": median(p["timing"].get("first_paint_cpu_ms") for p in perf_runs),
        "firstRenderMs": median(p["timing"].get("first_render_ms") for p in perf_runs),
        "firstRenderCpuMs": median(p["timing"].get("first_render_cpu_ms") for p in perf_runs),
        "fetchMs": median(p["timing"].get("fetch_ms") for p in perf_runs),
        "parseMs": median(p["timing"].get("parse_ms") for p in perf_runs),
        "styleMs": median(p["timing"].get("style_ms") for p in perf_runs),
        "scriptMs": median(p["timing"].get("script_ms") for p in perf_runs),
        "layoutMs": median(p["timing"].get("relayout_ms") for p in perf_runs),
        "imagesMs": median(p["timing"].get("images_ms") for p in perf_runs),
        "paintMs": median(p["timing"].get("paint_ms") for p in perf_runs),
        "netWaitMs": median(p["timing"].get("net_wait_ms") for p in perf_runs),
        "processWallMs": median(p["wall_ms"] for p in perf_runs),
        "processCpuMs": median((p["usage"] or {}).get("cpu_ms") for p in perf_runs),
        "maxRssMb": median((p["usage"] or {}).get("max_rss_mb") for p in perf_runs),
        "settledMainThreadMs": vtiming.get("cpu_ms"),
        "settledProcessCpuMs": (visual["usage"] or {}).get("cpu_ms"),
        "settledMaxRssMb": (visual["usage"] or {}).get("max_rss_mb"),
        "settledWallMs": visual["wall_ms"],
        "relayouts": vtiming.get("relayouts"),
        "nodes": (probe or {}).get("nodes") or vtiming.get("nodes"),
        "jsErrors": n_err,
    }
    status = vtiming.get("status") or next((p["timing"].get("status") for p in perf_runs
                                             if p["timing"].get("status")), None)
    error = fetch_error(visual["stderr"])
    if visual["timed_out"]:
        error = error or f"timed out after {a.timeout}s"
    elif visual["rc"] not in (0, None) and not full.exists():
        error = error or f"exit code {visual['rc']}"
    result = {
        "engine": "nordstjernen", "version": a.version, "site": site,
        "viewport": {"width": a.width, "height": a.height},
        "capturedAt": datetime.now(timezone.utc).isoformat(), "host": platform.node(),
        "cpus": os.cpu_count(), "status": status, "error": error,
        "settings": {"settleMs": a.settle_ms, "timeMs": a.time_ms, "runs": a.runs},
        "summary": summary, "perfRuns": perf_runs, "visualTiming": vtiming,
        "visualRc": visual["rc"], "visualTimedOut": visual["timed_out"],
        "jsErrorSample": err_sample,
    }
    (out_dir / "metrics.json").write_text(json.dumps(result, indent=1), encoding="utf-8")
    return result


def main():
    p = argparse.ArgumentParser(description="Capture Nordstjernen screenshots and metrics for the site list.")
    p.add_argument("--bin", default=os.environ.get("NS_BIN") or default_binary())
    p.add_argument("--sites", default=str(HERE / "sites.tsv"))
    p.add_argument("--out", default="sitebench-out")
    p.add_argument("--label", default="nordstjernen")
    p.add_argument("--only", default="")
    p.add_argument("--category", default="")
    p.add_argument("--viewport", default="1280x800")
    p.add_argument("--runs", type=int, default=1, help="cold first-render runs per site (median reported)")
    p.add_argument("--settle-ms", type=int, default=2000)
    p.add_argument("--time-ms", type=int, default=1000)
    p.add_argument("--timeout", type=int, default=120, help="seconds per browser invocation")
    p.add_argument("--jobs", type=int, default=1)
    p.add_argument("--skip-existing", action="store_true")
    a = p.parse_args()
    m = re.fullmatch(r"(\d+)x(\d+)", a.viewport)
    if not m:
        sys.exit("ns-capture: --viewport wants WxH")
    a.width, a.height = int(m.group(1)), int(m.group(2))
    if not os.path.exists(a.bin):
        sys.exit(f"ns-capture: browser binary not found: {a.bin} (build it or pass --bin)")
    only = set(filter(None, a.only.split(",")))
    category = set(filter(None, a.category.split(",")))
    sites = read_sites(a.sites, only, category)
    if not sites:
        sys.exit("ns-capture: no sites selected")
    a.version = engine_version(a.bin)
    probe_src = (HERE / "probe.js").read_text(encoding="utf-8")
    print(f"ns-capture: {a.version}, {len(sites)} sites, viewport {a.width}x{a.height}, "
          f"runs {a.runs}, jobs {a.jobs}", flush=True)

    def report(site, res):
        if res is None:
            return
        s = res["summary"]
        print(f"{site['id']:<20} {str(res['status'] or '-'):<4} first-paint={s['firstPaintMs']} "
              f"images-loaded={s['firstRenderMs']} "
              f"cpu={s['processCpuMs']} rss={s['maxRssMb']}MB nodes={s['nodes']} jsErr={s['jsErrors']}"
              + (f"  ERR {res['error']}" if res["error"] else ""), flush=True)

    if a.jobs <= 1:
        for site in sites:
            report(site, capture_site(site, a, probe_src))
    else:
        with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
            futures = {pool.submit(capture_site, s, a, probe_src): s for s in sites}
            for f in concurrent.futures.as_completed(futures):
                report(futures[f], f.result())


if __name__ == "__main__":
    main()
