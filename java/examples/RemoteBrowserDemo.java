/* Nordstjernen — minimal Swing browser over the renderer-process client.
 *
 * A pure-Java window (URL bar + rendered page) that drives a separate
 * nordstjernen-renderer process via org.nordstjernen.RemotePage — no JNI,
 * no native engine in the JVM. Point at the renderer with -Dnordstjernen.renderer=…
 * or the NORDSTJERNEN_RENDERER environment variable.
 *
 *   javac -cp <nordstjernen-jar-or-classes> RemoteBrowserDemo.java
 *   java  -cp .:<…> RemoteBrowserDemo https://example.com
 */

import org.nordstjernen.RemotePage;

import javax.swing.*;
import java.awt.*;
import java.awt.image.BufferedImage;

public class RemoteBrowserDemo {

    private static JFrame frame;
    private static JTextField urlField;
    private static JLabel pageLabel;
    private static JLabel status;

    public static void main(String[] args) {
        String start = args.length > 0 ? args[0] : "https://example.com";
        SwingUtilities.invokeLater(() -> {
            frame = new JFrame("Nordstjernen (Java renderer-process client)");
            frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
            frame.setSize(1040, 820);

            urlField = new JTextField(start);
            JButton go = new JButton("Go");
            JPanel top = new JPanel(new BorderLayout(6, 0));
            top.add(urlField, BorderLayout.CENTER);
            top.add(go, BorderLayout.EAST);

            pageLabel = new JLabel("", SwingConstants.CENTER);
            JScrollPane scroll = new JScrollPane(pageLabel);
            scroll.getVerticalScrollBar().setUnitIncrement(40);

            status = new JLabel(" ");

            frame.add(top, BorderLayout.NORTH);
            frame.add(scroll, BorderLayout.CENTER);
            frame.add(status, BorderLayout.SOUTH);
            frame.setVisible(true);

            Runnable load = () -> navigate(urlField.getText().trim());
            go.addActionListener(e -> load.run());
            urlField.addActionListener(e -> load.run());

            navigate(start);
        });
    }

    private static void navigate(String url) {
        if (url.isEmpty()) return;
        if (!url.contains("://") && !url.startsWith("about:")) {
            url = "https://" + url;
        }
        final String target = url;
        status.setText("Loading " + target + " …");
        new Thread(() -> {
            try (RemotePage page = RemotePage.open(target, 1000, 760, 1200)) {
                BufferedImage img = page.renderFullPage(1.0);
                String t = page.title();
                String u = page.url();
                SwingUtilities.invokeLater(() -> {
                    pageLabel.setIcon(new ImageIcon(img));
                    pageLabel.setText("");
                    urlField.setText(u);
                    frame.setTitle(t + " — Nordstjernen (Java)");
                    status.setText(t + "   (" + img.getWidth() + "×"
                        + img.getHeight() + ")");
                });
            } catch (RuntimeException ex) {
                SwingUtilities.invokeLater(() ->
                    status.setText("Error: " + ex.getMessage()));
            }
        }, "ns-load").start();
    }
}
