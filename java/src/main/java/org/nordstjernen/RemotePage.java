/* Nordstjernen — a page rendered by a separate renderer process.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

package org.nordstjernen;

import java.awt.image.BufferedImage;
import java.awt.image.DataBufferInt;
import java.nio.charset.StandardCharsets;

/**
 * A web page driven through a separate {@code nordstjernen-renderer} process,
 * mirroring the in-process {@link Page} API without loading the native engine
 * into the JVM. The engine runs in the child process; this class is a thin
 * client of its HTTP/JSON control protocol.
 *
 * <pre>{@code
 * try (RemotePage page = RemotePage.open("https://example.com", 1000, 700, 800)) {
 *     System.out.println(page.title());
 *     ImageIO.write(page.renderFullPage(1.0), "png", new File("out.png"));
 * }
 * }</pre>
 *
 * <p>Not thread-safe.
 */
public final class RemotePage implements AutoCloseable {

    private static final int MAX_W = 2560;
    private static final int MAX_H = 4096;

    private final RendererProcess renderer;
    private final String title;
    private final String finalUrl;
    private final int pageWidth;
    private final int pageHeight;

    private RemotePage(RendererProcess renderer, String title, String finalUrl,
                       int pageWidth, int pageHeight) {
        this.renderer = renderer;
        this.title = title;
        this.finalUrl = finalUrl;
        this.pageWidth = pageWidth;
        this.pageHeight = pageHeight;
    }

    /**
     * Open {@code url} in a fresh renderer process at the given CSS viewport,
     * letting scripts settle for {@code settleMs} before reading the result.
     */
    public static RemotePage open(String url, int viewportWidthCss,
                                  int viewportHeightCss, int settleMs) {
        if (url == null || url.isEmpty()) {
            throw new IllegalArgumentException("url must not be empty");
        }
        RendererProcess r = new RendererProcess(MAX_W, MAX_H);
        boolean ok = false;
        try {
            String body = "{\"url\":\"" + jsonEscape(url) + "\",\"width\":"
                + viewportWidthCss + ",\"height\":" + viewportHeightCss
                + ",\"settle_ms\":" + settleMs + "}";
            RendererProcess.Response resp = r.request("POST", "/open", body);
            String json = new String(resp.body, StandardCharsets.UTF_8);
            if (jsonInt(json, "ok", 0) != 1) {
                throw new NordstjernenException("failed to open " + url);
            }
            RemotePage page = new RemotePage(r,
                jsonString(json, "title"), jsonString(json, "url"),
                jsonInt(json, "page_width", viewportWidthCss),
                jsonInt(json, "page_height", viewportHeightCss));
            ok = true;
            return page;
        } finally {
            if (!ok) {
                r.close();
            }
        }
    }

    /** The page {@code <title>}. */
    public String title() {
        return title;
    }

    /** The final URL after redirects. */
    public String url() {
        return finalUrl;
    }

    /** The laid-out page size in CSS pixels. */
    public Size pageSize() {
        return new Size(pageWidth, pageHeight);
    }

    /**
     * Render a viewport region to premultiplied RGBA8888 bytes (row-major,
     * 4 bytes per pixel, R,G,B,A).
     */
    public byte[] renderRgba(int scrollX, int scrollY, int width, int height,
                             double scale) {
        if (width <= 0 || height <= 0) {
            throw new IllegalArgumentException("width and height must be positive");
        }
        String body = "{\"width\":" + width + ",\"height\":" + height
            + ",\"scroll_x\":" + scrollX + ",\"scroll_y\":" + scrollY
            + ",\"scale\":" + formatScale(scale) + "}";
        RendererProcess.Response resp = renderer.request("POST", "/render", body);
        int w = headerInt(resp, "X-W", width);
        int h = headerInt(resp, "X-H", height);
        byte[] bgra = resp.body;
        // The renderer returns cairo ARGB32 (little-endian B,G,R,A). Convert
        // to the R,G,B,A byte order the in-process Page also exposes.
        int px = Math.min(w * h, bgra.length / 4);
        byte[] out = new byte[w * h * 4];
        for (int i = 0, p = 0; i < px; i++, p += 4) {
            out[p]     = bgra[p + 2];
            out[p + 1] = bgra[p + 1];
            out[p + 2] = bgra[p];
            out[p + 3] = bgra[p + 3];
        }
        return out;
    }

    /** Render a viewport region to a premultiplied-ARGB image. */
    public BufferedImage render(int scrollX, int scrollY, int width, int height,
                                double scale) {
        if (width <= 0 || height <= 0) {
            throw new IllegalArgumentException("width and height must be positive");
        }
        String body = "{\"width\":" + width + ",\"height\":" + height
            + ",\"scroll_x\":" + scrollX + ",\"scroll_y\":" + scrollY
            + ",\"scale\":" + formatScale(scale) + "}";
        RendererProcess.Response resp = renderer.request("POST", "/render", body);
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
        return img;
    }

    /** Render the whole page (top to bottom, full width) at {@code scale}. */
    public BufferedImage renderFullPage(double scale) {
        int width = clampPx(Math.round(pageWidth * scale), MAX_W);
        int height = clampPx(Math.round(pageHeight * scale), MAX_H);
        return render(0, 0, width, height, scale);
    }

    @Override
    public void close() {
        renderer.close();
    }

    private static int clampPx(long px, int max) {
        if (px < 1) {
            return 1;
        }
        return (int) Math.min(px, max);
    }

    private static int headerInt(RendererProcess.Response resp, String name,
                                 int fallback) {
        String v = resp.header(name);
        if (v == null) {
            return fallback;
        }
        try {
            return Integer.parseInt(v.trim());
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    private static String formatScale(double scale) {
        if (!(scale > 0)) {
            scale = 1.0;
        }
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
                    if (c < 0x20) {
                        sb.append(String.format("\\u%04x", (int) c));
                    } else {
                        sb.append(c);
                    }
            }
        }
        return sb.toString();
    }

    private static int jsonInt(String json, String key, int fallback) {
        String needle = "\"" + key + "\":";
        int i = json.indexOf(needle);
        if (i < 0) {
            return fallback;
        }
        i += needle.length();
        int j = i;
        while (j < json.length()
               && (Character.isDigit(json.charAt(j)) || json.charAt(j) == '-')) {
            j++;
        }
        if (j == i) {
            return fallback;
        }
        try {
            return Integer.parseInt(json.substring(i, j));
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    private static String jsonString(String json, String key) {
        String needle = "\"" + key + "\":\"";
        int i = json.indexOf(needle);
        if (i < 0) {
            return "";
        }
        i += needle.length();
        StringBuilder sb = new StringBuilder();
        for (int j = i; j < json.length(); j++) {
            char c = json.charAt(j);
            if (c == '\\' && j + 1 < json.length()) {
                char n = json.charAt(++j);
                switch (n) {
                    case 'n': sb.append('\n'); break;
                    case 'r': sb.append('\r'); break;
                    case 't': sb.append('\t'); break;
                    case '"': sb.append('"'); break;
                    case '\\': sb.append('\\'); break;
                    case '/': sb.append('/'); break;
                    case 'u':
                        if (j + 4 < json.length()) {
                            sb.append((char) Integer.parseInt(
                                json.substring(j + 1, j + 5), 16));
                            j += 4;
                        }
                        break;
                    default: sb.append(n);
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
