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
    private javax.swing.Timer refreshTimer;
    private boolean renderBusy = false;
    private int stableFrames = 0;

    private Browser(String startUrl) {
        this.homeUrl = startUrl;
        buildUi();
        navigate(startUrl, true);
    }

    public static void main(String[] args) {
        try {
            UIManager.setLookAndFeel(UIManager.getSystemLookAndFeelClassName());
        } catch (Exception ignored) { }
        String start = args.length > 0 ? args[0] : "about:start";
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
        ImageIcon logo = icon("logo");
        if (logo != null) {
            bar.addSeparator();
            JLabel brand = new JLabel(logo);
            brand.setBorder(BorderFactory.createEmptyBorder(0, 6, 0, 6));
            brand.setToolTipText("Nordstjernen");
            bar.add(brand);
            frame.setIconImage(logo.getImage());
        }

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
            scheduleRefresh();
        });
        canvas.addMouseListener(new java.awt.event.MouseAdapter() {
            @Override public void mouseClicked(java.awt.event.MouseEvent e) {
                onCanvasClick(e.getX(), e.getY());
            }
        });
        frame.addComponentListener(new java.awt.event.ComponentAdapter() {
            @Override public void componentResized(java.awt.event.ComponentEvent e) {
                if (!loading && currentUrl() != null) {
                    int w = Math.max(1, canvas.getWidth());
                    int h = Math.max(1, canvas.getHeight());
                    io.submit(() -> {
                        engine.setViewport(w, h);
                        SwingUtilities.invokeLater(() -> scheduleRefresh());
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
                updateNavButtons();
                setStatus(title);
                scheduleRefresh();
            });
        });
    }

    /**
     * Keep re-rendering the current viewport until the page settles. Async
     * image loads, late layout, and animations all land after the first
     * render, so (like the GTK shell) we render repeatedly until the renderer
     * reports the frame unchanged and not animating.
     */
    private void scheduleRefresh() {
        stableFrames = 0;
        if (refreshTimer == null) {
            refreshTimer = new javax.swing.Timer(120, e -> tickRefresh());
        }
        if (!refreshTimer.isRunning()) {
            refreshTimer.start();
        }
    }

    private void tickRefresh() {
        if (renderBusy) {
            return;
        }
        renderBusy = true;
        final int vw = Math.max(1, canvas.getWidth());
        final int vh = Math.max(1, canvas.getHeight());
        final int sy = scrollY;
        io.submit(() -> {
            RemoteBrowser.Frame frm;
            try {
                frm = engine.render(0, sy, vw, vh, 1.0);
            } catch (RuntimeException ex) {
                SwingUtilities.invokeLater(() -> { renderBusy = false; });
                return;
            }
            final RemoteBrowser.Frame f = frm;
            SwingUtilities.invokeLater(() -> {
                renderBusy = false;
                if (f.image != null) {
                    canvas.setImage(f.image);
                }
                if (f.unchanged && !f.animating) {
                    if (++stableFrames >= 3 && refreshTimer != null) {
                        refreshTimer.stop();
                    }
                } else {
                    stableFrames = 0;
                }
                if (f.nav != null) {
                    navigate(f.nav, true);
                }
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
