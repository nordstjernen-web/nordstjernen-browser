/* Nordstjernen — official Java browser (Swing UI over the renderer process).
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

package org.nordstjernen.app;

import org.nordstjernen.RemoteBrowser;

import javax.swing.*;
import java.awt.*;
import java.awt.event.ActionEvent;
import java.awt.event.FocusAdapter;
import java.awt.event.FocusEvent;
import java.awt.image.BufferedImage;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * A small, standalone Java web browser with a GTK-shell-style chrome — back,
 * forward, reload, home, a URL bar and a status bar — that drives a separate
 * {@code nordstjernen-renderer} process through {@link RemoteBrowser}. No
 * native engine is loaded into the JVM; rendering, layout and scripting all
 * run in the renderer.
 *
 * <p>Keyboard: {@code Alt+Left}/{@code Alt+Right} navigate history,
 * {@code Ctrl+L} or {@code Alt+D} focuses the URL bar, {@code Ctrl+R}/{@code F5}
 * reloads, {@code Alt+Home} goes home, {@code Ctrl+W}/{@code Ctrl+Q} quit; with
 * the page focused, the arrow keys, {@code PageUp}/{@code PageDown},
 * {@code Home}/{@code End} and {@code Space} scroll. The mouse back/forward
 * buttons navigate history.
 *
 * <p>Point at the renderer binary with {@code -Dnordstjernen.renderer=…} or the
 * {@code NORDSTJERNEN_RENDERER} environment variable.
 */
public final class Browser {

    private static final int SETTLE_MS = 900;
    private static final int LINE_SCROLL = 60;
    private static final String VERSION = resolveVersion();
    private static final String TITLE_SUFFIX = " (Java " + VERSION + ")";

    private final RemoteBrowser engine = new RemoteBrowser();
    private final ExecutorService io = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "ns-engine");
        t.setDaemon(true);
        return t;
    });

    private final JFrame frame = new JFrame("Nordstjernen" + TITLE_SUFFIX);
    private final JButton back = navButton("back", "◀", "Back (Alt+Left)");
    private final JButton forward = navButton("forward", "▶", "Forward (Alt+Right)");
    private final JButton reload = navButton("reload", "↻", "Reload (Ctrl+R)");
    private final JButton home = navButton("home", "⌂", "Home (Alt+Home)");
    private final JTextField address = new JTextField();
    private final RenderCanvas canvas = new RenderCanvas();
    private final JScrollBar vScroll = new JScrollBar(JScrollBar.VERTICAL);
    private final JLabel status = new JLabel(" ");

    private final List<String> history = new ArrayList<>();
    private int historyIndex = -1;
    private final String homeUrl;

    private static final int MAX_JS_REDIRECTS = 10;

    private int scrollY = 0;
    private boolean loading = false;
    private int jsRedirects = 0;
    private boolean syncingScrollbar = false;
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
        reload.addActionListener(e -> reloadPage());
        home.addActionListener(e -> navigate(homeUrl, true));
        go.addActionListener(e -> navigate(normalize(address.getText()), true));
        address.addActionListener(e -> navigate(normalize(address.getText()), true));
        address.addFocusListener(new FocusAdapter() {
            @Override public void focusGained(FocusEvent e) { address.selectAll(); }
        });

        JPanel content = new JPanel(new BorderLayout());
        content.add(canvas, BorderLayout.CENTER);
        content.add(vScroll, BorderLayout.EAST);

        frame.add(bar, BorderLayout.NORTH);
        frame.add(content, BorderLayout.CENTER);
        frame.add(status, BorderLayout.SOUTH);

        vScroll.setUnitIncrement(LINE_SCROLL);
        vScroll.addAdjustmentListener(e -> {
            if (!syncingScrollbar) {
                setScrollY(e.getValue());
            }
        });

        canvas.setFocusable(true);
        canvas.setFocusTraversalKeysEnabled(false);
        canvas.addMouseWheelListener(e ->
            setScrollY(scrollY + e.getWheelRotation() * LINE_SCROLL));
        canvas.addMouseListener(new java.awt.event.MouseAdapter() {
            @Override public void mousePressed(java.awt.event.MouseEvent e) {
                canvas.requestFocusInWindow();
                if (e.getButton() == 4) { goBack(); }
                else if (e.getButton() == 5) { goForward(); }
            }
            @Override public void mouseClicked(java.awt.event.MouseEvent e) {
                if (e.getButton() == java.awt.event.MouseEvent.BUTTON1) {
                    onCanvasClick(e.getX(), e.getY());
                }
            }
        });
        canvas.addKeyListener(new java.awt.event.KeyAdapter() {
            @Override public void keyPressed(java.awt.event.KeyEvent e) { onCanvasKeyPressed(e); }
            @Override public void keyReleased(java.awt.event.KeyEvent e) { onCanvasKeyReleased(e); }
            @Override public void keyTyped(java.awt.event.KeyEvent e) { onCanvasKeyTyped(e); }
        });
        frame.addComponentListener(new java.awt.event.ComponentAdapter() {
            @Override public void componentResized(java.awt.event.ComponentEvent e) {
                if (!loading && currentUrl() != null) {
                    int w = Math.max(1, canvas.getWidth());
                    int h = Math.max(1, canvas.getHeight());
                    io.submit(() -> {
                        engine.setViewport(w, h);
                        SwingUtilities.invokeLater(() -> {
                            updateScrollModel();
                            scheduleRefresh();
                        });
                    });
                }
            }
        });

        installShortcuts();

        frame.setVisible(true);
        Runtime.getRuntime().addShutdownHook(new Thread(engine::close));
    }

    private void installShortcuts() {
        JComponent root = frame.getRootPane();
        bindWindow(root, "alt LEFT", "back", this::goBack);
        bindWindow(root, "alt RIGHT", "forward", this::goForward);
        bindWindow(root, "F5", "reload", this::reloadPage);
        bindWindow(root, "control R", "reload2", this::reloadPage);
        bindWindow(root, "alt HOME", "home", () -> navigate(homeUrl, true));
        bindWindow(root, "control L", "focusUrl", this::focusAddress);
        bindWindow(root, "alt D", "focusUrl2", this::focusAddress);
        bindWindow(root, "control W", "close", () -> frame.dispose());
        bindWindow(root, "control Q", "quit", () -> frame.dispose());
    }

    private void bindWindow(JComponent c, String ks, String name, Runnable action) {
        c.getInputMap(JComponent.WHEN_IN_FOCUSED_WINDOW)
            .put(KeyStroke.getKeyStroke(ks), name);
        c.getActionMap().put(name, asAction(action));
    }

    private static AbstractAction asAction(Runnable action) {
        return new AbstractAction() {
            @Override public void actionPerformed(ActionEvent e) { action.run(); }
        };
    }

    private void focusAddress() {
        address.requestFocusInWindow();
        address.selectAll();
    }

    private void reloadPage() {
        if (currentUrl() != null) {
            navigate(currentUrl(), false);
        }
    }

    private int pageStep() {
        return Math.max(LINE_SCROLL, canvas.getHeight() - LINE_SCROLL);
    }

    private int maxScroll() {
        return Math.max(0, engine.pageHeight() - Math.max(1, canvas.getHeight()));
    }

    private void setScrollY(int y) {
        int clamped = Math.max(0, Math.min(maxScroll(), y));
        if (clamped == scrollY) {
            syncScrollbar();
            return;
        }
        scrollY = clamped;
        syncScrollbar();
        scheduleRefresh();
    }

    private void syncScrollbar() {
        syncingScrollbar = true;
        if (vScroll.getValue() != scrollY) {
            vScroll.setValue(scrollY);
        }
        syncingScrollbar = false;
    }

    private void updateScrollModel() {
        int viewport = Math.max(1, canvas.getHeight());
        int extent = viewport;
        int max = Math.max(viewport, engine.pageHeight());
        scrollY = Math.min(scrollY, Math.max(0, max - viewport));
        syncingScrollbar = true;
        vScroll.setValues(scrollY, extent, 0, max);
        vScroll.setBlockIncrement(pageStep());
        vScroll.setEnabled(max > viewport);
        syncingScrollbar = false;
    }

    private String currentUrl() {
        return historyIndex >= 0 ? history.get(historyIndex) : null;
    }

    private void navigate(String url, boolean record) {
        navigate(url, record, false);
    }

    private void navigate(String url, boolean record, boolean isRedirect) {
        if (url == null || url.isEmpty() || loading) {
            return;
        }
        if (!isRedirect) {
            jsRedirects = 0;
        }
        loading = true;
        canvas.setCursor(Cursor.getPredefinedCursor(Cursor.WAIT_CURSOR));
        setStatus("Loading " + url + " …");
        int vw = Math.max(320, canvas.getWidth() > 0 ? canvas.getWidth() : 1000);
        int vh = Math.max(240, canvas.getHeight() > 0 ? canvas.getHeight() : 700);
        io.submit(() -> {
            boolean ok = engine.navigate(url, vw, vh, SETTLE_MS);
            String finalUrl = ok ? engine.url() : url;
            String title = ok ? engine.title() : "";
            String redirect = ok ? engine.pendingNav() : null;
            scrollY = 0;
            SwingUtilities.invokeLater(() -> {
                loading = false;
                canvas.setCursor(Cursor.getDefaultCursor());
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
                } else if (isRedirect && historyIndex >= 0) {
                    history.set(historyIndex, finalUrl);
                }
                address.setText(finalUrl);
                frame.setTitle((title.isEmpty() ? "Untitled" : title)
                    + " — Nordstjernen" + TITLE_SUFFIX);
                updateNavButtons();
                updateScrollModel();
                setStatus(title);
                canvas.requestFocusInWindow();
                scheduleRefresh();
                if (redirect != null && jsRedirects < MAX_JS_REDIRECTS) {
                    jsRedirects++;
                    navigate(redirect, false, true);
                }
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
                updateScrollModel();
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
        final int docX = cx;
        final int docY = scrollY + cy;
        io.submit(() -> {
            String pressed = engine.press(docX, docY, 0);
            String released = engine.release();
            String nav = pressed != null ? pressed : released;
            SwingUtilities.invokeLater(() -> {
                if (nav != null) {
                    navigate(nav, true);
                } else {
                    scheduleRefresh();
                }
            });
        });
    }

    private void onCanvasKeyTyped(java.awt.event.KeyEvent e) {
        char c = e.getKeyChar();
        if (c == java.awt.event.KeyEvent.CHAR_UNDEFINED || Character.isISOControl(c)) {
            return;
        }
        if (e.isControlDown() || e.isAltDown() || e.isMetaDown()) {
            return;
        }
        final String s = String.valueOf(c);
        final int mods = e.isShiftDown() ? 1 : 0;
        io.submit(() -> {
            engine.key(3, s, "", 0, mods);
            RemoteBrowser.Key res = engine.key(2, s, "", 0, 0);
            SwingUtilities.invokeLater(() -> {
                if (res.nav != null) {
                    navigate(res.nav, true);
                } else {
                    scheduleRefresh();
                }
            });
        });
    }

    private void onCanvasKeyPressed(java.awt.event.KeyEvent e) {
        String name = jsKeyName(e.getKeyCode());
        if (name == null) {
            String pk = printableKey(e);
            if (pk != null) {
                final String fpk = pk;
                final String fcode = printableCode(e);
                final int fkc = e.getKeyCode();
                final int fmods = swingMods(e);
                io.submit(() -> engine.key(0, fpk, fcode, fkc, fmods));
            }
            return;
        }
        e.consume();
        final String fname = name;
        final int fcode = jsKeycode(name);
        final int fmods = swingMods(e);
        io.submit(() -> {
            RemoteBrowser.Key res = engine.key(0, fname, fname, fcode, fmods);
            SwingUtilities.invokeLater(() -> {
                if (res.nav != null) {
                    navigate(res.nav, true);
                    return;
                }
                if (!res.prevented) {
                    switch (fname) {
                        case "ArrowDown": setScrollY(scrollY + LINE_SCROLL); return;
                        case "ArrowUp":   setScrollY(scrollY - LINE_SCROLL); return;
                        case "PageDown":  setScrollY(scrollY + pageStep()); return;
                        case "PageUp":    setScrollY(scrollY - pageStep()); return;
                        default: break;
                    }
                }
                scheduleRefresh();
            });
        });
    }

    private void onCanvasKeyReleased(java.awt.event.KeyEvent e) {
        String name = jsKeyName(e.getKeyCode());
        if (name == null) {
            return;
        }
        final String fname = name;
        final int fcode = jsKeycode(name);
        final int fmods = swingMods(e);
        io.submit(() -> engine.key(1, fname, fname, fcode, fmods));
    }

    private static int swingMods(java.awt.event.KeyEvent e) {
        return (e.isShiftDown() ? 1 : 0) | (e.isControlDown() ? 2 : 0)
             | (e.isAltDown() ? 4 : 0) | (e.isMetaDown() ? 8 : 0);
    }

    private static String jsKeyName(int vk) {
        switch (vk) {
            case java.awt.event.KeyEvent.VK_BACK_SPACE: return "Backspace";
            case java.awt.event.KeyEvent.VK_DELETE:     return "Delete";
            case java.awt.event.KeyEvent.VK_ENTER:      return "Enter";
            case java.awt.event.KeyEvent.VK_TAB:        return "Tab";
            case java.awt.event.KeyEvent.VK_ESCAPE:     return "Escape";
            case java.awt.event.KeyEvent.VK_LEFT:       return "ArrowLeft";
            case java.awt.event.KeyEvent.VK_RIGHT:      return "ArrowRight";
            case java.awt.event.KeyEvent.VK_UP:         return "ArrowUp";
            case java.awt.event.KeyEvent.VK_DOWN:       return "ArrowDown";
            case java.awt.event.KeyEvent.VK_HOME:       return "Home";
            case java.awt.event.KeyEvent.VK_END:        return "End";
            case java.awt.event.KeyEvent.VK_PAGE_UP:    return "PageUp";
            case java.awt.event.KeyEvent.VK_PAGE_DOWN:  return "PageDown";
            default: return null;
        }
    }

    private static String printableKey(java.awt.event.KeyEvent e) {
        int vk = e.getKeyCode();
        if (vk >= java.awt.event.KeyEvent.VK_A && vk <= java.awt.event.KeyEvent.VK_Z) {
            char c = (char) ('a' + (vk - java.awt.event.KeyEvent.VK_A));
            return e.isShiftDown() ? String.valueOf(Character.toUpperCase(c)) : String.valueOf(c);
        }
        if (vk >= java.awt.event.KeyEvent.VK_0 && vk <= java.awt.event.KeyEvent.VK_9) {
            return String.valueOf((char) ('0' + (vk - java.awt.event.KeyEvent.VK_0)));
        }
        char ch = e.getKeyChar();
        if (ch != java.awt.event.KeyEvent.CHAR_UNDEFINED && !Character.isISOControl(ch)) {
            return String.valueOf(ch);
        }
        return null;
    }

    private static String printableCode(java.awt.event.KeyEvent e) {
        int vk = e.getKeyCode();
        if (vk >= java.awt.event.KeyEvent.VK_A && vk <= java.awt.event.KeyEvent.VK_Z) {
            return "Key" + (char) ('A' + (vk - java.awt.event.KeyEvent.VK_A));
        }
        if (vk >= java.awt.event.KeyEvent.VK_0 && vk <= java.awt.event.KeyEvent.VK_9) {
            return "Digit" + (char) ('0' + (vk - java.awt.event.KeyEvent.VK_0));
        }
        return "";
    }

    private static int jsKeycode(String name) {
        switch (name) {
            case "Backspace":  return 8;
            case "Tab":        return 9;
            case "Enter":      return 13;
            case "Escape":     return 27;
            case "PageUp":     return 33;
            case "PageDown":   return 34;
            case "End":        return 35;
            case "Home":       return 36;
            case "ArrowLeft":  return 37;
            case "ArrowUp":    return 38;
            case "ArrowRight": return 39;
            case "ArrowDown":  return 40;
            case "Delete":     return 46;
            default:           return 0;
        }
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

    private static String resolveVersion() {
        String v = Browser.class.getPackage().getImplementationVersion();
        return (v == null || v.isBlank()) ? "1.0.7-dev" : v;
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
