# Browser extensions (WebExtensions, initial support)

Nordstjernen has initial, deliberately scoped support for the
cross-browser **WebExtensions** API used by Firefox and Chromium. The
goal is source-compatibility with simple, page-facing extensions —
*content scripts* that modify pages plus a small slice of the `browser.*`
(a.k.a. `chrome.*`) JavaScript API — not full parity. The implementation
lives in `src/ext.c` and is wired into the engine from `src/js.c`.

## Installing an extension

Extensions are loaded **unpacked** (a directory containing
`manifest.json`); `.xpi`/`.crx` archives are not unpacked yet. Two
locations are scanned at renderer start:

- Every immediate sub-directory of
  `$XDG_DATA_HOME/nordstjernen/extensions/` (on Linux, by default
  `~/.local/share/nordstjernen/extensions/<ext>/`).
- Any path in the `NS_EXTENSIONS_DIR` environment variable
  (`:`-separated). Each entry is treated as a directory of extensions,
  or — if it directly contains a `manifest.json` — as a single
  extension.

## What is supported

- **Manifest** parsing: `name`, `version`, the extension id from
  `browser_specific_settings.gecko.id` (or legacy `applications.gecko.id`,
  falling back to the directory name), `content_scripts`, and
  `declarative_net_request`.
- **Content blocking (`declarativeNetRequest`)**: static rule lists
  declared via `declarative_net_request.rule_resources` (each with
  `path` and `enabled`). Rules support `priority`, `action.type` of
  `block` / `allow` / `allowAllRequests`, and conditions `urlFilter`
  (Adblock-Plus-style `||`, `|`, `^`, `*`), `regexFilter`,
  `isUrlFilterCaseSensitive`, `requestDomains` / `excludedRequestDomains`,
  `initiatorDomains` / `excludedInitiatorDomains`, and `resourceTypes` /
  `excludedResourceTypes` (`script`, `stylesheet`, `image`, `font`,
  `media`). The highest-priority matching rule wins; `allow` beats
  `block` at equal priority. Rules are evaluated in the renderer's
  network path, so no per-request JavaScript runs. This is enough to
  drive ad/tracker blocking from a hosts-style or filter-list ruleset.
- **Content scripts**: `matches`, `js`, `css`, and `run_at`
  (`document_start` runs before page scripts; `document_end` /
  `document_idle` run after `load`). `css` files are injected as a
  `<style>` element. Match patterns support `<all_urls>`, `*` schemes,
  `*`/`*.domain` hosts, and `*` path globs.
- **`browser` / `chrome` API** exposed per-extension to its content
  scripts:
  - `runtime`: `id`, `getManifest()`, `getURL()`, `getPlatformInfo()`,
    `sendMessage()` / `onMessage` (delivered within the page's own
    context), `lastError`.
  - `storage.local` / `storage.sync` / `storage.managed`: `get`, `set`,
    `remove`, `clear` — promise-based, persisted per-extension under
    `$XDG_DATA_HOME/nordstjernen/ext-storage/<hash>/`. Disabled in
    private mode.
  - `i18n`: `getMessage()`, `getUILanguage()`, `getAcceptLanguages()`.
  - `extension.getURL()`.

## Current limitations

Content scripts run in the **page's** JavaScript world (not an isolated
world), and each is wrapped so that its `browser`/`chrome` object is
scoped to that extension. There is no background page / service-worker
host, no `tabs`/`windows`/blocking `webRequest`, no toolbar/popup UI, and
`getURL()` returns a `file://` URL (there is no `moz-extension://`
scheme). Extensions are unsigned and fully trusted — only install code
you trust.

`declarativeNetRequest` is **static-rules only** (no dynamic/session
rules JS API). `resourceTypes` / `excludedResourceTypes` are matched by
inferring the type from the request URL's file extension, so a rule that
restricts by type only matches when the type can be inferred (it fails
open — does not block — for extension-less URLs). Rules are applied to
**sub-resource** requests only; top-level document navigations are never
blocked. `redirect`, `modifyHeaders`, and `upgradeScheme` actions are
ignored.

## Example: content script

```
~/.local/share/nordstjernen/extensions/hello/
├── manifest.json
└── content.js
```

```json
{
  "manifest_version": 2,
  "name": "Hello",
  "version": "1.0",
  "browser_specific_settings": { "gecko": { "id": "hello@example" } },
  "content_scripts": [
    { "matches": ["*://*/*"], "js": ["content.js"], "run_at": "document_end" }
  ]
}
```

```js
document.title = "[" + browser.runtime.getManifest().name + "] " + document.title;
browser.storage.local.set({ seen: Date.now() });
```

## Example: ad blocker

A minimal content blocker using `declarativeNetRequest`. It blocks
requests to a few ad/tracker hosts and any URL with an `/ads/` path
segment, and hides leftover ad containers with an injected stylesheet.

```
~/.local/share/nordstjernen/extensions/adblock/
├── manifest.json
├── rules.json
└── hide.css
```

`manifest.json`:

```json
{
  "manifest_version": 3,
  "name": "Tiny Adblock",
  "version": "1.0",
  "browser_specific_settings": { "gecko": { "id": "adblock@example" } },
  "permissions": ["declarativeNetRequest"],
  "declarative_net_request": {
    "rule_resources": [
      { "id": "ruleset", "enabled": true, "path": "rules.json" }
    ]
  },
  "content_scripts": [
    { "matches": ["*://*/*"], "css": ["hide.css"], "run_at": "document_start" }
  ]
}
```

`rules.json` — higher-priority rules win, so the `allow` rule keeps the
first-party CDN working even though it would match the generic `/ads/`
block:

```json
[
  { "id": 1, "priority": 1, "action": { "type": "block" },
    "condition": { "urlFilter": "||doubleclick.net^" } },
  { "id": 2, "priority": 1, "action": { "type": "block" },
    "condition": { "urlFilter": "||googlesyndication.com^" } },
  { "id": 3, "priority": 1, "action": { "type": "block" },
    "condition": { "urlFilter": "||google-analytics.com^" } },
  { "id": 4, "priority": 1, "action": { "type": "block" },
    "condition": { "urlFilter": "/ads/",
                   "resourceTypes": ["script", "image", "media"] } },
  { "id": 5, "priority": 2, "action": { "type": "allow" },
    "condition": { "urlFilter": "/ads/", "requestDomains": ["cdn.example.com"] } }
]
```

`hide.css`:

```css
.ad, .ads, .advert, [id^="google_ads_"], ins.adsbygoogle { display: none !important; }
```

The network rules are evaluated for every sub-resource the page loads, so
blocked hosts never leave the browser; the stylesheet removes ad slots
that have no network request of their own. For real coverage, generate
`rules.json` from a filter list (e.g. convert EasyList to the
`declarativeNetRequest` rule format) — the format above is exactly what
the converters emit.
