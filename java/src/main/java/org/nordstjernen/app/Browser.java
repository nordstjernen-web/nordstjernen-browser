/* Nordstjernen — official Java browser (Swing UI over the renderer process).
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

package org.nordstjernen.app;

import org.nordstjernen.RemoteBrowser;

import javax.swing.*;
import java.awt.*;
import java.awt.event.FocusAdapter;
import java.awt.event.FocusEvent;
import java.awt.image.BufferedImage;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * A small, standalone Java browser with a GTK-shell-style chrome — back,
 * forward, reload, home, a URL bar and a status bar — that drives a separate
 * {@code nordstjernen-renderer} process through {@link RemoteBrowser}. No
 * native engine is loaded into the JVM; rendering, layout and scripting all
 * run in the renderer.
 *
 * <p>Point at the renderer binary with {@code -Dnordstjernen.renderer=…} or the
 * {@code NORDSTJERNEN_RENDERER} environment variable.
 */
public final class Browser {

    private static final int SETTLE_MS = 900;

    private final RemoteBrowser engine = new RemoteBrowser();
    private final ExecutorService io = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "ns-engine");
        t.setDaemon(true);
        return t;
    });

    private final JFrame frame = new JFrame("Nordstjernen");
    private final JButton back = navButton("back", "◀", "Back");
    private final JButton forward = navButton("forward", "▶", "Forward");
    private final JButton reload = navButton("reload", "↻", "Reload");
    private final JButton home = navButton("home", "⌂", "Home");
    private final JTextField address = new JTextField();
    private final RenderCanvas canvas = new RenderCanvas();
    private final JLabel status = new JLabel(" ");

    private final List<String> history = new ArrayList<>();
    private int historyIndex = -1;
    private final String homeUrl;

    private int scrollY = 0;
    private boolean loading = false;

    private Browser(String startUrl) {
        this.homeUrl = startUrl;
        buildUi();
        navigate(startUrl, true);
    }

    public static void main(String[] args) {
        try {
            UIManager.setLookAndFeel(UIManager.getSystemLookAndFeelClassName());
        } catch (Exception ignored) { }
        String start = args.length > 0 ? args[0] : "https://example.com";
        SwingUtilities.invokeLater(() -> new Browser(start));
    }

    private void buildUi() {
        frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        frame.setSize(1100, 820);

        JToolBar bar = new JToolBar();
        bar.setFloatable(false);
        for (JButton b : new JButton[]{back, forward, reload, home}) {
            b.setFocusable(false);
            bar.add(b);
        }
        bar.add(address);
        JButton go = navButton("go", "Go", "Go");
        bar.add(go);

        back.addActionListener(e -> goBack());
        forward.addActionListener(e -> goForward());
        reload.addActionListener(e -> { if (currentUrl() != null) navigate(currentUrl(), false); });
        home.addActionListener(e -> navigate(homeUrl, true));
        go.addActionListener(e -> navigate(normalize(address.getText()), true));
        address.addActionListener(e -> navigate(normalize(address.getText()), true));
        address.addFocusListener(new FocusAdapter() {
            @Override public void focusGained(FocusEvent e) { address.selectAll(); }
        });

        frame.add(bar, BorderLayout.NORTH);
        frame.add(canvas, BorderLayout.CENTER);
        frame.add(status, BorderLayout.SOUTH);

        canvas.addMouseWheelListener(e -> {
            int max = Math.max(0, engine.pageHeight() - canvas.getHeight());
            scrollY = Math.max(0, Math.min(max, scrollY + e.getWheelRotation() * 80));
            renderViewport();
        });
        canvas.addMouseListener(new java.awt.event.MouseAdapter() {
            @Override public void mouseClicked(java.awt.event.MouseEvent e) {
                onCanvasClick(e.getX(), e.getY());
            }
        });
        frame.addComponentListener(new java.awt.event.ComponentAdapter() {
            @Override public void componentResized(java.awt.event.ComponentEvent e) {
                if (!loading && currentUrl() != null) {
                    io.submit(() -> {
                        engine.setViewport(canvas.getWidth(),
                                           Math.max(1, canvas.getHeight()));
                        renderViewport();
                    });
                }
            }
        });

        frame.setVisible(true);
        Runtime.getRuntime().addShutdownHook(new Thread(engine::close));
    }

    private String currentUrl() {
        return historyIndex >= 0 ? history.get(historyIndex) : null;
    }

    private void navigate(String url, boolean record) {
        if (url == null || url.isEmpty() || loading) {
            return;
        }
        loading = true;
        setStatus("Loading " + url + " …");
        int vw = Math.max(320, canvas.getWidth() > 0 ? canvas.getWidth() : 1000);
        int vh = Math.max(240, canvas.getHeight() > 0 ? canvas.getHeight() : 700);
        io.submit(() -> {
            boolean ok = engine.navigate(url, vw, vh, SETTLE_MS);
            String finalUrl = ok ? engine.url() : url;
            String title = ok ? engine.title() : "";
            scrollY = 0;
            RemoteBrowser.Frame frm = ok ? engine.render(0, 0, vw, vh, 1.0) : null;
            SwingUtilities.invokeLater(() -> {
                loading = false;
                if (!ok) {
                    setStatus("Failed to load " + url);
                    return;
                }
                if (record) {
                    while (history.size() > historyIndex + 1) {
                        history.remove(history.size() - 1);
                    }
                    history.add(finalUrl);
                    historyIndex = history.size() - 1;
                }
                address.setText(finalUrl);
                frame.setTitle((title.isEmpty() ? "Untitled" : title)
                    + " — Nordstjernen");
                if (frm != null) canvas.setImage(frm.image);
                updateNavButtons();
                setStatus(title);
                if (frm != null && frm.nav != null) navigate(frm.nav, true);
            });
        });
    }

    private void renderViewport() {
        int vw = Math.max(1, canvas.getWidth());
        int vh = Math.max(1, canvas.getHeight());
        io.submit(() -> {
            RemoteBrowser.Frame frm = engine.render(0, scrollY, vw, vh, 1.0);
            SwingUtilities.invokeLater(() -> {
                canvas.setImage(frm.image);
                if (frm.nav != null) navigate(frm.nav, true);
            });
        });
    }

    private void onCanvasClick(int cx, int cy) {
        int docX = cx;
        int docY = scrollY + cy;
        io.submit(() -> {
            String href = engine.linkAt(docX, docY);
            if (href != null && !href.isEmpty()) {
                SwingUtilities.invokeLater(() -> navigate(href, true));
            }
        });
    }

    private void goBack() {
        if (historyIndex > 0) {
            historyIndex--;
            navigate(history.get(historyIndex), false);
        }
    }

    private void goForward() {
        if (historyIndex < history.size() - 1) {
            historyIndex++;
            navigate(history.get(historyIndex), false);
        }
    }

    private void updateNavButtons() {
        back.setEnabled(historyIndex > 0);
        forward.setEnabled(historyIndex < history.size() - 1);
    }

    private void setStatus(String s) {
        status.setText(s == null || s.isEmpty() ? " " : s);
    }

    private static ImageIcon icon(String name) {
        java.net.URL u = Browser.class.getResource("icons/" + name + ".png");
        return u != null ? new ImageIcon(u) : null;
    }

    private static JButton navButton(String iconName, String fallbackText,
                                     String tooltip) {
        ImageIcon ic = icon(iconName);
        JButton b = ic != null ? new JButton(ic) : new JButton(fallbackText);
        b.setToolTipText(tooltip);
        b.setFocusable(false);
        return b;
    }

    private static String normalize(String input) {
        String s = input.trim();
        if (s.isEmpty()) return s;
        if (s.contains("://") || s.startsWith("about:") || s.startsWith("data:")) {
            return s;
        }
        if (s.contains(".") && !s.contains(" ")) {
            return "https://" + s;
        }
        return "https://duckduckgo.com/?q="
            + java.net.URLEncoder.encode(s, java.nio.charset.StandardCharsets.UTF_8);
    }

    private static final class RenderCanvas extends JComponent {
        private BufferedImage image;

        void setImage(BufferedImage img) {
            this.image = img;
            repaint();
        }

        @Override
        protected void paintComponent(Graphics g) {
            g.setColor(Color.WHITE);
            g.fillRect(0, 0, getWidth(), getHeight());
            if (image != null) {
                g.drawImage(image, 0, 0, null);
            }
        }
    }
}
