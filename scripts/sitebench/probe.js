/* probe.js — page inventory evaluated identically in Chrome and Nordstjernen. */
(function () {
  var SELECTOR = 'header,nav,main,footer,aside,section,article,h1,h2,h3,h4,p,a,button,' +
    'input,select,textarea,label,img,picture,svg,video,iframe,canvas,form,ul,ol,li,table,' +
    '[role=banner],[role=navigation],[role=main],[role=search],[role=button],[role=dialog]';
  var MAX_SCAN = 4000;
  var MAX_KEEP = 500;
  var SCREENS = 3;

  function num(v) { var n = parseFloat(v); return isFinite(n) ? Math.round(n * 10) / 10 : null; }
  function squash(s, n) { return String(s || '').replace(/\s+/g, ' ').trim().slice(0, n); }
  function safe(f, d) { try { var v = f(); return v === undefined ? d : v; } catch (e) { return d; } }

  var vw = safe(function () { return window.innerWidth; }, 0) ||
           safe(function () { return document.documentElement.clientWidth; }, 0);
  var vh = safe(function () { return window.innerHeight; }, 0) ||
           safe(function () { return document.documentElement.clientHeight; }, 0);
  var sy = safe(function () { return window.scrollY || window.pageYOffset || 0; }, 0);
  var root = document.documentElement;
  var body = document.body;

  function label(el, tag) {
    if (tag === 'img') return squash(el.getAttribute('alt') || (el.getAttribute('src') || '').split('?')[0].split('/').pop(), 40);
    if (tag === 'input' || tag === 'select' || tag === 'textarea')
      return squash([el.getAttribute('type'), el.getAttribute('name'), el.getAttribute('placeholder'), el.getAttribute('aria-label')].join(' '), 40);
    if (tag === 'svg' || tag === 'iframe' || tag === 'video' || tag === 'canvas')
      return squash(el.getAttribute('aria-label') || el.getAttribute('title') || el.getAttribute('id') || '', 40);
    return squash(el.textContent, 40);
  }

  function visibleRect(el) {
    var r = safe(function () { return el.getBoundingClientRect(); }, null);
    if (!r || !(r.width > 0) || !(r.height > 0)) return null;
    var top = r.top + sy;
    if (top > vh * SCREENS || r.bottom + sy < 0) return null;
    return r;
  }

  function hiddenByStyle(cs) {
    return !!cs && (cs.visibility === 'hidden' || cs.display === 'none' || cs.opacity === '0');
  }

  function styleSummary(cs) {
    if (!cs) return { fs: null, fw: '', ff: '', color: '', bg: '', disp: '' };
    return {
      fs: num(cs.fontSize),
      fw: String(cs.fontWeight || ''),
      ff: squash(String(cs.fontFamily || '').split(',')[0].replace(/["']/g, ''), 40),
      color: String(cs.color || ''),
      bg: String(cs.backgroundColor || ''),
      disp: String(cs.display || '')
    };
  }

  function component(el, r, cs) {
    var tag = el.tagName.toLowerCase();
    var st = styleSummary(cs);
    return {
      tag: tag,
      id: el.id || '',
      text: label(el, tag),
      x: Math.round(r.left), y: Math.round(r.top + sy),
      w: Math.round(r.width), h: Math.round(r.height),
      fs: st.fs, fw: st.fw, ff: st.ff, color: st.color, bg: st.bg, disp: st.disp
    };
  }

  function inventory() {
    var list = safe(function () { return document.querySelectorAll(SELECTOR); }, []);
    var out = [];
    var limit = Math.min(list.length, MAX_SCAN);
    for (var i = 0; i < limit && out.length < MAX_KEEP; i++) {
      var el = list[i];
      var r = visibleRect(el);
      if (!r) continue;
      var cs = safe(function () { return getComputedStyle(el); }, null);
      if (hiddenByStyle(cs)) continue;
      out.push(component(el, r, cs));
    }
    return out;
  }

  function navTiming() {
    var n = safe(function () { return performance.getEntriesByType('navigation')[0]; }, null);
    if (n) {
      return {
        ttfb: num(n.responseStart), responseEnd: num(n.responseEnd),
        domInteractive: num(n.domInteractive), dcl: num(n.domContentLoadedEventEnd),
        load: num(n.loadEventEnd), transferSize: n.transferSize || 0
      };
    }
    var t = safe(function () { return performance.timing; }, null);
    if (!t || !t.navigationStart) return null;
    function d(v) { return v ? v - t.navigationStart : null; }
    return { ttfb: d(t.responseStart), responseEnd: d(t.responseEnd), domInteractive: d(t.domInteractive),
             dcl: d(t.domContentLoadedEventEnd), load: d(t.loadEventEnd), transferSize: 0 };
  }

  function paints() {
    var o = {};
    safe(function () {
      performance.getEntriesByType('paint').forEach(function (e) { o[e.name] = num(e.startTime); });
    }, null);
    return o;
  }

  function resources() {
    var list = safe(function () { return performance.getEntriesByType('resource'); }, []) || [];
    var bytes = 0;
    for (var i = 0; i < list.length; i++) bytes += list[i].transferSize || 0;
    return { count: list.length, bytes: bytes };
  }

  return JSON.stringify({
    url: safe(function () { return location.href; }, ''),
    title: squash(document.title, 120),
    readyState: document.readyState,
    vw: vw, vh: vh,
    docW: safe(function () { return Math.max(root.scrollWidth, body ? body.scrollWidth : 0); }, 0),
    docH: safe(function () { return Math.max(root.scrollHeight, body ? body.scrollHeight : 0); }, 0),
    nodes: safe(function () { return document.getElementsByTagName('*').length; }, 0),
    scripts: safe(function () { return document.scripts.length; }, 0),
    styleSheets: safe(function () { return document.styleSheets.length; }, 0),
    images: safe(function () { return document.images.length; }, 0),
    iframes: safe(function () { return document.getElementsByTagName('iframe').length; }, 0),
    textLen: safe(function () { return squash(body ? body.innerText : '', 1e9).length; }, 0),
    bodyBg: safe(function () { return getComputedStyle(body).backgroundColor; }, ''),
    nav: navTiming(),
    paint: paints(),
    resources: resources(),
    observed: safe(function () { return window.__sitebench || null; }, null),
    components: inventory()
  });
})()
