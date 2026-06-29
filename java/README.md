# Nordstjernen for Java

**A web browser for the JVM** — and the library it is built on. `org.nordstjernen.app.Browser`
is a standalone, pure-Java desktop browser (Swing chrome over the Nordstjernen
rendering engine); the same jar doubles as an embedding library for fetching,
laying out, scripting and rendering web pages from your own Java code.

**Requires JDK 21.**

## Run the browser

The Java browser is a Swing application with GTK-shell-style chrome — back,
forward, reload, home, a URL bar and a status bar. It drives a separate
`nordstjernen-renderer` process (no native engine is loaded into the JVM), so
an engine crash can never take down the JVM.

```sh
# Nightly fat jar — library + browser app + icons + native libs in one file:
java -jar nordstjernen-java.jar https://example.com

# From source:
./gradlew run --args="https://example.com"
# or, after `installDist`: build/install/nordstjernen/bin/nordstjernen
```

Point it at the renderer binary with the `nordstjernen.renderer` system
property or the `NORDSTJERNEN_RENDERER` environment variable; otherwise it
probes `nordstjernen-renderer` on the working directory and `builddir/src/`.

### Keyboard & mouse

| Action | Keys |
| --- | --- |
| Back / Forward | `Alt+Left` / `Alt+Right`, or the mouse back/forward buttons |
| Reload | `Ctrl+R` or `F5` |
| Home | `Alt+Home` |
| Focus the URL bar | `Ctrl+L` or `Alt+D` |
| Close the window | `Ctrl+W` or `Ctrl+Q` |
| Scroll | `↑` / `↓` / `PageUp` / `PageDown`, mouse wheel, or the scrollbar |

**Text input.** Click an input field, textarea or other editable element to
focus it, then type — characters, `Backspace`/`Delete`, caret movement
(`←`/`→`/`Home`/`End`), `Enter`, and `Tab` between fields are all forwarded to
the page. Key events go to whatever the page has focused, so the arrow keys
edit a focused field rather than scrolling.

## Use as a library

The same jar is an embedding API. The in-process path loads the engine through
its C embedding API (`src/libnordstjernen.h`) via a thin JNI bridge — fetch,
parse, lay out, script and render web pages from the JVM to raw RGBA, a
`BufferedImage`, a PNG/PDF file, or extracted text, plus the page title and
final URL, all links, and pixel link hit-testing.

```java
import org.nordstjernen.Nordstjernen;
import org.nordstjernen.Page;
import java.nio.file.Path;

try (Page page = Nordstjernen.open("https://example.com", 360, 600)) {
    page.title();                                  // page <title>
    page.url();                                    // final URL (after redirects)
    page.pageSize();                               // Size[width=…, height=…] (CSS px)
    page.text();                                   // extracted (laid-out) text
    page.links();                                  // List<String> of absolute link URLs
    page.linkAt(40, 72);                           // link URL at a CSS-px point, or null

    page.renderRgba(0, 0, 720, 1280, 2.0);         // premultiplied RGBA8888 bytes
    page.render(0, 0, 720, 1280, 2.0);             // BufferedImage of a viewport
    page.renderFullPage(2.0);                      // BufferedImage of the whole page
    page.renderToFile(Path.of("example.png"));     // .pdf path → PDF, else PNG
}
Nordstjernen.shutdown();
```

`open(url, viewportWidthCss, settleMs)` lays out at `viewportWidthCss` CSS
pixels (e.g. 360 for a phone, 1000 for desktop) and lets scripts/animations run
for `settleMs` before the final layout. Render at `scale` to map CSS pixels to
output device pixels (e.g. `2.0` for a HiDPI screenshot).

### One-shot helpers

For the common "open → do one thing → close" pattern (the engine stays
initialized; call `shutdown()` at the end):

```java
BufferedImage shot = Nordstjernen.screenshot("https://example.com", 360, 2.0);
Nordstjernen.saveScreenshot("https://example.com", 1000, Path.of("page.pdf"));
String text       = Nordstjernen.textOf("https://example.com", 1000);
List<String> urls = Nordstjernen.linksOf("https://example.com", 1000);
```

These make the headline use cases — thumbnails/OG images, HTML→PDF, search/RAG
text extraction, and link crawling — one line each.

### Threading & lifecycle

The engine uses a single GLib context and is **not** thread-safe; drive it from
one thread and treat `Page` as non-thread-safe. `Page` holds native memory —
always `close()` it (it is `AutoCloseable`). `Nordstjernen.shutdown()` releases
process-wide engine state.

### Renderer-process client (no JNI)

`RemotePage` is an alternative that does **not** load the native engine into
the JVM. Like the GTK shell — and the browser app above — it spawns a
separate `nordstjernen-renderer` process (in its `stdio` mode) and drives it
over the renderer's HTTP/JSON protocol, so an engine crash can't take down the
JVM and no `libnordstjernen` needs to be on the Java library path. This is also
the client the Android shell reuses.

```java
import org.nordstjernen.RemotePage;
import javax.imageio.ImageIO;
import java.io.File;

try (RemotePage page = RemotePage.open("https://example.com", 1000, 700, 800)) {
    page.title();                              // page <title>
    page.url();                                // final URL
    page.pageSize();                           // Size (CSS px)
    page.renderRgba(0, 0, 1000, 700, 1.0);     // premultiplied RGBA8888 bytes
    ImageIO.write(page.renderFullPage(1.0), "png", new File("example.png"));
}
```

`RemoteBrowser` is the persistent counterpart for interactive shells (the one
the browser app uses): one long-lived renderer per window, with `navigate`,
`render`, `setViewport`, `linkAt`, and `press`/`release`.

## Download

Nightly builds publish a single runnable **fat jar** —
[`nordstjernen-java.jar`](https://www.nordstjernen.org/nightly/nordstjernen-java.jar)
— bundling the library API, the browser app, its icons, and the JNI native
libraries, alongside
[sources](https://www.nordstjernen.org/nightly/nordstjernen-java-sources.jar),
[javadoc](https://www.nordstjernen.org/nightly/nordstjernen-java-javadoc.jar)
and [API docs](https://www.nordstjernen.org/nightly/java/apidocs/). Run it with
`java -jar nordstjernen-java.jar <url>` (see the renderer note under
[Run the browser](#run-the-browser) about pointing it at a
`nordstjernen-renderer` binary).

## Build

```sh
# 1. Build the engine + JNI bridge (stages libs into src/main/resources/native/<os>-<arch>/)
JAVA_HOME=/path/to/jdk-21 java/scripts/build-native.sh

# 2. Build the jar (bundles the native libs)
cd java && gradle build      # -> build/libs/nordstjernen-java-<version>.jar
```

`scripts/build-native.sh` compiles `libnordstjernen` (via meson) and
`libnordstjernenjni` (the JNI bridge, linked with `RPATH=$ORIGIN` so it finds
the engine beside it), staging both into `src/main/resources/native/`. The
Gradle jar bundles them; `NativeLoader` extracts and loads them at runtime.

The nightly **fat jar** (`scripts/nightly.sh`, `stage_java`) goes one step
further: it compiles every source under `src/main/java` (library **and** the
`org.nordstjernen.app` browser), folds in the toolbar icons and the staged
native libs, and writes a `Main-Class` manifest — so the one jar is both the
embedding library and the `java -jar`-launchable browser.

### Native library resolution

`NativeLoader` resolves, in order:
1. the directory in the `nordstjernen.native.dir` system property;
2. libraries bundled in the jar under `/native/<os>-<arch>/`;
3. the platform loader (`java.library.path` / `LD_LIBRARY_PATH`).

> The jar bundles `libnordstjernen` and the JNI bridge, but **not** the engine's
> own shared dependencies (GLib, cairo, pango, libcurl, sqlite, …). Those must
> be present on the host. Self-contained bundling of the whole stack is future
> work and tracks the Android dependency cross-build.

## CI

`.github/workflows/java.yml` builds the native libs and the jar on JDK 21, then
runs `examples/Example.java` against the **bundled** jar (exercising the
resource-extraction load path) and checks it renders `about:start`.
