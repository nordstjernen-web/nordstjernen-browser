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
 * render viewports, and follow links — the model the GTK and Qt shells use.
 * No native engine is loaded into the JVM.
 *
 * <p>Not thread-safe; drive one instance from a single thread (or serialise).
 */
public final class RemoteBrowser implements AutoCloseable {

    /** Maximum framebuffer the renderer is asked to allocate. */
    public static final int MAX_W = 2560;
    public static final int MAX_H = 1600;

    /** Result of a render: the image plus any navigation the page requested. */
    public static final class Frame {
        /** The rendered image, or null when {@link #unchanged} (reuse the prior one). */
        public final BufferedImage image;
        public final String nav;
        /** The renderer reported nothing changed since the last render. */
        public final boolean unchanged;
        /** The page is still animating / loading; keep rendering. */
        public final boolean animating;

        Frame(BufferedImage image, String nav, boolean unchanged, boolean animating) {
            this.image = image;
            this.nav = nav;
            this.unchanged = unchanged;
            this.animating = animating;
        }
    }

    private final RendererProcess renderer;
    private String title = "";
    private String url = "";
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
        this.pageWidth = jsonInt(json, "page_width", viewportWidthCss);
        this.pageHeight = jsonInt(json, "page_height", viewportHeightCss);
        return true;
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
        String nav = resp.header("X-Nav");
        if (nav != null && nav.isEmpty()) {
            nav = null;
        }
        boolean animating = "1".equals(resp.header("X-Anim"));
        boolean unchanged = "1".equals(resp.header("X-Unchanged"));
        if (unchanged || resp.body.length < 4) {
            return new Frame(null, nav, true, animating);
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
        return new Frame(img, nav, false, animating);
    }

    /** The link URL at a document-coordinate point, or null. */
    public String linkAt(int x, int y) {
        RendererProcess.Response resp = renderer.request("POST", "/link",
            "{\"x\":" + x + ",\"y\":" + y + "}");
        String href = jsonString(new String(resp.body, StandardCharsets.UTF_8), "href");
        return href.isEmpty() ? null : href;
    }

    /** Press at a document-coordinate point (then call {@link #release}). */
    public String press(int x, int y, int mods) {
        RendererProcess.Response resp = renderer.request("POST", "/click",
            "{\"x\":" + x + ",\"y\":" + y + ",\"mods\":" + mods + "}");
        String href = jsonString(new String(resp.body, StandardCharsets.UTF_8), "href");
        return href.isEmpty() ? null : href;
    }

    /** Release a pending press. */
    public void release() {
        renderer.request("POST", "/release", "");
    }

    public String title() { return title; }
    public String url() { return url; }
    public int pageWidth() { return pageWidth; }
    public int pageHeight() { return pageHeight; }

    @Override
    public void close() {
        renderer.close();
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
