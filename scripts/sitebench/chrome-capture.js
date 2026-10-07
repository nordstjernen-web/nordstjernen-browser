/* chrome-capture.js — records screenshots, a filmstrip and load/runtime metrics for each site in Chrome. */
'use strict';

const fs = require('fs');
const path = require('path');
const os = require('os');

const HERE = __dirname;

function loadPlaywright() {
  try {
    return require('playwright');
  } catch (e) {
    try {
      return require(path.join(HERE, 'node_modules', 'playwright'));
    } catch (e2) {
      console.error('chrome-capture: playwright not found; run `npm install` in scripts/sitebench ' +
                    'or set NODE_PATH to a directory containing it');
      process.exit(2);
    }
  }
}

function parseArgs(argv) {
  const o = {
    sites: path.join(HERE, 'sites.tsv'),
    out: path.resolve('sitebench-out'),
    label: 'chrome',
    only: null,
    category: null,
    viewport: '1280x800',
    runs: 1,
    timeoutMs: 30000,
    settleMs: 3000,
    fullMax: 8000,
    channel: null,
    executable: null,
    filmstrip: true,
    skipExisting: false,
  };
  for (const a of argv) {
    const [k, v] = a.includes('=') ? [a.slice(0, a.indexOf('=')), a.slice(a.indexOf('=') + 1)] : [a, ''];
    switch (k) {
      case '--sites': o.sites = path.resolve(v); break;
      case '--out': o.out = path.resolve(v); break;
      case '--label': o.label = v; break;
      case '--only': o.only = new Set(v.split(',').filter(Boolean)); break;
      case '--category': o.category = new Set(v.split(',').filter(Boolean)); break;
      case '--viewport': o.viewport = v; break;
      case '--runs': o.runs = Math.max(1, parseInt(v, 10) || 1); break;
      case '--timeout-ms': o.timeoutMs = parseInt(v, 10) || o.timeoutMs; break;
      case '--settle-ms': o.settleMs = parseInt(v, 10); break;
      case '--full-max': o.fullMax = parseInt(v, 10) || o.fullMax; break;
      case '--channel': o.channel = v; break;
      case '--executable': o.executable = v; break;
      case '--no-filmstrip': o.filmstrip = false; break;
      case '--skip-existing': o.skipExisting = true; break;
      case '-h': case '--help':
        console.log('usage: node chrome-capture.js [--sites=FILE] [--out=DIR] [--only=id,..] [--category=c,..]\n' +
                    '  [--viewport=WxH] [--runs=N] [--timeout-ms=N] [--settle-ms=N] [--full-max=PX]\n' +
                    '  [--channel=chrome] [--executable=PATH] [--no-filmstrip] [--skip-existing]');
        process.exit(0);
        break;
      default:
        console.error(`chrome-capture: unknown option ${a}`);
        process.exit(2);
    }
  }
  const m = /^(\d+)x(\d+)$/.exec(o.viewport);
  if (!m) { console.error('chrome-capture: --viewport wants WxH'); process.exit(2); }
  o.width = +m[1];
  o.height = +m[2];
  return o;
}

function readSites(file, only, category) {
  return fs.readFileSync(file, 'utf8').split('\n')
    .map(l => l.trim())
    .filter(l => l && !l.startsWith('#'))
    .map(l => { const [id, cat, url] = l.split('\t'); return { id, category: cat, url }; })
    .filter(s => s.id && s.url)
    .filter(s => !only || only.has(s.id))
    .filter(s => !category || category.has(s.category));
}

const OBSERVERS = `(() => {
  try {
    const sb = window.__sitebench = { lcp: null, cls: 0, longTasks: [], lcpTag: null };
    new PerformanceObserver(l => {
      for (const e of l.getEntries()) {
        sb.lcp = Math.round(e.renderTime || e.loadTime || e.startTime);
        sb.lcpTag = e.element ? e.element.tagName.toLowerCase() : null;
      }
    }).observe({ type: 'largest-contentful-paint', buffered: true });
    new PerformanceObserver(l => {
      for (const e of l.getEntries()) if (!e.hadRecentInput) sb.cls += e.value;
    }).observe({ type: 'layout-shift', buffered: true });
    new PerformanceObserver(l => {
      for (const e of l.getEntries()) sb.longTasks.push([Math.round(e.startTime), Math.round(e.duration)]);
    }).observe({ type: 'longtask', buffered: true });
  } catch (e) {}
})();`;

function median(xs) {
  const v = xs.filter(x => typeof x === 'number' && isFinite(x)).sort((a, b) => a - b);
  if (!v.length) return null;
  const mid = v.length >> 1;
  return v.length % 2 ? v[mid] : (v[mid - 1] + v[mid]) / 2;
}

function processTreeRssKb(rootPid) {
  if (process.platform !== 'linux' || !rootPid) return null;
  const children = new Map();
  for (const name of fs.readdirSync('/proc')) {
    if (!/^\d+$/.test(name)) continue;
    try {
      const stat = fs.readFileSync(`/proc/${name}/stat`, 'utf8');
      const ppid = +stat.slice(stat.lastIndexOf(')') + 2).split(' ')[1];
      if (!children.has(ppid)) children.set(ppid, []);
      children.get(ppid).push(+name);
    } catch (e) {}
  }
  let total = 0;
  const stack = [...(children.get(rootPid) || [])];
  while (stack.length) {
    const pid = stack.pop();
    try {
      const m = /VmRSS:\s+(\d+)/.exec(fs.readFileSync(`/proc/${pid}/status`, 'utf8'));
      if (m) total += +m[1];
    } catch (e) {}
    for (const c of children.get(pid) || []) stack.push(c);
  }
  return total;
}

function cpuTimesMs() {
  return os.cpus().reduce((acc, c) => acc + c.times.user + c.times.sys, 0);
}

async function captureRun(browser, site, o, dir, visual, browserPid) {
  const probeSrc = fs.readFileSync(path.join(HERE, 'probe.js'), 'utf8');
  const ctx = await browser.newContext({
    viewport: { width: o.width, height: o.height },
    deviceScaleFactor: 1,
    userAgent: o.userAgent,
    locale: 'en-US',
    timezoneId: 'UTC',
  });
  const page = await ctx.newPage();
  await page.addInitScript(OBSERVERS);
  const cdp = await ctx.newCDPSession(page);
  await cdp.send('Performance.enable', { timeDomain: 'threadTicks' });
  await cdp.send('Network.enable');

  const net = { requests: 0, failed: 0, bytes: 0, byType: {} };
  const reqType = new Map();
  cdp.on('Network.requestWillBeSent', e => reqType.set(e.requestId, e.type || 'Other'));
  cdp.on('Network.loadingFinished', e => {
    const t = reqType.get(e.requestId) || 'Other';
    net.requests++;
    net.bytes += e.encodedDataLength || 0;
    const b = net.byType[t] || (net.byType[t] = { requests: 0, bytes: 0 });
    b.requests++;
    b.bytes += e.encodedDataLength || 0;
  });
  cdp.on('Network.loadingFailed', () => { net.failed++; });

  const consoleErrors = [];
  page.on('console', m => { if (m.type() === 'error') consoleErrors.push(m.text().slice(0, 300)); });
  page.on('pageerror', e => consoleErrors.push(String(e && e.message || e).slice(0, 300)));

  const frames = [];
  if (visual && o.filmstrip) {
    cdp.on('Page.screencastFrame', f => {
      frames.push({ t: f.metadata.timestamp * 1000, data: f.data });
      cdp.send('Page.screencastFrameAck', { sessionId: f.sessionId }).catch(() => {});
    });
    await cdp.send('Page.startScreencast', {
      format: 'jpeg', quality: 70, maxWidth: Math.round(o.width / 2), maxHeight: Math.round(o.height / 2),
      everyNthFrame: 1,
    });
  }

  const cpu0 = cpuTimesMs();
  const t0 = Date.now();
  let status = null;
  let error = null;
  let loadWallMs = null;
  try {
    const resp = await page.goto(site.url, { waitUntil: 'load', timeout: o.timeoutMs });
    status = resp ? resp.status() : null;
    loadWallMs = Date.now() - t0;
  } catch (e) {
    error = String(e.message || e).split('\n')[0];
  }
  if (o.settleMs > 0) {
    await page.waitForLoadState('networkidle', { timeout: o.settleMs }).catch(() => {});
  }
  await page.waitForTimeout(500);
  const settledWallMs = Date.now() - t0;
  const cpuMs = cpuTimesMs() - cpu0;

  if (visual && o.filmstrip) await cdp.send('Page.stopScreencast').catch(() => {});

  const perf = {};
  try {
    for (const m of (await cdp.send('Performance.getMetrics')).metrics) perf[m.name] = m.value;
  } catch (e) {}
  let probe = null;
  try {
    probe = JSON.parse(await page.evaluate(probeSrc));
  } catch (e) {
    error = error || `probe: ${String(e.message || e).split('\n')[0]}`;
  }
  const timeOrigin = await page.evaluate('performance.timeOrigin').catch(() => t0);
  const rssKb = processTreeRssKb(browserPid);

  if (visual) {
    await page.evaluate('window.scrollTo(0, 0)').catch(() => {});
    await page.screenshot({ path: path.join(dir, 'viewport.png'), timeout: 15000 }).catch(e => {
      error = error || `screenshot: ${e.message.split('\n')[0]}`;
    });
    const docH = Math.min(Math.max(probe ? probe.docH : o.height, o.height), o.fullMax);
    await page.screenshot({
      path: path.join(dir, 'full.png'), fullPage: true, timeout: 30000,
      clip: { x: 0, y: 0, width: o.width, height: docH },
    }).catch(() => {});
    if (frames.length) {
      const fdir = path.join(dir, 'frames');
      fs.rmSync(fdir, { recursive: true, force: true });
      fs.mkdirSync(fdir, { recursive: true });
      let lastBucket = -1;
      const index = [];
      for (const f of frames) {
        const ms = Math.max(0, Math.round(f.t - timeOrigin));
        const bucket = Math.floor(ms / 100);
        if (bucket === lastBucket) index.pop();
        lastBucket = bucket;
        index.push({ ms, file: `${String(ms).padStart(6, '0')}.jpg`, data: f.data });
      }
      for (const f of index) fs.writeFileSync(path.join(fdir, f.file), Buffer.from(f.data, 'base64'));
      fs.writeFileSync(path.join(fdir, 'index.json'),
        JSON.stringify(index.map(f => ({ ms: f.ms, file: f.file }))));
    }
  }
  await ctx.close();

  const obs = probe && probe.observed || {};
  const fcp = probe && probe.paint ? probe.paint['first-contentful-paint'] : null;
  const tbt = (obs.longTasks || [])
    .filter(([start]) => fcp == null || start >= fcp)
    .reduce((acc, [, d]) => acc + Math.max(0, d - 50), 0);
  return {
    status, error, loadWallMs, settledWallMs,
    ttfb: probe && probe.nav ? probe.nav.ttfb : null,
    fp: probe && probe.paint ? probe.paint['first-paint'] : null,
    fcp,
    lcp: obs.lcp != null ? obs.lcp : null,
    cls: obs.cls != null ? Math.round(obs.cls * 1000) / 1000 : null,
    tbt,
    dcl: probe && probe.nav ? probe.nav.dcl : null,
    load: probe && probe.nav ? probe.nav.load : null,
    mainThreadMs: perf.TaskDuration != null ? Math.round(perf.TaskDuration * 1000) : null,
    scriptMs: perf.ScriptDuration != null ? Math.round(perf.ScriptDuration * 1000) : null,
    styleMs: perf.RecalcStyleDuration != null ? Math.round(perf.RecalcStyleDuration * 1000) : null,
    layoutMs: perf.LayoutDuration != null ? Math.round(perf.LayoutDuration * 1000) : null,
    layoutCount: perf.LayoutCount != null ? perf.LayoutCount : null,
    styleCount: perf.RecalcStyleCount != null ? perf.RecalcStyleCount : null,
    jsHeapMb: perf.JSHeapUsedSize != null ? Math.round(perf.JSHeapUsedSize / 1048576 * 10) / 10 : null,
    systemCpuMs: Math.round(cpuMs),
    browserRssMb: rssKb != null ? Math.round(rssKb / 1024) : null,
    requests: net.requests, failedRequests: net.failed, bytes: net.bytes, byType: net.byType,
    consoleErrors: consoleErrors.length,
    consoleSample: consoleErrors.slice(0, 5),
    probe,
  };
}

const SUMMARY_KEYS = ['loadWallMs', 'settledWallMs', 'ttfb', 'fp', 'fcp', 'lcp', 'cls', 'tbt', 'dcl', 'load',
  'mainThreadMs', 'scriptMs', 'styleMs', 'layoutMs', 'layoutCount', 'styleCount', 'jsHeapMb',
  'systemCpuMs', 'browserRssMb', 'requests', 'bytes', 'consoleErrors'];

async function main() {
  const o = parseArgs(process.argv.slice(2));
  const { chromium } = loadPlaywright();
  const sites = readSites(o.sites, o.only, o.category);
  if (!sites.length) { console.error('chrome-capture: no sites selected'); process.exit(2); }
  const launch = { headless: true, args: ['--hide-scrollbars', '--force-color-profile=srgb', '--font-render-hinting=none'] };
  if (o.channel) launch.channel = o.channel;
  if (o.executable) launch.executablePath = o.executable;
  const browser = await chromium.launch(launch);
  const version = browser.version();
  const browserPid = process.pid;
  const bcdp = await browser.newBrowserCDPSession();
  o.userAgent = (await bcdp.send('Browser.getVersion')).userAgent.replace('HeadlessChrome', 'Chrome');
  await bcdp.detach();
  console.log(`chrome-capture: ${version}, ${sites.length} sites, viewport ${o.width}x${o.height}, runs ${o.runs}`);

  for (const site of sites) {
    const dir = path.join(o.out, o.label, site.id);
    if (o.skipExisting && fs.existsSync(path.join(dir, 'metrics.json'))) continue;
    fs.mkdirSync(dir, { recursive: true });
    const runs = [];
    for (let r = 0; r < o.runs; r++) {
      try {
        runs.push(await captureRun(browser, site, o, dir, r === 0, browserPid));
      } catch (e) {
        runs.push({ error: String(e.message || e).split('\n')[0] });
      }
    }
    const first = runs[0] || {};
    const summary = {};
    for (const k of SUMMARY_KEYS) summary[k] = median(runs.map(r => r[k]));
    const result = {
      engine: 'chrome', version, site, viewport: { width: o.width, height: o.height },
      capturedAt: new Date().toISOString(), host: os.hostname(), cpus: os.cpus().length,
      status: first.status, error: first.error || null,
      summary, runs: runs.map(r => { const c = Object.assign({}, r); delete c.probe; return c; }),
    };
    fs.writeFileSync(path.join(dir, 'metrics.json'), JSON.stringify(result, null, 1));
    if (first.probe) fs.writeFileSync(path.join(dir, 'probe.json'), JSON.stringify(first.probe));
    console.log(`${site.id.padEnd(20)} ${String(first.status || '-').padEnd(4)} ` +
      `fcp=${summary.fcp ?? '-'} lcp=${summary.lcp ?? '-'} load=${summary.load ?? '-'} ` +
      `main=${summary.mainThreadMs ?? '-'}ms req=${summary.requests ?? '-'}` +
      (first.error ? `  ERR ${first.error}` : ''));
  }
  await browser.close();
}

main().catch(e => { console.error(e); process.exit(1); });
