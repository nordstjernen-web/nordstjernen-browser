/* Nordstjernen — a persistent renderer-process browser session.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

package org.nordstjernen;

import java.awt.image.BufferedImage;
import java.awt.image.DataBufferInt;
import java.nio.charset.StandardCharsets;

/**
 * A long-lived browser backed by a single {@code nordstjernen-renderer}
 * process. Unlike {@link RemotePage} (one page, then the renderer exits),
 * this keeps the renderer alive so an interactive shell can navigate, scroll,
 * render viewports, and follow links — the model the GTK shell uses.
 * No native engine is loaded into the JVM.
 *
 * <p>Not thread-safe; drive one instance from a single thread (or serialise).
 */
public final class RemoteBrowser implements AutoCloseable {

    /** Maximum framebuffer the renderer is asked to allocate. */
    public static final int MAX_W = 2560;
    public static final int MAX_H = 1600;

    /** Result of a render: the image plus any side-channel the page requested. */
    public static final class Frame {
        /** The rendered image, or null when {@link #unchanged} (reuse the prior one). */
        public final BufferedImage image;
        public final String nav;
        /** The renderer reported nothing changed since the last render. */
        public final boolean unchanged;
        /** The page is still animating / loading; keep rendering. */
        public final boolean animating;
        /** Origin requesting WebGL (needs a trust prompt), or null. */
        public final String webgl;
        /** A download the page initiated (URL), or null. */
        public final String download;

        Frame(BufferedImage image, String nav, boolean unchanged,
              boolean animating, String webgl, String download) {
            this.image = image;
            this.nav = nav;
            this.unchanged = unchanged;
            this.animating = animating;
            this.webgl = webgl;
            this.download = download;
        }
    }

    /** A hover probe: whether the frame changed, the link under the point, and the CSS cursor. */
    public static final class Hover {
        public final boolean changed;
        public final String href;
        public final String cursor;

        Hover(boolean changed, String href, String cursor) {
            this.changed = changed;
            this.href = href;
            this.cursor = cursor;
        }
    }

    /** Result of a find-in-page query: match count, the current 1-based index, and where to scroll. */
    public static final class Find {
        public final int total;
        public final int current;
        public final int scrollY;

        Find(int total, int current, int scrollY) {
            this.total = total;
            this.current = current;
            this.scrollY = scrollY;
        }
    }

    /** A clickable media element resolved at a point: its URL, whether it is video, and if it streams. */
    public static final class Media {
        public final String url;
        public final boolean video;
        public final boolean stream;

        Media(String url, boolean video, boolean stream) {
            this.url = url;
            this.video = video;
            this.stream = stream;
        }
    }

    private RendererProcess renderer;
    private String title = "";
    private String url = "";
    private String pendingNav = "";
    private int pageWidth;
    private int pageHeight;

    public RemoteBrowser() {
        this.renderer = new RendererProcess(MAX_W, MAX_H);
    }

    /** Navigate to {@code url}; returns false if the page failed to open. */
    public boolean navigate(String url, int viewportWidthCss,
                            int viewportHeightCss, int settleMs) {
        if (url == null || url.isEmpty()) {
            return false;
        }
        String body = "{\"url\":\"" + jsonEscape(url) + "\",\"width\":"
            + viewportWidthCss + ",\"height\":" + viewportHeightCss
            + ",\"settle_ms\":" + settleMs + "}";
        RendererProcess.Response resp = renderer.request("POST", "/open", body);
        String json = new String(resp.body, StandardCharsets.UTF_8);
        if (jsonInt(json, "ok", 0) != 1) {
            return false;
        }
        this.title = jsonString(json, "title");
        this.url = jsonString(json, "url");
        this.pendingNav = jsonString(json, "nav");
        this.pageWidth = jsonInt(json, "page_width", viewportWidthCss);
        this.pageHeight = jsonInt(json, "page_height", viewportHeightCss);
        return true;
    }

    /**
     * A client-side redirect (meta refresh or JS {@code location}) that fired
     * while the just-opened page settled, or null. Follow it to land on the
     * real destination — e.g. a {@code duckduckgo.com/l/?uddg=…} result link.
     */
    public String pendingNav() {
        return (pendingNav == null || pendingNav.isEmpty()) ? null : pendingNav;
    }

    /** Tell the engine the viewport changed (re-lays out, fires resize). */
    public void setViewport(int widthCss, int heightCss) {
        renderer.request("POST", "/viewport",
            "{\"width\":" + widthCss + ",\"height\":" + heightCss + "}");
    }

    /** Render a viewport region (document coordinates via scroll) to an image. */
    public Frame render(int scrollX, int scrollY, int width, int height,
                        double scale) {
        if (width <= 0 || height <= 0) {
            throw new IllegalArgumentException("width and height must be positive");
        }
        String body = "{\"width\":" + width + ",\"height\":" + height
            + ",\"scroll_x\":" + scrollX + ",\"scroll_y\":" + scrollY
            + ",\"scale\":" + formatScale(scale) + "}";
        RendererProcess.Response resp = renderer.request("POST", "/render", body);
        String nav = emptyToNull(resp.header("X-Nav"));
        String webgl = emptyToNull(resp.header("X-WebGL"));
        String download = emptyToNull(resp.header("X-Download"));
        boolean animating = "1".equals(resp.header("X-Anim"));
        boolean unchanged = "1".equals(resp.header("X-Unchanged"));
        if (unchanged || resp.body.length < 4) {
            return new Frame(null, nav, true, animating, webgl, download);
        }
        int w = headerInt(resp, "X-W", width);
        int h = headerInt(resp, "X-H", height);
        byte[] bgra = resp.body;
        BufferedImage img = new BufferedImage(w, h, BufferedImage.TYPE_INT_ARGB_PRE);
        int[] data = ((DataBufferInt) img.getRaster().getDataBuffer()).getData();
        int n = Math.min(data.length, bgra.length / 4);
        for (int i = 0, p = 0; i < n; i++, p += 4) {
            int b = bgra[p] & 0xFF;
            int g = bgra[p + 1] & 0xFF;
            int r = bgra[p + 2] & 0xFF;
            int a = bgra[p + 3] & 0xFF;
            data[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }
        return new Frame(img, nav, false, animating, webgl, download);
    }

    /** The link URL at a document-coordinate point, or null. */
    public String linkAt(int x, int y) {
        RendererProcess.Response resp = renderer.request("POST", "/link",
            "{\"x\":" + x + ",\"y\":" + y + "}");
        String href = jsonString(new String(resp.body, StandardCharsets.UTF_8), "href");
        return href.isEmpty() ? null : href;
    }

    /**
     * Probe the point under the pointer: moves the engine's hover state (firing
     * {@code :hover} / {@code mouseover}), and reports the link there and the CSS
     * cursor name so the shell can mirror the GTK status bar and pointer shape.
     */
    public Hover hover(int x, int y) {
        RendererProcess.Response resp = renderer.request("POST", "/hover",
            "{\"x\":" + x + ",\"y\":" + y + "}");
        String json = new String(resp.body, StandardCharsets.UTF_8);
        boolean changed = jsonInt(json, "changed", 0) != 0;
        String href = jsonString(json, "href");
        String cursor = jsonString(json, "cursor");
        return new Hover(changed, href.isEmpty() ? null : href,
                         cursor.isEmpty() ? null : cursor);
    }

    /**
     * Drive a text selection. {@code kind} is 0 to anchor a new selection at the
     * point, 1 to extend it, 3 to select the whole document, and 4 to return the
     * currently selected text (the copy path). Returns the selected text for
     * {@code kind == 4}, otherwise null.
     */
    public String select(int kind, int x, int y) {
        RendererProcess.Response resp = renderer.request("POST", "/select",
            "{\"kind\":" + kind + ",\"x\":" + x + ",\"y\":" + y + "}");
        String href = jsonString(new String(resp.body, StandardCharsets.UTF_8), "href");
        return href.isEmpty() ? null : href;
    }

    /**
     * Find {@code query} in the page. {@code direction} is 0 to (re)search from
     * {@code fromY}, 1 for the next match, 2 for the previous. Returns the match
     * count, the current match index, and the document Y to scroll the match into
     * view.
     */
    public Find find(String query, boolean caseSensitive, int direction, int fromY) {
        String body = "{\"query\":\"" + jsonEscape(query == null ? "" : query)
            + "\",\"case_sensitive\":" + (caseSensitive ? 1 : 0)
            + ",\"direction\":" + direction + ",\"from_y\":" + fromY + "}";
        RendererProcess.Response resp = renderer.request("POST", "/find", body);
        String json = new String(resp.body, StandardCharsets.UTF_8);
        return new Find(jsonInt(json, "total", 0), jsonInt(json, "current", 0),
                        jsonInt(json, "scroll_y", 0));
    }

    /** Evaluate JavaScript in the page and return its result rendered as text. */
    public String eval(String src) {
        String body = "{\"src\":\"" + jsonEscape(src == null ? "" : src) + "\"}";
        RendererProcess.Response resp = renderer.request("POST", "/eval", body);
        String text = jsonString(new String(resp.body, StandardCharsets.UTF_8), "text");
        return text.isEmpty() ? null : text;
    }

    /** Drain any buffered {@code console.*} output the page produced, or null. */
    public String consoleDrain() {
        RendererProcess.Response resp = renderer.request("POST", "/console", "{}");
        String text = jsonString(new String(resp.body, StandardCharsets.UTF_8), "text");
        return text.isEmpty() ? null : text;
    }

    /** The media element (audio/video URL) at a document-coordinate point, or null. */
    public Media mediaAt(int x, int y) {
        RendererProcess.Response resp = renderer.request("POST", "/media",
            "{\"x\":" + x + ",\"y\":" + y + "}");
        String json = new String(resp.body, StandardCharsets.UTF_8);
        String url = jsonString(json, "url");
        if (url.isEmpty()) {
            return null;
        }
        return new Media(url, jsonInt(json, "is_video", 0) != 0,
                         jsonInt(json, "stream", 0) != 0);
    }

    /** Render the whole page to {@code path} (a {@code .pdf} → PDF, else PNG); true on success. */
    public boolean export(String path) {
        String body = "{\"path\":\"" + jsonEscape(path) + "\"}";
        RendererProcess.Response resp = renderer.request("POST", "/export", body);
        return jsonInt(new String(resp.body, StandardCharsets.UTF_8), "ok", -1) == 0;
    }

    /** Allow or block WebGL for {@code origin} for the rest of the session. */
    public void resolveWebgl(String origin, boolean allow) {
        renderer.request("POST", "/webgl",
            "{\"origin\":\"" + jsonEscape(origin) + "\",\"allow\":" + (allow ? 1 : 0) + "}");
    }

    /** The current page's favicon as an image, or null if it has none. */
    public BufferedImage favicon() {
        RendererProcess.Response resp = renderer.request("POST", "/favicon", "{}");
        int w = headerInt(resp, "X-W", 0);
        int h = headerInt(resp, "X-H", 0);
        int stride = headerInt(resp, "X-Stride", w * 4);
        if (w <= 0 || h <= 0 || resp.body.length < stride * h) {
            return null;
        }
        byte[] bgra = resp.body;
        BufferedImage img = new BufferedImage(w, h, BufferedImage.TYPE_INT_ARGB_PRE);
        int[] data = ((DataBufferInt) img.getRaster().getDataBuffer()).getData();
        for (int y = 0; y < h; y++) {
            int row = y * stride;
            for (int x = 0; x < w; x++) {
                int p = row + x * 4;
                int b = bgra[p] & 0xFF;
                int g = bgra[p + 1] & 0xFF;
                int r = bgra[p + 2] & 0xFF;
                int a = bgra[p + 3] & 0xFF;
                data[y * w + x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
        return img;
    }

    /** Press at a document-coordinate point (then call {@link #release}). */
    public String press(int x, int y, int mods) {
        RendererProcess.Response resp = renderer.request("POST", "/click",
            "{\"x\":" + x + ",\"y\":" + y + ",\"mods\":" + mods + "}");
        String href = jsonString(new String(resp.body, StandardCharsets.UTF_8), "href");
        return href.isEmpty() ? null : href;
    }

    /** Release a pending press; returns a navigation URL if the click followed a link. */
    public String release() {
        RendererProcess.Response resp = renderer.request("POST", "/release", "{}");
        String href = jsonString(new String(resp.body, StandardCharsets.UTF_8), "href");
        return href.isEmpty() ? null : href;
    }

    /** Result of a key event: any navigation it triggered, and whether the page consumed it. */
    public static final class Key {
        /** Navigation URL the key triggered (e.g. Enter submitting a form), or null. */
        public final String nav;
        /** The page called preventDefault() — the shell should not apply its default. */
        public final boolean prevented;

        Key(String nav, boolean prevented) {
            this.nav = nav;
            this.prevented = prevented;
        }
    }

    /**
     * Forward a key event to the focused element. {@code kind} is 0 for keydown,
     * 1 for keyup, 2 to insert {@code key} as text, 3 for keypress. {@code key}
     * and {@code code} are the {@code KeyboardEvent.key}/{@code .code} values.
     */
    public Key key(int kind, String key, String code, int keycode, int mods) {
        String body = "{\"kind\":" + kind
            + ",\"key\":\"" + jsonEscape(key == null ? "" : key)
            + "\",\"code\":\"" + jsonEscape(code == null ? "" : code)
            + "\",\"keycode\":" + keycode + ",\"mods\":" + mods + "}";
        RendererProcess.Response resp = renderer.request("POST", "/key", body);
        String json = new String(resp.body, StandardCharsets.UTF_8);
        String href = jsonString(json, "href");
        boolean prevented = jsonInt(json, "prevented", 0) != 0;
        return new Key(href.isEmpty() ? null : href, prevented);
    }

    public String title() { return title; }
    public String url() { return url; }
    public int pageWidth() { return pageWidth; }
    public int pageHeight() { return pageHeight; }

    /**
     * Replace a dead renderer with a fresh process. Call after a request throws
     * (the child crashed or wedged); the caller must then re-navigate, since the
     * new process starts with no page loaded.
     */
    public void restart() {
        try { renderer.close(); } catch (RuntimeException ignored) { }
        this.renderer = new RendererProcess(MAX_W, MAX_H);
        this.title = "";
        this.url = "";
        this.pendingNav = "";
        this.pageWidth = 0;
        this.pageHeight = 0;
    }

    @Override
    public void close() {
        renderer.close();
    }

    private static String emptyToNull(String s) {
        return (s == null || s.isEmpty()) ? null : s;
    }

    private static int headerInt(RendererProcess.Response resp, String name, int fb) {
        String v = resp.header(name);
        if (v == null) return fb;
        try { return Integer.parseInt(v.trim()); } catch (NumberFormatException e) { return fb; }
    }

    private static String formatScale(double scale) {
        if (!(scale > 0)) scale = 1.0;
        int milli = (int) (scale * 1000.0 + 0.5);
        return (milli / 1000) + "." + String.format("%03d", milli % 1000);
    }

    private static String jsonEscape(String s) {
        StringBuilder sb = new StringBuilder(s.length() + 8);
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '"': sb.append("\\\""); break;
                case '\\': sb.append("\\\\"); break;
                case '\n': sb.append("\\n"); break;
                case '\r': sb.append("\\r"); break;
                case '\t': sb.append("\\t"); break;
                default:
                    if (c < 0x20) sb.append(String.format("\\u%04x", (int) c));
                    else sb.append(c);
            }
        }
        return sb.toString();
    }

    private static int jsonInt(String json, String key, int fallback) {
        String needle = "\"" + key + "\":";
        int i = json.indexOf(needle);
        if (i < 0) return fallback;
        i += needle.length();
        int j = i;
        while (j < json.length()
               && (Character.isDigit(json.charAt(j)) || json.charAt(j) == '-')) j++;
        if (j == i) return fallback;
        try { return Integer.parseInt(json.substring(i, j)); }
        catch (NumberFormatException e) { return fallback; }
    }

    private static String jsonString(String json, String key) {
        String needle = "\"" + key + "\":\"";
        int i = json.indexOf(needle);
        if (i < 0) return "";
        i += needle.length();
        StringBuilder sb = new StringBuilder();
        for (int j = i; j < json.length(); j++) {
            char c = json.charAt(j);
            if (c == '\\' && j + 1 < json.length()) {
                char nx = json.charAt(++j);
                switch (nx) {
                    case 'n': sb.append('\n'); break;
                    case 'r': sb.append('\r'); break;
                    case 't': sb.append('\t'); break;
                    case '"': sb.append('"'); break;
                    case '\\': sb.append('\\'); break;
                    case '/': sb.append('/'); break;
                    case 'u':
                        if (j + 4 < json.length()) {
                            sb.append((char) Integer.parseInt(json.substring(j + 1, j + 5), 16));
                            j += 4;
                        }
                        break;
                    default: sb.append(nx);
                }
            } else if (c == '"') {
                break;
            } else {
                sb.append(c);
            }
        }
        return sb.toString();
    }
}
