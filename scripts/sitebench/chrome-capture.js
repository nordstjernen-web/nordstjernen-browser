/* chrome-capture.js — records screenshots, a filmstrip and load/runtime metrics for each site in Chrome. */
'use strict';

const fs = require('fs');
const path = require('path');
const os = require('os');

const HERE = __dirname;
const LOCALE = 'en-US';

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

async function openPage(browser, o) {
  const ctx = await browser.newContext({
    viewport: { width: o.width, height: o.height },
    deviceScaleFactor: 1,
    userAgent: o.userAgent,
    locale: LOCALE,
    timezoneId: 'UTC',
  });
  const page = await ctx.newPage();
  await page.addInitScript(OBSERVERS);
  const cdp = await ctx.newCDPSession(page);
  await cdp.send('Performance.enable', { timeDomain: 'threadTicks' });
  await cdp.send('Network.enable');
  return { ctx, page, cdp };
}

function trackNetwork(cdp) {
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
  return net;
}

function trackConsole(page) {
  const consoleErrors = [];
  page.on('console', m => { if (m.type() === 'error') consoleErrors.push(m.text().slice(0, 300)); });
  page.on('pageerror', e => consoleErrors.push(String(e && e.message || e).slice(0, 300)));
  return consoleErrors;
}

async function startFilmstrip(cdp, o) {
  const frames = [];
  cdp.on('Page.screencastFrame', f => {
    frames.push({ t: f.metadata.timestamp * 1000, data: f.data });
    cdp.send('Page.screencastFrameAck', { sessionId: f.sessionId }).catch(() => {});
  });
  await cdp.send('Page.startScreencast', {
    format: 'jpeg', quality: 70, maxWidth: Math.round(o.width / 2), maxHeight: Math.round(o.height / 2),
    everyNthFrame: 1,
  });
  return frames;
}

async function loadPage(page, site, o, t0) {
  const out = { status: null, error: null, loadWallMs: null };
  try {
    const resp = await page.goto(site.url, { waitUntil: 'load', timeout: o.timeoutMs });
    out.status = resp ? resp.status() : null;
    out.loadWallMs = Date.now() - t0;
  } catch (e) {
    out.error = String(e.message || e).split('\n')[0];
  }
  if (o.settleMs > 0) {
    await page.waitForLoadState('networkidle', { timeout: o.settleMs }).catch(() => {});
  }
  await page.waitForTimeout(500);
  return out;
}

async function readPerfMetrics(cdp) {
  const perf = {};
  try {
    for (const m of (await cdp.send('Performance.getMetrics')).metrics) perf[m.name] = m.value;
  } catch (e) {}
  return perf;
}

async function runProbe(page, probeSrc) {
  try {
    return { probe: JSON.parse(await page.evaluate(probeSrc)), error: null };
  } catch (e) {
    return { probe: null, error: `probe: ${String(e.message || e).split('\n')[0]}` };
  }
}

async function saveScreenshots(page, dir, o, probe) {
  let error = null;
  await page.evaluate('window.scrollTo(0, 0)').catch(() => {});
  await page.screenshot({ path: path.join(dir, 'viewport.png'), timeout: 15000 }).catch(e => {
    error = `screenshot: ${e.message.split('\n')[0]}`;
  });
  const docH = Math.min(Math.max(probe ? probe.docH : o.height, o.height), o.fullMax);
  await page.screenshot({
    path: path.join(dir, 'full.png'), fullPage: true, timeout: 30000,
    clip: { x: 0, y: 0, width: o.width, height: docH },
  }).catch(() => {});
  return error;
}

function writeFrames(fdir, frames, timeOrigin) {
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

function nullable(v, f) {
  return v != null ? f(v) : null;
}

function field(obj, key) {
  return obj ? obj[key] : null;
}

function blockingTimeMs(obs, fcp) {
  return (obs.longTasks || [])
    .filter(([start]) => fcp == null || start >= fcp)
    .reduce((acc, [, d]) => acc + Math.max(0, d - 50), 0);
}

function perfSummary(perf) {
  const ms = v => Math.round(v * 1000);
  const same = v => v;
  return {
    mainThreadMs: nullable(perf.TaskDuration, ms),
    scriptMs: nullable(perf.ScriptDuration, ms),
    styleMs: nullable(perf.RecalcStyleDuration, ms),
    layoutMs: nullable(perf.LayoutDuration, ms),
    layoutCount: nullable(perf.LayoutCount, same),
    styleCount: nullable(perf.RecalcStyleCount, same),
    jsHeapMb: nullable(perf.JSHeapUsedSize, v => Math.round(v / 1048576 * 10) / 10),
  };
}

function runSummary(run, probe, perf, net, consoleErrors) {
  const obs = probe && probe.observed || {};
  const nav = probe && probe.nav;
  const paint = probe && probe.paint;
  const fcp = field(paint, 'first-contentful-paint');
  return {
    status: run.status, error: run.error, loadWallMs: run.loadWallMs, settledWallMs: run.settledWallMs,
    ttfb: field(nav, 'ttfb'),
    fp: field(paint, 'first-paint'),
    fcp,
    lcp: nullable(obs.lcp, v => v),
    cls: nullable(obs.cls, v => Math.round(v * 1000) / 1000),
    tbt: blockingTimeMs(obs, fcp),
    dcl: field(nav, 'dcl'),
    load: field(nav, 'load'),
    ...perfSummary(perf),
    systemCpuMs: Math.round(run.cpuMs),
    browserRssMb: nullable(run.rssKb, v => Math.round(v / 1024)),
    requests: net.requests, failedRequests: net.failed, bytes: net.bytes, byType: net.byType,
    consoleErrors: consoleErrors.length,
    consoleSample: consoleErrors.slice(0, 5),
    probe,
  };
}

async function captureRun(browser, site, o, dir, visual, browserPid) {
  const probeSrc = fs.readFileSync(path.join(HERE, 'probe.js'), 'utf8');
  const { ctx, page, cdp } = await openPage(browser, o);
  const net = trackNetwork(cdp);
  const consoleErrors = trackConsole(page);
  const filmstrip = visual && o.filmstrip;
  const frames = filmstrip ? await startFilmstrip(cdp, o) : [];

  const cpu0 = cpuTimesMs();
  const t0 = Date.now();
  const run = await loadPage(page, site, o, t0);
  run.settledWallMs = Date.now() - t0;
  run.cpuMs = cpuTimesMs() - cpu0;

  if (filmstrip) await cdp.send('Page.stopScreencast').catch(() => {});

  const perf = await readPerfMetrics(cdp);
  const probed = await runProbe(page, probeSrc);
  run.error = run.error || probed.error;
  const timeOrigin = await page.evaluate('performance.timeOrigin').catch(() => t0);
  run.rssKb = processTreeRssKb(browserPid);

  if (visual) {
    const shotError = await saveScreenshots(page, dir, o, probed.probe);
    run.error = run.error || shotError;
    if (frames.length) writeFrames(path.join(dir, 'frames'), frames, timeOrigin);
  }
  await ctx.close();
  return runSummary(run, probed.probe, perf, net, consoleErrors);
}

const SUMMARY_KEYS = ['loadWallMs', 'settledWallMs', 'ttfb', 'fp', 'fcp', 'lcp', 'cls', 'tbt', 'dcl', 'load',
  'mainThreadMs', 'scriptMs', 'styleMs', 'layoutMs', 'layoutCount', 'styleCount', 'jsHeapMb',
  'systemCpuMs', 'browserRssMb', 'requests', 'bytes', 'consoleErrors'];

async function launchBrowser(chromium, o) {
  const launch = {
    headless: true,
    channel: o.channel || 'chromium',
    args: ['--hide-scrollbars', '--force-color-profile=srgb', '--font-render-hinting=none',
           '--disable-blink-features=AutomationControlled'],
  };
  if (o.executable) launch.executablePath = o.executable;
  const browser = await chromium.launch(launch);
  const bcdp = await browser.newBrowserCDPSession();
  o.userAgent = (await bcdp.send('Browser.getVersion')).userAgent.replace('HeadlessChrome', 'Chrome');
  await bcdp.detach();
  return browser;
}

async function captureRuns(browser, site, o, dir, browserPid) {
  const runs = [];
  for (let r = 0; r < o.runs; r++) {
    try {
      runs.push(await captureRun(browser, site, o, dir, r === 0, browserPid));
    } catch (e) {
      runs.push({ error: String(e.message || e).split('\n')[0] });
    }
  }
  return runs;
}

function logSite(site, first, summary) {
  console.log(`${site.id.padEnd(20)} ${String(first.status || '-').padEnd(4)} ` +
    `fcp=${summary.fcp ?? '-'} lcp=${summary.lcp ?? '-'} load=${summary.load ?? '-'} ` +
    `main=${summary.mainThreadMs ?? '-'}ms req=${summary.requests ?? '-'}` +
    (first.error ? `  ERR ${first.error}` : ''));
}

async function captureSite(browser, version, site, o, browserPid) {
  const dir = path.join(o.out, o.label, site.id);
  if (o.skipExisting && fs.existsSync(path.join(dir, 'metrics.json'))) return;
  fs.mkdirSync(dir, { recursive: true });
  const runs = await captureRuns(browser, site, o, dir, browserPid);
  const first = runs[0] || {};
  const summary = {};
  for (const k of SUMMARY_KEYS) summary[k] = median(runs.map(r => r[k]));
  const result = {
    engine: 'chrome', version, locale: LOCALE, site, viewport: { width: o.width, height: o.height },
    capturedAt: new Date().toISOString(), host: os.hostname(), cpus: os.cpus().length,
    status: first.status, error: first.error || null,
    summary, runs: runs.map(r => { const c = Object.assign({}, r); delete c.probe; return c; }),
  };
  fs.writeFileSync(path.join(dir, 'metrics.json'), JSON.stringify(result, null, 1));
  if (first.probe) fs.writeFileSync(path.join(dir, 'probe.json'), JSON.stringify(first.probe));
  logSite(site, first, summary);
}

async function main() {
  const o = parseArgs(process.argv.slice(2));
  const { chromium } = loadPlaywright();
  const sites = readSites(o.sites, o.only, o.category);
  if (!sites.length) { console.error('chrome-capture: no sites selected'); process.exit(2); }
  const browser = await launchBrowser(chromium, o);
  const version = browser.version();
  const browserPid = process.pid;
  console.log(`chrome-capture: ${version}, ${sites.length} sites, viewport ${o.width}x${o.height}, runs ${o.runs}`);
  for (const site of sites) await captureSite(browser, version, site, o, browserPid);
  await browser.close();
}

main().catch(e => { console.error(e); process.exit(1); });
