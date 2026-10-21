#!/usr/bin/env python3
"""snapshot.py — freezes sites into script-free local copies and compares every element's box with Chrome's."""

import argparse
import base64
import collections
import concurrent.futures
import functools
import hashlib
import html
import http.server
import json
import math
import os
import re
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import traceback
import urllib.parse
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
TOL = 2
MIN_ELEMENTS = 100
WINDOW_PAD = [0, 87]
RECTS_ATTR = "data-sitebench-rects"
VIEW_ATTR = "data-sitebench-viewport"
DEAD_PROXY = "http://127.0.0.1:9"
PROBE = r"""(function(){
  var out = [];
  function walk(e, path) {
    var r = e.getBoundingClientRect();
    var cls = (typeof e.className === 'string' && e.className) ? '.' + e.className.trim().split(/\s+/).slice(0, 2).join('.') : '';
    out.push(path + '\t' + e.tagName.toLowerCase() + (e.id ? '#' + e.id : '') + cls + '\t' + [Math.round(r.left), Math.round(r.top + scrollY), Math.round(r.width), Math.round(r.height)].join(','));
    var k = 0;
    for (var c = e.firstElementChild; c; c = c.nextElementSibling) walk(c, path + '/' + (k++));
  }
  walk(document.body, 'b');
  return out.join('\n');
})()"""
PROBE_SCRIPT = ("<script>addEventListener('load', function () { var go = function () { setTimeout(function () {"
                " var d = document.documentElement; d.setAttribute('%s', innerWidth + 'x' + innerHeight);"
                " d.setAttribute('%s', %s); }, %d); };"
                " (document.fonts && document.fonts.ready ? document.fonts.ready : Promise.resolve()).then(go, go); });"
                "</script>")
STILL = "<style>*,*::before,*::after{animation:none!important;transition:none!important}</style>"
SERIALIZE = r"""new Promise(done => setTimeout(() => {
  for (const el of document.querySelectorAll('input, textarea, select')) {
    if (el.type === 'checkbox' || el.type === 'radio') el.toggleAttribute('checked', el.checked);
    else if (el.tagName === 'SELECT') for (const o of el.options) o.toggleAttribute('selected', o.selected);
    else if (el.tagName === 'TEXTAREA') el.textContent = el.value;
    else if (!['password', 'file', 'submit', 'reset', 'button', 'image'].includes(el.type)) el.setAttribute('value', el.value);
  }
  const rules = sheet => { try { return Array.from(sheet.cssRules, r => r.cssText).join('\n'); } catch (e) { return null; } };
  const parsed = css => { try { const s = new CSSStyleSheet(); s.replaceSync(css); return rules(s); } catch (e) { return null; } };
  for (const el of document.querySelectorAll('style')) {
    const live = el.sheet ? rules(el.sheet) : null;
    if (live !== null && live !== parsed(el.textContent)) el.textContent = live;
  }
  const adopted = (document.adoptedStyleSheets || []).map(s => {
    const css = rules(s), media = s.media ? s.media.mediaText : '';
    return css && media ? '@media ' + media + ' {\n' + css + '\n}' : css;
  }).filter(Boolean);
  if (adopted.length) {
    const el = document.createElement('style');
    el.textContent = adopted.join('\n');
    (document.body || document.documentElement).appendChild(el);
  }
  const dt = document.doctype;
  done({html: document.documentElement.outerHTML, base: document.baseURI, compat: document.compatMode,
        doctype: dt ? '<!DOCTYPE ' + dt.name + (dt.publicId ? ' PUBLIC "' + dt.publicId + '"' : '') +
                      (dt.systemId ? (dt.publicId ? '' : ' SYSTEM') + ' "' + dt.systemId + '"' : '') + '>' : ''});
}, %d))"""
TRAMPOLINE = ("import fcntl, os, sys; r, w = (fcntl.fcntl(int(f), fcntl.F_DUPFD, 10) for f in sys.argv[1:3]); "
              "os.dup2(r, 3); os.dup2(w, 4); os.execv(sys.argv[3], sys.argv[3:])")
ASSET_EXT = re.compile(r"\.(svg|png|jpe?g|webp|gif|avif|ico|bmp|woff2?|ttf|otf)$", re.I)
IMPORT = re.compile(r"""(/\*.*?\*/|"(?:[^"\\\n]|\\.)*"|'(?:[^'\\\n]|\\.)*')"""
                    r"""|@import\s+(?:url\(\s*(?:"([^"]*)"|'([^']*)'|([^"'()\s]*))\s*\)|"([^"]*)"|'([^']*)')"""
                    r"""([^;{}]*)(?:;|$)""", re.I | re.S)
CSS_TOKEN = re.compile(r"""(/\*.*?\*/)|(@namespace\b[^;]*;?)"""
                       r"""|(?<![\w-])url\(\s*(?:"((?:[^"\\]|\\.)*)"|'((?:[^'\\]|\\.)*)'|([^"'()\s]*))\s*\)"""
                       r"""|("(?:[^"\\\n]|\\.)*"|'(?:[^'\\\n]|\\.)*')|(?<![\w-])((?:-webkit-)?image-set)\(|(\()|(\))""",
                       re.I | re.S)
HTML_START = re.compile(r"\s*<(?:!doctype\s+html|html|head|body)\b", re.I)
STYLE_END = re.compile(r"</(style)", re.I)
EVENT_ATTR = re.compile(r"""\son[a-z]+=("[^"]*"|'[^']*')""", re.I)
CHARSET_META = re.compile(r"<meta\b[^>]*\bcharset\s*=[^>]*>", re.I)
IMAGES = (".png", ".jpg", ".gif", ".bmp", ".webp", ".avif")
SNIFF = ((b"\x89PNG", ".png"), (b"\xff\xd8\xff", ".jpg"), (b"GIF8", ".gif"), (b"wOF2", ".woff2"), (b"wOFF", ".woff"),
         (b"\x00\x01\x00\x00", ".ttf"), (b"OTTO", ".otf"), (b"BM", ".bmp"))


def read_sites(path, only, category):
    sites = []
    for line in open(path, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        parts = line.rstrip("\n").split("\t")
        if len(parts) < 3:
            continue
        if (only and parts[0] not in only) or (category and parts[1] not in category):
            continue
        sites.append({"id": parts[0], "category": parts[1], "url": parts[2]})
    return sites


def default_binary():
    return str(ROOT / "builddir" / "src" / "gtk" / "nordstjernen")


def default_chrome():
    if os.environ.get("CHROME_BIN"):
        return os.environ["CHROME_BIN"]
    if sys.platform == "darwin":
        return "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
    for name in ("google-chrome", "google-chrome-stable", "chromium", "chromium-browser"):
        if shutil.which(name):
            return shutil.which(name)
    return "google-chrome"


def chrome_version(chrome):
    try:
        out = subprocess.run([chrome, "--version"], capture_output=True, text=True, timeout=60).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    m = re.search(r"\d+(?:\.\d+)+", out)
    return m.group(0) if m else None


def chrome_user_agent(version):
    system = "Macintosh; Intel Mac OS X 10_15_7" if sys.platform == "darwin" else "X11; Linux x86_64"
    major = (version or "0").split(".")[0]
    return f"Mozilla/5.0 ({system}) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/{major}.0.0.0 Safari/537.36"


def lang_tag(locale):
    return locale.split(".")[0].replace("_", "-")


def locale_env(locale):
    return dict(os.environ, LANG=locale, LC_ALL=locale, TZ="UTC")


def chrome_args(a):
    lang = lang_tag(a.locale)
    return ["--disable-gpu", "--hide-scrollbars", "--no-first-run", "--no-default-browser-check",
            "--blink-settings=preferredColorScheme=1", "--force-color-profile=srgb", "--font-render-hinting=none",
            "--lang=" + lang, "--accept-lang=%s,%s" % (lang, lang.split("-")[0])]


def fail(message):
    print("snapshot: " + message, file=sys.stderr)
    sys.exit(2)


def kill_group(proc):
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError, AttributeError):
        proc.kill()


def complete_dom(dom):
    return len(dom) >= 2000 and dom.rstrip().lower().endswith("</html>")


def read_line(path):
    try:
        return path.read_text().strip()
    except OSError:
        return None


def dump_dom(chrome, url, a, budget_ms, window):
    dom = ""
    for _ in range(2):
        dom = dump_dom_once(chrome, url, a, budget_ms, window)
        if complete_dom(dom):
            break
    return dom


def dump_dom_once(chrome, url, a, budget_ms, window):
    prof = tempfile.mkdtemp(prefix="sitebench-chrome-")
    out_path = os.path.join(prof, "dom.html")
    try:
        with open(out_path, "wb") as out:
            proc = subprocess.Popen([chrome, "--headless=new", *chrome_args(a), "--proxy-server=" + DEAD_PROXY,
                                     "--user-data-dir=" + prof, "--window-size=%d,%d" % tuple(window),
                                     "--virtual-time-budget=%d" % budget_ms, "--dump-dom", url],
                                    stdout=out, stderr=subprocess.DEVNULL, env=locale_env(a.locale),
                                    start_new_session=True)
            deadline = time.time() + budget_ms / 1000.0 + 60
            while proc.poll() is None and time.time() < deadline:
                with open(out_path, "rb") as f:
                    if f.read().rstrip().lower().endswith(b"</html>"):
                        break
                time.sleep(0.25)
            if proc.poll() is None:
                kill_group(proc)
            proc.wait()
        return open(out_path, "rb").read().decode("utf-8", "replace")
    finally:
        shutil.rmtree(prof, ignore_errors=True)


class Cdp:
    def __init__(self, chrome, args, env):
        self.prof = tempfile.mkdtemp(prefix="sitebench-chrome-")
        cmd_r, self.cmd_w = os.pipe()
        self.out_r, out_w = os.pipe()
        self.proc = subprocess.Popen([sys.executable, "-c", TRAMPOLINE, str(cmd_r), str(out_w), chrome,
                                      "--headless=new", "--remote-debugging-pipe", "--user-data-dir=" + self.prof,
                                      *args, "about:blank"], pass_fds=(cmd_r, out_w), stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL, env=env, start_new_session=True)
        os.close(cmd_r)
        os.close(out_w)
        self.buf, self.serial, self.events = b"", 0, []

    def close(self):
        kill_group(self.proc)
        self.proc.wait()
        os.close(self.cmd_w)
        os.close(self.out_r)
        shutil.rmtree(self.prof, ignore_errors=True)

    def message(self, deadline):
        while b"\0" not in self.buf:
            left = deadline - time.time()
            if left <= 0 or not select.select([self.out_r], [], [], left)[0]:
                raise TimeoutError
            chunk = os.read(self.out_r, 1 << 20)
            if not chunk:
                raise EOFError
            self.buf += chunk
        raw, self.buf = self.buf.split(b"\0", 1)
        return json.loads(raw)

    def call(self, method, params=None, session=None, timeout=30):
        self.serial += 1
        msg = {"id": self.serial, "method": method, "params": params or {}}
        if session:
            msg["sessionId"] = session
        os.write(self.cmd_w, json.dumps(msg).encode() + b"\0")
        deadline = time.time() + timeout
        while True:
            m = self.message(deadline)
            if m.get("id") == self.serial:
                if "error" in m:
                    raise RuntimeError(m["error"].get("message", "protocol error"))
                return m.get("result", {})
            self.events.append(m)

    def wait(self, test, timeout, start=0):
        deadline = time.time() + timeout
        i = start
        while True:
            while i < len(self.events):
                if test(self.events[i]):
                    return i
                i += 1
            try:
                self.events.append(self.message(deadline))
            except TimeoutError:
                return None


def open_page(cdp, a):
    target = next(t for t in cdp.call("Target.getTargets")["targetInfos"] if t["type"] == "page")
    session = cdp.call("Target.attachToTarget", {"targetId": target["targetId"], "flatten": True})["sessionId"]
    cdp.call("Page.enable", session=session)
    cdp.call("Page.setLifecycleEventsEnabled", {"enabled": True}, session=session)
    cdp.call("Emulation.setDeviceMetricsOverride", {"width": a.vw, "height": a.vh, "deviceScaleFactor": 1,
                                                    "mobile": False}, session=session)
    for method, params in (("Emulation.setTimezoneOverride", {"timezoneId": "UTC"}),
                           ("Emulation.setLocaleOverride", {"locale": lang_tag(a.locale)})):
        try:
            cdp.call(method, params, session=session)
        except RuntimeError:
            pass
    return session


def live_dom(chrome, url, a, user_agent):
    args = [*chrome_args(a), "--window-size=%d,%d" % (a.vw + WINDOW_PAD[0], a.vh + WINDOW_PAD[1]),
            "--user-agent=" + user_agent, "--disable-blink-features=AutomationControlled"]
    cdp = Cdp(chrome, args, locale_env(a.locale))
    try:
        session = open_page(cdp, a)
        del cdp.events[:]
        nav = cdp.call("Page.navigate", {"url": url}, session=session, timeout=60)
        if nav.get("errorText"):
            return None, nav["errorText"]
        if cdp.wait(lambda m: m.get("method") == "Page.loadEventFired", 45) is None:
            return None, "no load event in 45 s"
        cdp.wait(lambda m: m.get("method") == "Page.lifecycleEvent" and m["params"].get("name") == "networkIdle"
                 and m["params"].get("loaderId") == nav.get("loaderId"), 15)
        result = cdp.call("Runtime.evaluate", {"expression": SERIALIZE % a.settle_ms, "awaitPromise": True,
                                               "returnByValue": True}, session=session, timeout=60)
        return result.get("result", {}).get("value"), None
    except (TimeoutError, EOFError, OSError, RuntimeError, StopIteration, KeyError, ValueError) as e:
        return None, "DevTools protocol: %s" % (str(e) or type(e).__name__)
    finally:
        cdp.close()


def freeze_dom(chrome, url, a, user_agent):
    page, err = None, None
    for _ in range(2):
        page, err = live_dom(chrome, url, a, user_agent)
        if isinstance(page, dict) and complete_dom(page.get("html") or ""):
            return page, None
    return None, err or "Chrome returned %d bytes" % len((page or {}).get("html") or "")


def sniff(head):
    for magic, ext in SNIFF:
        if head.startswith(magic):
            return ext
    if head[:4] == b"RIFF" and head[8:12] == b"WEBP":
        return ".webp"
    if head[4:12] in (b"ftypavif", b"ftypavis"):
        return ".avif"
    return ".svg" if b"<svg" in head.lower() else ""


class Freezer:
    def __init__(self, base, out, prefix, user_agent, lang):
        self.base, self.out, self.prefix, self.user_agent, self.lang = base, Path(out), prefix, user_agent, lang
        self.assets = {}
        self.counts = collections.Counter()
        self.unfetched = []
        (self.out / "img").mkdir(parents=True, exist_ok=True)

    def fetch(self, url, path):
        if not url.startswith(("http://", "https://")):
            return None
        try:
            r = subprocess.run(["curl", "-s", "-L", "--compressed", "--connect-timeout", "5", "--max-time", "25",
                                "--max-filesize", "20000000",
                                "-A", self.user_agent, "-H", "Accept-Language: %s,en;q=0.9" % self.lang,
                                "-o", str(path), "-w", "%{http_code}", url],
                               capture_output=True, text=True, timeout=60)
        except (OSError, subprocess.SubprocessError):
            return None
        if r.returncode == 0 and path.exists() and path.stat().st_size > 0:
            return r.stdout.strip()
        path.unlink(missing_ok=True)
        return None

    @staticmethod
    def absolute(url, base):
        return urllib.parse.urljoin(base, url.strip())

    def local(self, url):
        url, frag = urllib.parse.urldefrag(url)
        if url not in self.assets:
            self.assets[url] = self.download(url)
        return self.assets[url] + ("#" + frag if frag else "")

    def download(self, url):
        stem = hashlib.sha256(url.encode()).hexdigest()[:12]
        tmp = self.out / "img" / (stem + ".part")
        status = self.fetch(url, tmp)
        ext = self.extension(url, tmp, status.startswith("2")) if status else None
        if ext is None:
            tmp.unlink(missing_ok=True)
            self.counts["assets_missing"] += 1
            self.unfetched.append(url)
            return self.missing(url)
        tmp.replace(self.out / "img" / (stem + ext))
        self.counts["assets"] += 1
        return "%s/img/%s%s" % (self.prefix, stem, ext)

    def missing(self, url):
        return "%s/img/missing-%s" % (self.prefix, hashlib.sha256(url.encode()).hexdigest()[:12])

    @staticmethod
    def extension(url, path, ok):
        head = path.open("rb").read(512)
        if not ok:
            ext = sniff(head)
            return ext if ext in IMAGES else None
        if HTML_START.match(head.decode("latin-1")):
            return None
        m = ASSET_EXT.search(urllib.parse.urlsplit(url).path)
        return m.group(0).lower() if m else sniff(head)

    def css(self, text, base):
        stack, out, pos = [], [], 0
        for m in CSS_TOKEN.finditer(text):
            out += [text[pos:m.start()], self.token(m, stack, base)]
            pos = m.end()
        return "".join(out) + text[pos:]

    def token(self, m, stack, base):
        url = next((g for g in m.group(3, 4, 5) if g is not None), None)
        if url is not None:
            quote = '"' if m.group(3) is not None else "'" if m.group(4) is not None else ""
            return "url(%s%s%s)" % (quote, self.css_url(url, base), quote)
        if m.group(6) and stack and stack[-1] == "set":
            return m.group(6)[0] + self.css_url(m.group(6)[1:-1], base) + m.group(6)[0]
        if m.group(7) or m.group(8):
            stack.append("set" if m.group(7) else "(")
        elif m.group(9) and stack:
            stack.pop()
        return m.group(0)

    def css_url(self, url, base):
        if not url.strip() or url.strip().startswith(("data:", "#", "about:")):
            return url
        return self.local(self.absolute(url, base))

    def sheet(self, text, base, depth):
        bodies = []

        def inline(m):
            if m.group(1):
                return m.group(0)
            url = next((g for g in m.group(2, 3, 4, 5, 6) if g), None)
            if not url or depth >= 3:
                return ""
            bodies.append(self.imported(self.absolute(url, base), m.group(7), depth + 1))
            return "\0%d\0" % (len(bodies) - 1)

        return re.sub(r"\0(\d+)\0", lambda m: bodies[int(m.group(1))], self.css(IMPORT.sub(inline, text), base))

    def imported(self, url, condition, depth):
        body = self.stylesheet(url, depth)
        layer = re.match(r"\s*layer(?:\(\s*([^)]*?)\s*\))?", condition, re.I)
        if layer:
            body = "@layer %s{\n%s\n}" % (layer.group(1) + " " if layer.group(1) else "", body)
            condition = condition[layer.end():]
        supports = re.match(r"\s*supports\(((?:[^()]|\([^()]*\))*)\)", condition, re.I)
        if supports:
            body = "@supports (%s) {\n%s\n}" % (supports.group(1), body)
            condition = condition[supports.end():]
        return "@media %s {\n%s\n}" % (condition.strip(), body) if condition.strip() else body

    def stylesheet(self, url, depth=0):
        path = self.out / ("sheet-%s.css" % hashlib.sha256(url.encode()).hexdigest()[:12])
        status = self.fetch(url, path)
        text = path.read_text(encoding="utf-8-sig", errors="replace") if status and status.startswith("2") else None
        path.unlink(missing_ok=True)
        if text is None or HTML_START.match(text):
            self.counts["stylesheets_missing"] += 1
            self.unfetched.append(url)
            return ""
        self.counts["stylesheets"] += 1
        return self.sheet(text, url, depth)

    def link(self, m):
        tag = m.group(0)
        rel = re.search(r"""\srel=["']?([a-z -]+)""", tag, flags=re.I)
        href = re.search(r'\shref="([^"]*)"', tag)
        if not rel or not href or rel.group(1).strip().lower() != "stylesheet":
            return ""
        url = self.absolute(html.unescape(href.group(1)), self.base)
        media = re.search(r'\smedia="([^"]*)"', tag)
        return '<style data-from-link="%s"%s>%s</style>' % (
            html.escape(url[-60:], quote=True), ' media="%s"' % media.group(1) if media else "",
            STYLE_END.sub(r"<\\/\1", self.stylesheet(url)))

    def src(self, m):
        url = html.unescape(m.group(2)).strip()
        if not url or url.startswith(("data:", "#")):
            return m.group(0)
        return m.group(1) + html.escape(self.local(self.absolute(url, self.base)), quote=True) + '"'

    def offline(self, m):
        url = html.unescape(m.group(2)).strip()
        if not url or url.startswith("data:"):
            return m.group(0)
        self.counts["media_offline"] += 1
        return m.group(1) + html.escape(self.missing(self.absolute(url, self.base)), quote=True) + '"'

    def attributes(self, s):
        for pattern, fn in ((r'(<(?:img|input)\b[^>]*?\ssrc=")([^"]*)"', self.src),
                            (r'(<video\b[^>]*?\sposter=")([^"]*)"', self.src),
                            (r'(<(?:image|use|feImage)\b[^>]*?\s(?:xlink:)?href=")([^"]*)"', self.src),
                            (r'(<(?:body|table|thead|tbody|tfoot|tr|td|th)\b[^>]*?\sbackground=")([^"]*)"', self.src),
                            (r'(<(?:video|audio|source|track|embed)\b[^>]*?\ssrc=")([^"]*)"', self.offline),
                            (r'(<object\b[^>]*?\sdata=")([^"]*)"', self.offline)):
            s = re.sub(pattern, fn, s, flags=re.I)
        return re.sub(r'\ssrcset="[^"]*"|\sloading="lazy"', "", s, flags=re.I)

    def run(self, page):
        s = re.sub(r"<script\b[^>]*>.*?</script\s*>", "", page["html"], flags=re.S | re.I)
        s = re.sub(r"<iframe\b[^>]*>.*?</iframe\s*>", "", s, flags=re.S | re.I)
        s = re.sub(r"<[a-zA-Z][^>]*>", lambda m: EVENT_ATTR.sub("", m.group(0)), s)
        s = re.sub(r"(<style\b[^>]*>)(.*?)(</style>)", lambda m: m.group(1) + STYLE_END.sub(
            r"<\\/\1", self.sheet(m.group(2), self.base, 0)) + m.group(3), s, flags=re.S | re.I)
        s = re.sub(r'(\sstyle=")([^"]*)"', lambda m: m.group(1) + html.escape(
            self.css(html.unescape(m.group(2)), self.base), quote=True) + '"', s)
        s = re.sub(r"<link\b[^>]*>", self.link, self.attributes(s), flags=re.I)
        s = re.sub(r"""<meta\b[^>]*http-equiv=["']?refresh[^>]*>|<base\b[^>]*>""", "", s, flags=re.I)
        s = re.sub(r"(<head\b[^>]*>)", r'\1<meta charset="utf-8">', CHARSET_META.sub("", s), count=1, flags=re.I)
        end = s.lower().find("</head>")
        s = s[:end] + STILL + s[end:] if end >= 0 else STILL + s
        s = (page["doctype"] + "\n" if page.get("doctype") else "") + s
        (self.out / "index.html").write_text(s, encoding="utf-8", errors="replace")
        title = re.search(r"<title\b[^>]*>(.*?)</title>", s, re.S | re.I)
        return dict(self.counts, htmlBytes=len(s), compatMode=page.get("compat"), missingFiles=self.unfetched,
                    title=html.unescape(title.group(1)).strip()[:120] if title else "")


def freeze_site(site, a, chrome, version):
    dest = Path(a.snapshots) / site["id"]
    if (dest / "index.html").exists() and not a.refresh:
        return site["id"], "kept"
    t0 = time.time()
    tmp = dest.with_name(dest.name + ".part")
    try:
        return site["id"], freeze_into(site, a, chrome, version, dest, tmp, t0)
    except Exception as e:
        shutil.rmtree(tmp, ignore_errors=True)
        return site["id"], "failed after %.0fs: %s" % (time.time() - t0, str(e) or type(e).__name__)


def freeze_into(site, a, chrome, version, dest, tmp, t0):
    ua = chrome_user_agent(version)
    page, err = freeze_dom(chrome, site["url"], a, ua)
    if err:
        return "failed after %.0fs: %s" % (time.time() - t0, err)
    shutil.rmtree(tmp, ignore_errors=True)
    stats = Freezer(page.get("base") or site["url"], tmp, "/" + site["id"], ua, lang_tag(a.locale)).run(page)
    meta = {"id": site["id"], "url": site["url"], "base": page.get("base"),
            "frozen": datetime.now(timezone.utc).isoformat(timespec="seconds"), "chrome": version,
            "viewport": "%dx%d" % (a.vw, a.vh), "locale": a.locale, **stats}
    (tmp / "meta.json").write_text(json.dumps(meta, indent=1) + "\n")
    shutil.rmtree(dest, ignore_errors=True)
    tmp.rename(dest)
    return "%d assets (%d missing), %d stylesheets (%d missing), %.0fs: %s" % (
        stats.get("assets", 0), stats.get("assets_missing", 0), stats.get("stylesheets", 0),
        stats.get("stylesheets_missing", 0), time.time() - t0, stats["title"] or "(no title)")


def inject_probe(src, settle_ms):
    script = PROBE_SCRIPT % (VIEW_ATTR, RECTS_ATTR, PROBE, settle_ms)
    m = re.search(r"<head\b[^>]*>", src, re.I)
    if m:
        return src[:m.end()] + script + src[m.end():]
    m = re.search(r"<html\b[^>]*>", src, re.I)
    return src[:m.end()] + script + src[m.end():] if m else script + src


def serve(root, settle_ms):
    class Handler(http.server.SimpleHTTPRequestHandler):
        extensions_map = dict(http.server.SimpleHTTPRequestHandler.extensions_map,
                              **{".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8",
                                 ".svg": "image/svg+xml"})

        def log_message(self, format, *args):
            pass

        def do_GET(self):
            path = urllib.parse.urlsplit(self.path).path
            if not path.endswith("/__rects.html"):
                return super().do_GET()
            index = self.translate_path(path[:-len("__rects.html")] + "index.html")
            if not os.path.isfile(index):
                return self.send_error(404)
            body = inject_probe(open(index, encoding="utf-8", errors="replace").read(), settle_ms).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Handler, directory=root))
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd, "http://127.0.0.1:%d/" % httpd.server_address[1]


def chrome_rects(chrome, base_url, name, a):
    url = base_url + urllib.parse.quote(name) + "/__rects.html"
    pad, got = list(WINDOW_PAD), None
    for _ in range(2):
        dom = dump_dom(chrome, url, a, 10000 + a.settle_ms, (a.vw + pad[0], a.vh + pad[1]))
        view = re.search(r'<html\b[^>]*\s%s="(\d+)x(\d+)"' % VIEW_ATTR, dom)
        rects = re.search(r'<html\b[^>]*\s%s="([^"]*)"' % RECTS_ATTR, dom)
        if not view or not rects:
            return "", "no measurement in %d bytes of DOM" % len(dom)
        got = (int(view.group(1)), int(view.group(2)))
        if got == (a.vw, a.vh):
            WINDOW_PAD[:] = pad
            return html.unescape(rects.group(1)), None
        pad = [pad[0] + a.vw - got[0], pad[1] + a.vh - got[1]]
    return "", "viewport came out %dx%d instead of %dx%d" % (got + (a.vw, a.vh))


FONT_CACHE = os.path.join(tempfile.gettempdir(), "sitebench-fontconfig-%s" % (os.getuid() if hasattr(os, "getuid") else "user"))


def share_font_cache(cache_home):
    os.makedirs(FONT_CACHE, exist_ok=True)
    try:
        os.symlink(FONT_CACHE, os.path.join(cache_home, "fontconfig"))
    except OSError:
        pass


def isolated_env(home, locale):
    env = locale_env(locale)
    env["HOME"] = home
    for key, sub in (("XDG_CACHE_HOME", ".cache"), ("XDG_CONFIG_HOME", ".config"), ("XDG_DATA_HOME", ".local/share")):
        env[key] = os.path.join(home, sub)
        os.makedirs(env[key], exist_ok=True)
    share_font_cache(env["XDG_CACHE_HOME"])
    for key in ("http_proxy", "https_proxy", "all_proxy", "HTTPS_PROXY", "ALL_PROXY"):
        env[key] = DEAD_PROXY
    env["no_proxy"] = env["NO_PROXY"] = "127.0.0.1,localhost"
    if hasattr(os, "geteuid") and os.geteuid() == 0:
        env.setdefault("NS_ALLOW_ROOT", "1")
    return env


def ns_rects(binary, url, a):
    home = tempfile.mkdtemp(prefix="sitebench-ns-")
    try:
        proc = subprocess.Popen([binary, "--headless", "--viewport=%dx%d" % (a.vw, a.vh), "--settle-ms=%d" % a.settle_ms,
                                 "--dump=none", "--eval=" + PROBE, url], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, env=isolated_env(home, a.locale), start_new_session=True)
        try:
            out, _ = proc.communicate(timeout=a.timeout)
        except subprocess.TimeoutExpired:
            kill_group(proc)
            proc.communicate()
            return "", "timed out after %ds" % a.timeout
        text = out.decode("utf-8", "replace")
        text = text[6:] if text.startswith("eval: ") else text
        if proc.returncode != 0:
            return text, "exit status %d" % proc.returncode
        return text, None if "\t" in text else "no elements printed"
    finally:
        shutil.rmtree(home, ignore_errors=True)


def snapshot_names(a):
    root = Path(a.snapshots)
    names = sorted(p.name for p in root.iterdir()
                   if (p / "index.html").exists() and not p.name.endswith(".part")) if root.is_dir() else []
    return [n for n in names if not a.only or n in a.only]


def file_digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()[:12]


def reference_key(a, site_dir):
    h = hashlib.sha256(("%s\0%s\0%d\0%s" % (PROBE_SCRIPT, PROBE, a.settle_ms, chrome_args(a))).encode())
    h.update((site_dir / "index.html").read_bytes())
    img = site_dir / "img"
    for p in sorted(img.iterdir()) if img.is_dir() else ():
        h.update(("\0%s\0%d" % (p.name, p.stat().st_size)).encode())
    return h.hexdigest()[:16]


def ensure_reference(a, chrome, version, names, base_url):
    ref = Path(a.out) / "snapshot-chrome" / ("%s-%dx%d-%s" % (version or "unknown", a.vw, a.vh, a.locale))
    keys = {n: reference_key(a, Path(a.snapshots) / n) for n in names}
    todo = [n for n in names if a.refresh or read_line(ref / n / "key") != keys[n]]
    errors = {}

    def one(name):
        text, err = chrome_rects(chrome, base_url, name, a)
        (ref / name).mkdir(parents=True, exist_ok=True)
        (ref / name / "rects.tsv").write_text(text + "\n" if text else "", encoding="utf-8", errors="replace")
        (ref / name / "key").write_text("" if err else keys[name] + "\n")
        return name, text.count("\n") + 1 if text else 0, err

    if todo:
        print("chrome %s: measuring %d snapshots" % (version, len(todo)), flush=True)
        with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
            for name, n, err in ex.map(one, todo):
                print("  %-22s %6d elements%s" % (name, n, "  " + err if err else ""), flush=True)
                if err:
                    errors[name] = "Chrome: " + err
    return ref, errors


def render(a, binary, label, run, names, base_url):
    dest = Path(a.out) / "snapshot-ns" / label / str(run)
    shutil.rmtree(dest, ignore_errors=True)

    def one(name):
        t0 = time.time()
        text, err = ns_rects(binary, base_url + urllib.parse.quote(name) + "/index.html", a)
        (dest / name).mkdir(parents=True, exist_ok=True)
        (dest / name / "rects.tsv").write_text(text if text.endswith("\n") or not text else text + "\n",
                                               encoding="utf-8", errors="replace")
        return name, text.count("\n"), err, time.time() - t0

    print("nordstjernen %s, run %d: rendering %d snapshots" % (label, run, len(names)), flush=True)
    errors = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for name, n, err, secs in ex.map(one, names):
            print("  %-22s %6d elements %5.1fs%s" % (name, n, secs, "  " + err if err else ""), flush=True)
            if err:
                errors[name] = err
    return dest, errors


def load_rects(path):
    rects = {}
    if not path.exists():
        return rects
    for line in open(path, encoding="utf-8", errors="replace"):
        parts = line.rstrip("\n").split("\t")
        if len(parts) < 3:
            continue
        try:
            box = tuple(int(float(v)) for v in parts[2].split(","))
        except (ValueError, OverflowError):
            continue
        if len(box) == 4:
            rects[parts[0]] = (parts[1], box)
    return rects


def parent(path):
    return path.rsplit("/", 1)[0] if "/" in path else None


def matching(ch, ns):
    same = set()
    for path, (_, c) in ch.items():
        n = ns.get(path)
        if not n or any(abs(n[1][i] - c[i]) > TOL for i in (2, 3)):
            continue
        n = n[1]
        par = parent(path)
        pc, pn = ch.get(par), ns.get(par)
        if not pc or not pn:
            if all(abs(c[i] - n[i]) <= TOL for i in (0, 1)):
                same.add(path)
        elif all(abs((c[i] - pc[1][i]) - (n[i] - pn[1][i])) <= TOL for i in (0, 1)):
            same.add(path)
    return same


def boxes_changed(a, b):
    return sum(1 for k in a if b.get(k, (None, None))[1] != a[k][1]) + sum(1 for k in b if k not in a)


class Sources:
    def __init__(self, ch, ns):
        self.ch, self.ns = ch, ns
        self.kids = collections.defaultdict(list)
        for p in ch:
            if parent(p) is not None:
                self.kids[parent(p)].append(p)
        for v in self.kids.values():
            v.sort(key=self.index)
        self.fail = {p: f for p, f in ((p, self.how(p)) for p in ch) if f}
        self.src = {}

    @staticmethod
    def index(path):
        try:
            return int(path.rsplit("/", 1)[-1])
        except ValueError:
            return -1

    def how(self, p):
        if p not in self.ns:
            return "missing"
        c, n = self.ch[p][1], self.ns[p][1]
        if abs(c[2] - n[2]) > TOL:
            return "width"
        if abs(c[3] - n[3]) > TOL:
            return "height"
        par = parent(p)
        pc, pn = self.ch.get(par), self.ns.get(par)
        if not pc or not pn:
            off = any(abs(c[i] - n[i]) > TOL for i in (0, 1))
        else:
            off = any(abs((c[i] - pc[1][i]) - (n[i] - pn[1][i])) > TOL for i in (0, 1))
        return "offset" if off else None

    def height_gap(self, p):
        return abs(self.ch[p][1][3] - self.ns[p][1][3]) if p in self.ns else 0

    def follows(self, p):
        f, par = self.fail[p], parent(p)
        if f == "missing":
            return par if self.fail.get(par) == "missing" else None
        if f == "width":
            return par if self.fail.get(par) == "width" else None
        if f == "height":
            kids = [k for k in self.kids.get(p, ()) if self.fail.get(k) in ("width", "height")]
            return max(kids, key=self.height_gap) if kids else None
        earlier = [k for k in self.kids.get(par, ()) if self.index(k) < self.index(p) and k in self.fail]
        if earlier:
            return earlier[-1]
        return par if self.fail.get(par) == "width" else None

    def source(self, p):
        seen = []
        while p not in self.src:
            seen.append(p)
            q = self.follows(p)
            if q is None or q in seen:
                self.src[p] = p
                break
            p = q
        s = self.src[p]
        for x in seen:
            self.src[x] = s
        return s

    def weights(self):
        return collections.Counter(self.source(p) for p in self.fail)


def levers(ref, ns_dir, names, limit):
    groups = collections.defaultdict(list)
    for name in names:
        ch, ns = load_rects(ref / name / "rects.tsv"), load_rects(ns_dir / name / "rects.tsv")
        if len(ch) < MIN_ELEMENTS or not ns:
            continue
        st = Sources(ch, ns)
        for p, w in st.weights().items():
            groups[(name, st.fail[p], re.sub(r"\d+", "#", ch[p][0]))].append((w, p))
    rows = []
    for (name, kind, label), g in sorted(groups.items(), key=lambda x: -sum(w for w, _ in x[1]))[:limit]:
        w, p = max(g)
        rows.append({"site": name, "kind": kind, "element": label, "weight": sum(w for w, _ in g),
                     "sources": len(g), "example": p})
    return rows


def meta_line(m):
    return ("Chrome %s · viewport %s · Nordstjernen locale %s · %d sites · %s Chrome elements"
            % (m["chrome"], m["viewport"], m["locale"], len(m["sites"]), format(m["elements"], ",")))


METRIC = ("An element matches when its width, its height and its offset in its parent are all within 2 px of "
          "Chrome's, for the element at the same place in the DOM.")


def levers_md(rows, label):
    if not rows:
        return []
    out = ["", "## Where %s fails most" % label, "",
           "Every failing element is traced to the failure it most likely follows from (widths flow down, heights "
           "up, an offset follows the earlier sibling that failed); weight is the number of failing elements traced "
           "to the sources of one kind and element on one site, an estimate of what fixing them wins.", "",
           "| Weight | Site | Kind | Element | Sources | Largest source |", "| ---: | --- | --- | --- | ---: | --- |"]
    for r in rows:
        out.append("| %d | %s | %s | `%s` | %d | `%s` |" % (r["weight"], r["site"], r["kind"], r["element"][:60],
                                                             r["sources"], r["example"]))
    return out


def failure_lines(label, failed):
    if not failed:
        return []
    return ["", "FAIL: %s crashed, timed out or printed no elements on %s" % (
        label, "; ".join("%s (%s)" % kv for kv in sorted(failed.items())))]


def write_report(a, data, lines):
    report = Path(a.report or Path(a.out) / "report")
    report.mkdir(parents=True, exist_ok=True)
    (report / "snapshots.json").write_text(json.dumps(data, indent=1) + "\n")
    (report / "snapshots.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    print("\nreport: %s" % (report / "snapshots.md"))


def common_meta(a, version, names, elements):
    return {"chrome": version, "viewport": "%dx%d" % (a.vw, a.vh), "locale": a.locale, "sites": names,
            "elements": elements, "generated": datetime.now(timezone.utc).isoformat(timespec="seconds")}


def cmd_freeze(a):
    if not os.path.isfile(a.sites):
        fail("no site list at %s" % a.sites)
    sites = read_sites(a.sites, a.only, a.category)
    if not sites:
        fail("no sites selected")
    version = chrome_version(a.chrome)
    if not version:
        fail("cannot run Chrome at %s (set --chrome or CHROME_BIN)" % a.chrome)
    Path(a.snapshots).mkdir(parents=True, exist_ok=True)
    print("freezing %d sites with Chrome %s into %s" % (len(sites), version, a.snapshots), flush=True)
    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for name, result in ex.map(lambda s: freeze_site(s, a, a.chrome, version), sites):
            print("  %-22s %s" % (name, result), flush=True)
            failed += result.startswith("failed")
    return 1 if failed == len(sites) else 0


def prepare(a):
    names = snapshot_names(a)
    if not names:
        fail("no snapshots under %s (run: snapshot.py freeze)" % a.snapshots)
    version = chrome_version(a.chrome)
    if not version:
        fail("cannot run Chrome at %s (set --chrome or CHROME_BIN)" % a.chrome)
    return names, version


def cmd_score(a):
    names, version = prepare(a)
    httpd, base_url = serve(a.snapshots, a.settle_ms)
    try:
        ref, skipped = ensure_reference(a, a.chrome, version, names, base_url)
        ns_dir, errors = render(a, a.bin, a.label, 1, names, base_url)
    finally:
        httpd.shutdown()
    rows, failed = [], {}
    for name in names:
        ch = load_rects(ref / name / "rects.tsv")
        if len(ch) < MIN_ELEMENTS:
            skipped.setdefault(name, "Chrome found %d elements" % len(ch))
            continue
        ns = load_rects(ns_dir / name / "rects.tsv")
        if name in errors or not ns:
            failed[name] = errors.get(name) or "no elements"
        m = len(matching(ch, ns))
        rows.append({"site": name, "chrome": len(ch), "matching": m, "share": round(100.0 * m / len(ch), 2)})
    if not rows:
        fail("nothing to score (%s)" % "; ".join("%s: %s" % kv for kv in skipped.items()))
    total, same = sum(r["chrome"] for r in rows), sum(r["matching"] for r in rows)
    share = round(100.0 * same / total, 2)
    meta = dict(common_meta(a, version, [r["site"] for r in rows], total), build=a.label,
                binary={"path": a.bin, "sha256": file_digest(a.bin)})
    lever_rows = levers(ref, ns_dir, [r["site"] for r in rows], a.levers)
    lines = ["# Frozen snapshots: %s vs Chrome" % a.label, "", meta_line(meta), "", METRIC, "",
             "| Site | Chrome elements | Matching | Share |", "| --- | ---: | ---: | ---: |"]
    for r in rows:
        lines.append("| %s | %s | %s | %.2f%% |" % (r["site"], format(r["chrome"], ","), format(r["matching"], ","),
                                                   r["share"]))
    lines.append("| **All** | %s | %s | **%.2f%%** |" % (format(total, ","), format(same, ","), share))
    lines += levers_md(lever_rows, a.label)
    if skipped:
        lines += ["", "Skipped: " + "; ".join("%s (%s)" % kv for kv in sorted(skipped.items()))]
    lines += failure_lines(a.label, failed)
    below = a.min_share is not None and share < a.min_share
    if below:
        lines += ["", "FAIL: %.2f%% of elements match, below --min-share=%.2f" % (share, a.min_share)]
    write_report(a, {"meta": meta, "sites": rows, "matching": same, "share": share, "levers": lever_rows,
                     "skipped": skipped, "failed": failed}, lines)
    return 1 if below or failed else 0


def ab_row(name, ref, dirs, errs, before_label, skipped, failed):
    co = load_rects(ref / name / "rects.tsv")
    if len(co) < MIN_ELEMENTS:
        skipped.setdefault(name, "Chrome found %d elements" % len(co))
        return None
    r1, r2, rn = (load_rects(d / name / "rects.tsv") for d in dirs)
    before_err = errs[0].get(name) or errs[1].get(name) or (None if r1 and r2 else "no elements")
    if before_err:
        skipped[name] = "%s: %s" % (before_label, before_err)
        return None
    if name in errs[2] or not rn:
        failed[name] = errs[2].get(name) or "no elements"
    p1, p2, pn = matching(co, r1), matching(co, r2), matching(co, rn)
    before, noise = (len(p1) + len(p2)) / 2.0, abs(len(p1) - len(p2))
    delta = len(pn) - before
    lost = sorted((p1 & p2) - pn, key=Sources.index)
    return {"site": name, "chrome": len(co), "before": before, "after": len(pn), "delta": delta, "noise": noise,
            "won": len(pn - (p1 | p2)), "lost": len(lost), "boxesMoved": boxes_changed(r1, rn),
            "boxNoise": boxes_changed(r1, r2), "significant": abs(delta) > noise / 2.0 + 2,
            "lostExamples": [[p, co[p][0], co[p][1], rn.get(p, (None, None))[1]] for p in sorted(lost)[:20]]}


def cmd_ab(a):
    names, version = prepare(a)
    before_label, after_label = a.labels
    httpd, base_url = serve(a.snapshots, a.settle_ms)
    try:
        ref, skipped = ensure_reference(a, a.chrome, version, names, base_url)
        b1, e1 = render(a, a.before, before_label, 1, names, base_url)
        b2, e2 = render(a, a.before, before_label, 2, names, base_url)
        nd, e3 = render(a, a.after, after_label, 1, names, base_url)
    finally:
        httpd.shutdown()
    rows, failed = [], {}
    for name in names:
        row = ab_row(name, ref, (b1, b2, nd), (e1, e2, e3), before_label, skipped, failed)
        if row:
            rows.append(row)
    if not rows:
        fail("nothing to compare (%s)" % "; ".join("%s: %s" % kv for kv in skipped.items()))
    elements, noise = sum(r["chrome"] for r in rows), sum(r["noise"] for r in rows)
    before, after = sum(r["before"] for r in rows), sum(r["after"] for r in rows)
    pct = lambda v: round(100.0 * v / elements, 2)
    delta = round(after - before, 1)
    up = [r for r in rows if r["significant"] and r["delta"] > 0]
    down = [r for r in rows if r["significant"] and r["delta"] < 0]
    worse = delta < -(noise / 2.0 + 2)
    over = [r for r in rows if a.max_site_loss is not None and r["lost"] > r["noise"] / 2.0 + 2 + a.max_site_loss]
    verdict = "worse" if worse else "better" if delta > noise / 2.0 + 2 else "unchanged"
    meta = dict(common_meta(a, version, [r["site"] for r in rows], elements),
                builds={before_label: {"path": a.before, "sha256": file_digest(a.before)},
                        after_label: {"path": a.after, "sha256": file_digest(a.after)}})
    lever_rows = levers(ref, nd, [r["site"] for r in rows], a.levers)
    signed = lambda v: format(v, "+,.1f").replace(".0", "")
    lines = ["# Frozen snapshots: %s vs %s" % (before_label, after_label), "", meta_line(meta), "",
             METRIC + " Noise is the difference between two renders of %s; a change counts when it is larger than "
             "half the noise plus 2 elements." % before_label, "",
             "| Site | Chrome elements | %s | %s | Change | Noise | Won | Lost | Boxes moved |" % (before_label,
                                                                                              after_label),
             "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for r in rows:
        mark = "**" if r["significant"] else ""
        lines.append("| %s | %s | %s | %s | %s%s%s | %d | %s | %s | %s |" % (
            r["site"], format(r["chrome"], ","), format(r["before"], ",g"), format(r["after"], ","), mark,
            signed(r["delta"]), mark, r["noise"], format(r["won"], ","), format(r["lost"], ","),
            format(r["boxesMoved"], ",")))
    lines.append("| **All** | %s | %.2f%% | %.2f%% | **%s** | %d | %s | %s | |" % (
        format(elements, ","), pct(before), pct(after), signed(delta), noise,
        format(sum(r["won"] for r in rows), ","), format(sum(r["lost"] for r in rows), ",")))
    lines += ["", "Result: **%s**, %s elements (noise %d); up: %s; down: %s." % (
        verdict, signed(delta), noise, ", ".join("%s %s" % (r["site"], signed(r["delta"])) for r in up) or "none",
        ", ".join("%s %s" % (r["site"], signed(r["delta"])) for r in down) or "none")]
    for r in [r for r in rows if r in down or r in over]:
        lines += ["", "### Lost on %s" % r["site"], "", "| Element | Path | Chrome | %s |" % after_label,
                  "| --- | --- | --- | --- |"]
        for p, label, c, n in r["lostExamples"][:10]:
            lines.append("| `%s` | `%s` | %s | %s |" % (label[:50], p, ",".join(map(str, c)),
                                                       ",".join(map(str, n)) if n else "-"))
    lines += levers_md(lever_rows, after_label)
    if skipped:
        lines += ["", "Skipped: " + "; ".join("%s (%s)" % kv for kv in sorted(skipped.items()))]
    lines += failure_lines(after_label, failed)
    if worse:
        lines += ["", "FAIL: %s matches %s elements fewer than %s, beyond the noise" % (after_label, signed(-delta)[1:],
                                                                                         before_label)]
    for r in over:
        lines += ["", "FAIL: %s loses %s of the elements %s matched on %s, more than --max-site-loss=%d beyond "
                  "its noise" % (after_label, format(r["lost"], ","), before_label, r["site"], a.max_site_loss)]
    write_report(a, {"meta": meta, "sites": rows, "before": pct(before), "after": pct(after), "delta": delta,
                     "noise": noise, "verdict": verdict, "levers": lever_rows, "skipped": skipped,
                     "failed": failed}, lines)
    return 1 if worse or over or failed else 0


def ns_load(binary, url, a):
    home = tempfile.mkdtemp(prefix="sitebench-ns-")
    try:
        proc = subprocess.Popen([binary, "--headless", "--viewport=%dx%d" % (a.vw, a.vh), "--settle-ms=0", "--timing",
                                 "--dump=none", url], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                env=isolated_env(home, a.locale), start_new_session=True)
        expired = []
        killer = threading.Timer(a.timeout, lambda: expired.append(kill_group(proc)))
        killer.start()
        try:
            out = proc.stdout.read().decode("utf-8", "replace")
            _, status, usage = os.wait4(proc.pid, 0)
        finally:
            killer.cancel()
            proc.stdout.close()
        proc.returncode = os.waitstatus_to_exitcode(status)
        m = re.search(r"^timing: (\{.*\})$", out, re.M)
        if expired:
            return None, "timed out after %ds" % a.timeout
        if proc.returncode != 0 or not m:
            return None, "exit status %d" % proc.returncode if proc.returncode else "no timing line"
        t = json.loads(m.group(1))
        rss = usage.ru_maxrss / (1 << 20) if sys.platform == "darwin" else usage.ru_maxrss / 1024.0
        return {"cpu": t["cpu_ms"], "paint": t["first_paint_ms"], "style": t["style_ms"], "layout": t["relayout_ms"],
                "process": round((usage.ru_utime + usage.ru_stime) * 1000, 1), "rss": round(rss, 1)}, None
    finally:
        shutil.rmtree(home, ignore_errors=True)


CHROME_SETTLED = (
    "Promise.all([document.fonts ? document.fonts.ready : null, new Promise(done => {"
    " setTimeout(() => done(null), 3000);"
    " new PerformanceObserver(l => { for (const e of l.getEntries())"
    "  if (e.name === 'first-contentful-paint') done(e.startTime); }).observe({type: 'paint', buffered: true}); })])"
    ".then(([, fcp]) => new Promise(done => requestAnimationFrame(() => requestAnimationFrame(() => {"
    " const n = performance.getEntriesByType('navigation')[0];"
    " done(JSON.stringify({fcp: fcp, load: n ? n.loadEventEnd : null})); }))))")


class ChromeLoads:
    def __init__(self, chrome, a):
        self.chrome, self.a, self.cdp = chrome, a, None

    def close(self):
        if self.cdp:
            self.cdp.close()
            self.cdp = None

    def load(self, url):
        def timing(cdp, s, times):
            metrics = {m["name"]: m["value"] for m in cdp.call("Performance.getMetrics", session=s)["metrics"]}
            ms = lambda key: round(metrics.get(key, 0) * 1000, 1)
            return {"cpu": ms("TaskDuration"), "style": ms("RecalcStyleDuration"), "layout": ms("LayoutDuration"),
                    "paint": None if times.get("fcp") is None else round(times["fcp"], 1),
                    "load": None if times.get("load") is None else round(times["load"], 1)}
        return self.visit(url, timing)

    def visit(self, url, measure):
        a = self.a
        if not self.cdp:
            self.cdp = Cdp(self.chrome, [*chrome_args(a), "--proxy-server=" + DEAD_PROXY,
                                         "--window-size=%d,%d" % (a.vw + WINDOW_PAD[0], a.vh + WINDOW_PAD[1])],
                           locale_env(a.locale))
        cdp, ctx = self.cdp, None
        try:
            ctx = cdp.call("Target.createBrowserContext")["browserContextId"]
            target = cdp.call("Target.createTarget", {"url": "about:blank", "browserContextId": ctx})["targetId"]
            s = cdp.call("Target.attachToTarget", {"targetId": target, "flatten": True})["sessionId"]
            for method, params in (("Page.enable", {}), ("Page.bringToFront", {}),
                                   ("Performance.enable", {"timeDomain": "threadTicks"}),
                                   ("Emulation.setDeviceMetricsOverride", {"width": a.vw, "height": a.vh,
                                                                           "deviceScaleFactor": 1, "mobile": False}),
                                   ("Emulation.setTimezoneOverride", {"timezoneId": "UTC"})):
                cdp.call(method, params, session=s)
            start = len(cdp.events)
            nav = cdp.call("Page.navigate", {"url": url}, session=s, timeout=60)
            if nav.get("errorText"):
                return None, nav["errorText"]
            if cdp.wait(lambda m: m.get("method") == "Page.loadEventFired" and m.get("sessionId") == s, 60,
                        start) is None:
                return None, "no load event in 60 s"
            done = cdp.call("Runtime.evaluate", {"expression": CHROME_SETTLED, "awaitPromise": True,
                                                 "returnByValue": True}, session=s, timeout=60)
            return measure(cdp, s, json.loads(done["result"]["value"])), None
        except (TimeoutError, EOFError, OSError, RuntimeError, KeyError, ValueError, TypeError) as e:
            self.close()
            ctx = None
            return None, "DevTools protocol: %s" % (str(e) or type(e).__name__)
        finally:
            if ctx:
                try:
                    cdp.call("Target.disposeBrowserContext", {"browserContextId": ctx})
                except (TimeoutError, EOFError, OSError, RuntimeError):
                    self.close()
            if self.cdp:
                del self.cdp.events[:]


def median(values):
    values = sorted(v for v in values if v is not None)
    if not values:
        return None
    mid = len(values) // 2
    return values[mid] if len(values) % 2 else round((values[mid - 1] + values[mid]) / 2.0, 1)


def summarize(runs):
    keys = sorted({k for r in runs for k in r})
    return {k: median(r.get(k) for r in runs) for k in keys}


def geomean(values):
    values = [v for v in values if v and v > 0]
    return math.exp(sum(math.log(v) for v in values) / len(values)) if values else None


def perf_site(a, name, url, chrome, binaries, labels):
    loads = {"chrome": []}
    loads.update((label, []) for label in labels)
    errors = {}
    builds = list(zip(binaries, labels))
    for run in range(a.runs):
        got, err = chrome.load(url)
        loads["chrome"].append(got) if got else errors.setdefault("chrome", err)
        k = run % len(builds)
        for binary, label in builds[k:] + builds[:k]:
            got, err = ns_load(binary, url, a)
            loads[label].append(got) if got else errors.setdefault(label, err)
    row = {"site": name, "errors": errors}
    for key, runs in loads.items():
        row[key] = dict(summarize(runs), cpuRuns=[r["cpu"] for r in runs]) if runs else None
    return row


def ratio(x, y):
    return x / y if x is not None and y else None


def fmt(v, spec=",.0f"):
    return "-" if v is None else format(v, spec)


def faster_in_every_run(old, new):
    if not old or not new or not old["cpuRuns"] or not new["cpuRuns"]:
        return None
    if max(new["cpuRuns"]) < min(old["cpuRuns"]):
        return -1
    if min(new["cpuRuns"]) > max(old["cpuRuns"]):
        return 1
    return 0


PERF_METRIC = (
    "Main-thread CPU is Nordstjernen's main-thread CPU time from start to its first painted frame (`--settle-ms=0 "
    "--timing`), and Chrome's main-thread task time in thread ticks (`Performance.getMetrics`) from navigation until "
    "the load event, ready fonts and two animation frames, on the same frozen page served locally, each load in a "
    "fresh profile. Medians over %d runs per site and browser, interleaved. Nordstjernen's first paint is wall time "
    "to its first painted frame, Chrome's its first contentful paint.")


def cmd_perf(a):
    names, version = prepare(a)
    labels = a.labels
    httpd, base_url = serve(a.snapshots, a.settle_ms)
    chrome = ChromeLoads(a.chrome, a)
    rows = []
    try:
        warm = base_url + urllib.parse.quote(names[0]) + "/index.html"
        chrome.load(warm)
        for binary in a.bins:
            ns_load(binary, warm, a)
        print("timing %d snapshots, %d runs each: Chrome %s, %s" % (len(names), a.runs, version, ", ".join(labels)),
              flush=True)
        for name in names:
            row = perf_site(a, name, base_url + urllib.parse.quote(name) + "/index.html", chrome, a.bins, labels)
            rows.append(row)
            cells = ["%s %s" % (k, fmt((row[k] or {}).get("cpu"))) for k in ["chrome"] + labels]
            err = "; ".join("%s: %s" % kv for kv in row["errors"].items())
            print("  %-22s cpu ms: %s%s" % (name, ", ".join(cells), "  " + err if err else ""), flush=True)
    finally:
        chrome.close()
        httpd.shutdown()
    ok = [r for r in rows if r["chrome"] and all(r[label] for label in labels)]
    if not ok:
        fail("nothing timed (%s)" % "; ".join("%s: %s" % (r["site"], r["errors"]) for r in rows))
    newest, oldest = labels[-1], labels[0]
    totals = {k: sum(r[k]["cpu"] for r in ok) for k in ["chrome"] + labels}
    summary = {"sites": len(ok), "cpuMs": totals,
               "cpuRatio": {k: geomean(ratio(r[k]["cpu"], r["chrome"]["cpu"]) for r in ok) for k in labels},
               "paintRatio": {k: geomean(ratio(r[k]["paint"], r["chrome"]["paint"]) for r in ok) for k in labels},
               "rssMb": {k: median(r[k]["rss"] for r in ok) for k in labels}}
    meta = dict(common_meta(a, version, [r["site"] for r in ok], 0), runs=a.runs,
                builds={label: {"path": b, "sha256": file_digest(b)} for b, label in zip(a.bins, labels)})
    del meta["elements"]
    lines = ["# Frozen snapshots: load cost, %s vs Chrome" % " vs ".join(labels), "",
             "Chrome %s · viewport %s · Nordstjernen locale %s · %d sites" % (version, meta["viewport"], a.locale,
                                                                              len(ok)), "", PERF_METRIC % a.runs, ""]
    if len(labels) == 1:
        lines += ["| Site | Chrome CPU ms | %s CPU ms | ÷ Chrome | Chrome FCP ms | %s first paint ms | Style ms | "
                  "Layout ms | Peak RSS MB |" % (newest, newest),
                  "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
        for r in ok:
            c, n = r["chrome"], r[newest]
            lines.append("| %s | %s | %s | %s× | %s | %s | %s | %s | %s |" % (
                r["site"], fmt(c["cpu"]), fmt(n["cpu"]), fmt(ratio(n["cpu"], c["cpu"]), ".2f"), fmt(c["paint"]),
                fmt(n["paint"]), fmt(n["style"]), fmt(n["layout"]), fmt(n["rss"])))
    else:
        moved = {r["site"]: faster_in_every_run(r[oldest], r[newest]) for r in ok}
        lines += ["| Site | Chrome CPU ms | %s | Change | %s ÷ Chrome | %s |" % (
                      " | ".join("%s CPU ms" % k for k in labels), newest,
                      " | ".join("%s first paint ms" % k for k in labels)),
                  "| --- | ---: | %s | ---: | ---: | %s |" % (" | ".join(["---:"] * len(labels)),
                                                             " | ".join(["---:"] * len(labels)))]
        for r in ok:
            change = 100.0 * (r[newest]["cpu"] - r[oldest]["cpu"]) / r[oldest]["cpu"] if r[oldest]["cpu"] else None
            mark = "**" if moved[r["site"]] else ""
            lines.append("| %s | %s | %s | %s%s%s | %s× | %s |" % (
                r["site"], fmt(r["chrome"]["cpu"]), " | ".join(fmt(r[k]["cpu"]) for k in labels), mark,
                fmt(change, "+.1f") + "%" if change is not None else "-", mark,
                fmt(ratio(r[newest]["cpu"], r["chrome"]["cpu"]), ".2f"), " | ".join(fmt(r[k]["paint"]) for k in labels)))
        faster = sorted(s for s, v in moved.items() if v == -1)
        slower = sorted(s for s, v in moved.items() if v == 1)
        summary.update(faster=faster, slower=slower)
        lines += ["", "In bold: %s was faster (or slower) than %s in every run. Faster on %d sites: %s. Slower on %d "
                  "sites: %s." % (newest, oldest, len(faster), ", ".join(faster) or "none", len(slower),
                                  ", ".join(slower) or "none")]
    lines += ["", "| | Chrome | %s |" % " | ".join(labels), "| --- | ---: | %s |" % " | ".join(["---:"] * len(labels)),
              "| Main-thread CPU, sum over sites (ms) | %s | %s |" % (
                  fmt(totals["chrome"]), " | ".join(fmt(totals[k]) for k in labels)),
              "| Main-thread CPU ÷ Chrome (geomean) | 1.00× | %s |" % " | ".join(
                  fmt(summary["cpuRatio"][k], ".2f") + "×" for k in labels),
              "| First paint ÷ Chrome FCP (geomean) | 1.00× | %s |" % " | ".join(
                  fmt(summary["paintRatio"][k], ".2f") + "×" for k in labels),
              "| Peak RSS, median over sites (MB) | | %s |" % " | ".join(fmt(summary["rssMb"][k]) for k in labels)]
    failed = {r["site"]: r["errors"] for r in rows if r["errors"]}
    if failed:
        lines += ["", "Failed loads: " + "; ".join("%s (%s)" % (s, "; ".join("%s: %s" % kv for kv in e.items()))
                                                   for s, e in sorted(failed.items()))]
    slow = None
    if a.max_slowdown is not None and len(labels) > 1 and totals[oldest]:
        slow = 100.0 * (totals[newest] - totals[oldest]) / totals[oldest]
        if slow > a.max_slowdown:
            lines += ["", "FAIL: %s spends %.1f%% more main-thread CPU than %s over the %d sites, above "
                      "--max-slowdown=%g" % (newest, slow, oldest, len(ok), a.max_slowdown)]
    report = Path(a.report or Path(a.out) / "report")
    report.mkdir(parents=True, exist_ok=True)
    (report / "perf.json").write_text(json.dumps({"meta": meta, "summary": summary, "sites": rows}, indent=1) + "\n")
    (report / "perf.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    print("\nreport: %s" % (report / "perf.md"))
    return 1 if slow is not None and slow > a.max_slowdown else 0


PAINT_PROBE = r"""(function(){
  var out = [], replaced = /^(img|svg|video|canvas|input|textarea|select|button|iframe|object|embed|hr)$/;
  var rootClips = getComputedStyle(document.documentElement).overflow !== 'visible';
  function paints(e, s) {
    if (replaced.test(e.tagName.toLowerCase()) || s.backgroundImage !== 'none' || s.boxShadow !== 'none') return true;
    if (!/^(transparent|rgba\(.*,\s*0\))$/.test(s.backgroundColor)) return true;
    for (var side of ['Top', 'Right', 'Bottom', 'Left'])
      if (s['border' + side + 'Style'] !== 'none' && parseFloat(s['border' + side + 'Width']) > 0) return true;
    for (var c = e.firstChild; c; c = c.nextSibling) if (c.nodeType === 3 && c.data.trim()) return true;
    for (var pseudo of ['::before', '::after']) {
      var ps = getComputedStyle(e, pseudo);
      if (ps.content !== 'none' && ps.content !== 'normal' && ps.display !== 'none') return true;
    }
    return false;
  }
  function covered(e, r) {
    var x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2;
    if (x < 0 || y < 0 || x >= innerWidth || y >= innerHeight) return false;
    var hit = document.elementFromPoint(x, y);
    return !!hit && hit !== e && !e.contains(hit) && !hit.contains(e);
  }
  function walk(e, path, opacity, clip) {
    var r = e.getBoundingClientRect(), s = getComputedStyle(e), o = parseFloat(s.opacity);
    var box = [r.left, r.top + scrollY, r.right, r.bottom + scrollY];
    opacity *= isNaN(o) ? 1 : o;
    if (s.position === 'fixed') clip = null;
    var inside = !clip || (Math.min(box[2], clip[2]) - Math.max(box[0], clip[0]) > 0 &&
                           Math.min(box[3], clip[3]) - Math.max(box[1], clip[1]) > 0);
    var shown = s.visibility === 'visible' && opacity > 0.01 && inside && paints(e, s) && !covered(e, r);
    var cls = (typeof e.className === 'string' && e.className) ? '.' + e.className.trim().split(/\s+/).slice(0, 2).join('.') : '';
    out.push(path + '\t' + e.tagName.toLowerCase() + (e.id ? '#' + e.id : '') + cls + '\t' + [Math.round(r.left), Math.round(r.top + scrollY), Math.round(r.width), Math.round(r.height)].join(',') + '\t' + (shown ? 1 : 0));
    if (e !== document.body || rootClips) {
      var cx = s.overflowX !== 'visible', cy = s.overflowY !== 'visible';
      if (cx || cy) {
        var c0 = clip || [-1e9, -1e9, 1e9, 1e9];
        clip = [cx ? Math.max(c0[0], box[0]) : c0[0], cy ? Math.max(c0[1], box[1]) : c0[1],
                cx ? Math.min(c0[2], box[2]) : c0[2], cy ? Math.min(c0[3], box[3]) : c0[3]];
      }
    }
    var k = 0;
    for (var c = e.firstElementChild; c; c = c.nextElementSibling) walk(c, path + '/' + (k++), opacity, clip);
  }
  walk(document.body, 'b', 1, null);
  return out.join('\n');
})()"""
PAGE_HEIGHT = "Math.max(document.documentElement.scrollHeight, document.body ? document.body.scrollHeight : 0)"
PAINT_CELL = 4
PAINT_LIMIT = 48
PAINT_SHARE = 0.1
PAINT_SHOWN = 20
PAINT_CAPTURES = 2


def paint_key(a, site_dir):
    return hashlib.sha256(("%s\0%s\0%d\0%d" % (reference_key(a, site_dir), PAINT_PROBE, a.max_height,
                                                PAINT_CAPTURES)).encode()).hexdigest()[:16]


def chrome_paint(chrome, url, a, dest):
    def capture(cdp, s, times):
        time.sleep(a.settle_ms / 1000.0)
        rects = cdp.call("Runtime.evaluate", {"expression": PAINT_PROBE, "returnByValue": True}, session=s,
                         timeout=60)["result"]["value"]
        height = cdp.call("Runtime.evaluate", {"expression": PAGE_HEIGHT, "returnByValue": True},
                          session=s)["result"]["value"]
        clip = {"x": 0, "y": 0, "width": a.vw, "height": min(max(int(height), a.vh), a.max_height), "scale": 1}
        for k in range(PAINT_CAPTURES):
            if k:
                time.sleep(1)
            shot = cdp.call("Page.captureScreenshot", {"format": "png", "captureBeyondViewport": True, "clip": clip},
                            session=s, timeout=120)
        dest.mkdir(parents=True, exist_ok=True)
        (dest / "paint.png").write_bytes(base64.b64decode(shot["data"]))
        (dest / "paint.tsv").write_text(rects + "\n", encoding="utf-8", errors="replace")
        return rects.count("\n") + 1
    return chrome.visit(url, capture)


def ns_paint(binary, url, a, dest):
    from PIL import Image
    dest.mkdir(parents=True, exist_ok=True)
    png = dest / "paint.png"
    for stale in (png, dest / "paint-initial.png", dest / "paint.tsv"):
        stale.unlink(missing_ok=True)
    home = tempfile.mkdtemp(prefix="sitebench-ns-")
    try:
        proc = subprocess.Popen([binary, "--headless", "--viewport=%dx%d" % (a.vw, a.vh), "--settle-ms=%d" % a.settle_ms,
                                 "--time-ms=0", "--dump=png:" + str(png), "--eval=" + PAINT_PROBE, url],
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=isolated_env(home, a.locale),
                                start_new_session=True)
        try:
            out, _ = proc.communicate(timeout=a.timeout)
        except subprocess.TimeoutExpired:
            kill_group(proc)
            proc.communicate()
            return 0, "timed out after %ds" % a.timeout
    finally:
        shutil.rmtree(home, ignore_errors=True)
    (dest / "paint-initial.png").unlink(missing_ok=True)
    text = out.decode("utf-8", "replace")
    text = text[text.find("eval: ") + 6:] if "eval: " in text else ""
    (dest / "paint.tsv").write_text(text if text.endswith("\n") or not text else text + "\n", encoding="utf-8",
                                    errors="replace")
    if not png.exists():
        return 0, "exit status %d, no screenshot" % proc.returncode
    with Image.open(png) as im:
        if im.height > a.max_height:
            im.crop((0, 0, im.width, a.max_height)).save(png)
    return text.count("\n"), None if "\t" in text else "no elements printed"


def load_paint(path):
    rects, visible = {}, set()
    if not path.exists():
        return rects, visible
    for line in open(path, encoding="utf-8", errors="replace"):
        parts = line.rstrip("\n").split("\t")
        if len(parts) < 4:
            continue
        try:
            box = tuple(int(float(v)) for v in parts[2].split(","))
        except (ValueError, OverflowError):
            continue
        if len(box) == 4:
            rects[parts[0]] = (parts[1], box)
            if parts[3] == "1":
                visible.add(parts[0])
    return rects, visible


def paint_cells(im, box):
    from PIL import Image
    import numpy as np
    x, y, w, h = box
    cells = (max(1, round(w / PAINT_CELL)), max(1, round(h / PAINT_CELL)))
    return np.asarray(im.crop((x, y, x + w, y + h)).resize(cells, Image.BOX), dtype=np.int16)


def paint_compare(ch_dir, ns_dir):
    from PIL import Image
    import numpy as np
    ch, visible = load_paint(ch_dir / "paint.tsv")
    ns, _ = load_paint(ns_dir / "paint.tsv")
    if not ch or not ns or not (ns_dir / "paint.png").exists():
        return None
    with Image.open(ch_dir / "paint.png") as a, Image.open(ns_dir / "paint.png") as b:
        ci, ni = a.convert("RGB"), b.convert("RGB")
    bottom = min(ci.height, ni.height)
    compared, differs = 0, {}
    for p in matching(ch, ns) & visible:
        c, n = ch[p][1], ns[p][1]
        w, h = min(c[2], n[2]), min(c[3], n[3])
        if w < PAINT_CELL or h < PAINT_CELL or min(c[0], c[1], n[0], n[1]) < 0:
            continue
        if max(c[0], n[0]) + w > min(ci.width, ni.width) or max(c[1], n[1]) + h > bottom:
            continue
        compared += 1
        bad = np.abs(paint_cells(ci, (c[0], c[1], w, h)) - paint_cells(ni, (n[0], n[1], w, h))).max(axis=2) > PAINT_LIMIT
        if bad.mean() > PAINT_SHARE:
            differs[p] = (int(bad.sum()) * PAINT_CELL * PAINT_CELL, (c[0], c[1], w, h), (n[0], n[1], w, h))
    above = set()
    for p in differs:
        q = parent(p)
        while q and q not in above:
            above.add(q)
            q = parent(q)
    leaves = sorted(((v[0], p, ch[p][0], v[1], v[2]) for p, v in differs.items() if p not in above), reverse=True)
    return {"compared": compared, "differ": len(differs), "leaves": leaves, "images": (ci, ni)}


def paint_figure(ci, ni, cbox, nbox, label, dest):
    from PIL import Image, ImageDraw
    pad = 24
    crops = []
    for im, (x, y, w, h) in ((ci, cbox), (ni, nbox)):
        crops.append(im.crop((max(0, x - pad), max(0, y - pad), min(im.width, x + w + pad), min(im.height, y + h + pad))))
    width, height = sum(c.width for c in crops) + 12, max(c.height for c in crops) + 20
    fig = Image.new("RGB", (width, height), (255, 255, 255))
    draw = ImageDraw.Draw(fig)
    x = 0
    for crop, name in zip(crops, ("Chrome", label)):
        draw.text((x + 4, 4), name, fill=(200, 0, 0))
        fig.paste(crop, (x, 20))
        x += crop.width + 12
    if width > 1600:
        fig = fig.resize((1600, max(1, round(height * 1600 / width))), Image.LANCZOS)
    fig.save(dest)


def cmd_paint(a):
    try:
        import numpy
        from PIL import Image
    except ImportError:
        fail("paint needs Pillow and numpy (pip install pillow numpy)")
    Image.MAX_IMAGE_PIXELS = None
    names, version = prepare(a)
    labels = a.labels
    ref = Path(a.out) / "snapshot-chrome" / ("%s-%dx%d-%s" % (version or "unknown", a.vw, a.vh, a.locale))
    httpd, base_url = serve(a.snapshots, a.settle_ms)
    skipped, errors = {}, {label: {} for label in labels}
    try:
        keys = {n: paint_key(a, Path(a.snapshots) / n) for n in names}
        todo = [n for n in names if a.refresh or read_line(ref / n / "paint-key") != keys[n]]
        if todo:
            print("chrome %s: painting %d snapshots" % (version, len(todo)), flush=True)
            chrome = ChromeLoads(a.chrome, a)
            try:
                for name in todo:
                    n, err = chrome_paint(chrome, base_url + urllib.parse.quote(name) + "/index.html", a, ref / name)
                    (ref / name).mkdir(parents=True, exist_ok=True)
                    (ref / name / "paint-key").write_text("" if err else keys[name] + "\n")
                    print("  %-22s %6d elements%s" % (name, n or 0, "  " + err if err else ""), flush=True)
                    if err:
                        skipped[name] = "Chrome: " + err
            finally:
                chrome.close()
        for binary, label in zip(a.bins, labels):
            dest = Path(a.out) / "snapshot-ns" / label / "paint"
            print("nordstjernen %s: painting %d snapshots" % (label, len(names)), flush=True)

            def one(name):
                t0 = time.time()
                n, err = ns_paint(binary, base_url + urllib.parse.quote(name) + "/index.html", a, dest / name)
                return name, n, err, time.time() - t0

            with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
                for name, n, err, secs in ex.map(one, names):
                    print("  %-22s %6d elements %5.1fs%s" % (name, n, secs, "  " + err if err else ""), flush=True)
                    if err:
                        errors[label][name] = err
    finally:
        httpd.shutdown()
    report = Path(a.report or Path(a.out) / "report")
    figures = report / "paint"
    shutil.rmtree(figures, ignore_errors=True)
    figures.mkdir(parents=True, exist_ok=True)
    rows, worst = [], []
    newest = labels[-1]
    for name in names:
        if name in skipped:
            continue
        ch_rects, _ = load_paint(ref / name / "paint.tsv")
        if len(ch_rects) < MIN_ELEMENTS:
            skipped[name] = "Chrome found %d elements" % len(ch_rects)
            continue
        row = {"site": name}
        for label in labels:
            got = paint_compare(ref / name, Path(a.out) / "snapshot-ns" / label / "paint" / name)
            if not got:
                errors[label].setdefault(name, "no elements or no screenshot")
                row[label] = None
                continue
            row[label] = {"compared": got["compared"], "differ": got["differ"],
                          "share": round(100.0 * (got["compared"] - got["differ"]) / got["compared"], 2)
                          if got["compared"] else None,
                          "leaves": [[p, el, area] for area, p, el, _, _ in got["leaves"][:50]]}
            if label == newest:
                ci, ni = got["images"]
                for area, p, el, cbox, nbox in got["leaves"][:PAINT_SHOWN]:
                    worst.append((area, name, p, el, cbox, nbox, ci, ni))
                worst = sorted(worst, key=lambda w: w[0], reverse=True)[:PAINT_SHOWN]
        rows.append(row)
    if not rows:
        fail("nothing to compare (%s)" % "; ".join("%s: %s" % kv for kv in skipped.items()))
    totals = {}
    for label in labels:
        done = [r[label] for r in rows if r[label]]
        compared, differ = sum(d["compared"] for d in done), sum(d["differ"] for d in done)
        totals[label] = {"compared": compared, "differ": differ,
                         "share": round(100.0 * (compared - differ) / compared, 2) if compared else None}
    lines = ["# Frozen snapshots: paint, %s vs Chrome" % " vs ".join(labels), "",
             "Chrome %s · viewport %s · Nordstjernen locale %s · %d sites · first %d px of each page" % (
                 version, "%dx%d" % (a.vw, a.vh), a.locale, len(rows), a.max_height), "",
             "Of the elements Chrome paints itself (visible, not fully transparent, not clipped away by an overflow, not "
             "under another element on the first screen, and with a "
             "background, a border, a "
             "shadow, text of its own, a ::before or ::after box, or replaced content) that the build lays out where Chrome does, the share whose "
             "box also looks the same in both screenshots: both crops are reduced to %d px cells, and "
             "a box differs when more than %d%% of its cells differ by more than %d in a colour channel. The list "
             "below names the deepest boxes that differ, the largest first." % (
                 PAINT_CELL, round(PAINT_SHARE * 100), PAINT_LIMIT), "",
             "| Site | %s |" % " | ".join("%s compared | %s paint the same" % (k, k) for k in labels),
             "| --- | %s |" % " | ".join(["---: | ---:"] * len(labels))]
    for r in rows:
        lines.append("| %s | %s |" % (r["site"], " | ".join(
            "%s | %s" % (fmt((r[k] or {}).get("compared")), fmt((r[k] or {}).get("share"), ".2f") + "%"
                         if (r[k] or {}).get("share") is not None else "-") for k in labels)))
    lines.append("| **All** | %s |" % " | ".join("%s | **%s%%**" % (fmt(totals[k]["compared"]), fmt(totals[k]["share"], ".2f"))
                                                for k in labels))
    if worst:
        lines += ["", "## Where %s paints differently" % newest, "",
                  "| Area px | Site | Element | Path | Chrome box | Figure |", "| ---: | --- | --- | --- | --- | --- |"]
        for k, (area, name, p, el, cbox, nbox, ci, ni) in enumerate(worst, 1):
            figure = "%02d-%s.png" % (k, name)
            paint_figure(ci, ni, cbox, nbox, newest, figures / figure)
            lines.append("| %s | %s | `%s` | `%s` | %s | [%s](paint/%s) |" % (
                fmt(area), name, el[:50], p, ",".join(map(str, cbox)), figure, figure))
    if skipped:
        lines += ["", "Skipped: " + "; ".join("%s (%s)" % kv for kv in sorted(skipped.items()))]
    for label in labels:
        lines += failure_lines(label, errors[label])
    meta = dict(common_meta(a, version, [r["site"] for r in rows], 0), maxHeight=a.max_height,
                builds={label: {"path": b, "sha256": file_digest(b)} for b, label in zip(a.bins, labels)})
    del meta["elements"]
    (report / "paint.json").write_text(json.dumps({"meta": meta, "totals": totals, "sites": rows, "skipped": skipped,
                                                   "failed": errors}, indent=1) + "\n")
    (report / "paint.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    print("\nreport: %s" % (report / "paint.md"))
    return 1 if any(errors.values()) else 0


def main():
    if sys.platform == "win32":
        fail("runs on Linux and macOS only")
    p = argparse.ArgumentParser(description="Freeze sites into local snapshots and compare every element's box, "
                                            "and the cost of loading them, in Nordstjernen with Chrome's.")
    sub = p.add_subparsers(dest="cmd", required=True)
    shared = argparse.ArgumentParser(add_help=False)
    shared.add_argument("--out", default=os.environ.get("OUT", "sitebench-out"))
    shared.add_argument("--snapshots", default=None, help="snapshot root (default OUT/snapshots)")
    shared.add_argument("--only", default="")
    shared.add_argument("--viewport", default=os.environ.get("VIEWPORT", "1280x800"))
    shared.add_argument("--locale", default=os.environ.get("NS_LOCALE", "en_US.UTF-8"))
    shared.add_argument("--chrome", default=default_chrome())
    shared.add_argument("--jobs", type=int, default=4)
    shared.add_argument("--settle-ms", type=int, default=3000)
    shared.add_argument("--timeout", type=int, default=150, help="seconds per Nordstjernen run")
    shared.add_argument("--refresh", action="store_true", help="freeze again, or measure Chrome again")
    shared.add_argument("--report", default=None, help="report directory (default OUT/report)")
    shared.add_argument("--levers", type=int, default=15, help="failure sources to list")
    f = sub.add_parser("freeze", parents=[shared], help="save script-free copies of the sites")
    f.add_argument("--sites", default=str(HERE / "sites.tsv"))
    f.add_argument("--category", default="")
    s = sub.add_parser("score", parents=[shared], help="compare one build with Chrome")
    s.add_argument("--bin", default=os.environ.get("NS_BIN") or default_binary())
    s.add_argument("--label", default="nordstjernen")
    s.add_argument("--min-share", type=float, default=None, help="fail below this share of matching elements")
    b = sub.add_parser("ab", parents=[shared], help="compare two builds, each with Chrome")
    b.add_argument("before")
    b.add_argument("after")
    b.add_argument("--labels", default="before,after")
    b.add_argument("--max-site-loss", type=int, default=None,
                   help="also fail when the new build loses more than this many of the elements the old one "
                        "matched on one site, beyond that site's noise")
    t = sub.add_parser("perf", parents=[shared], help="time one or more builds and Chrome loading the snapshots")
    t.add_argument("bins", nargs="*", help="builds to time, oldest first (default: NS_BIN or builddir's build)")
    t.add_argument("--labels", default=None, help="names of the builds (default: nordstjernen, or before,after)")
    t.add_argument("--runs", type=int, default=5, help="loads per site and browser")
    t.add_argument("--max-slowdown", type=float, default=None,
                   help="fail when the newest build spends more than this many percent more main-thread CPU, "
                        "summed over the sites, than the oldest")
    g = sub.add_parser("paint", parents=[shared], help="compare the screenshots of one or more builds with Chrome's")
    g.add_argument("bins", nargs="*", help="builds to compare, oldest first (default: NS_BIN or builddir's build)")
    g.add_argument("--labels", default=None, help="names of the builds (default: nordstjernen, or before,after)")
    g.add_argument("--max-height", type=int, default=8000, help="compare the page down to this many pixels")
    g.set_defaults(jobs=1)
    a = p.parse_args()
    a.only = {x for x in a.only.split(",") if x}
    if a.cmd == "freeze":
        a.category = {x for x in a.category.split(",") if x}
    a.snapshots = a.snapshots or os.path.join(a.out, "snapshots")
    try:
        a.vw, a.vh = (int(x) for x in a.viewport.lower().split("x"))
    except ValueError:
        p.error("--viewport must look like 1280x800")
    if a.cmd == "ab":
        a.labels = [x for x in a.labels.split(",") if x]
        if len(a.labels) != 2 or a.labels[0] == a.labels[1]:
            p.error("--labels needs two different names")
        for binary in (a.before, a.after):
            if not os.access(binary, os.X_OK):
                p.error("not an executable: %s" % binary)
        return cmd_ab(a)
    if a.cmd in ("perf", "paint"):
        a.bins = a.bins or [os.environ.get("NS_BIN") or default_binary()]
        default = ["nordstjernen"] if len(a.bins) == 1 else ["before", "after"] if len(a.bins) == 2 else None
        a.labels = [x for x in a.labels.split(",") if x] if a.labels else default
        if not a.labels or len(a.labels) != len(a.bins) or len(set(a.labels)) != len(a.labels) or "chrome" in a.labels:
            p.error("--labels needs one different name per build, other than chrome")
        if a.cmd == "perf" and a.runs < 1:
            p.error("--runs must be 1 or more")
        if a.cmd == "paint" and a.max_height < a.vh:
            p.error("--max-height must be at least the viewport height")
        for binary in a.bins:
            if not os.access(binary, os.X_OK):
                p.error("not an executable: %s" % binary)
        return cmd_perf(a) if a.cmd == "perf" else cmd_paint(a)
    if a.cmd == "score":
        if not os.access(a.bin, os.X_OK):
            p.error("not an executable: %s" % a.bin)
        return cmd_score(a)
    return cmd_freeze(a)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except Exception:
        traceback.print_exc()
        sys.exit(2)
