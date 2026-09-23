Changelog:
==Significant changes in each release:

1.0.26:
======
* Each `<style>` element is its own style sheet. Adjacent inline sheets
  were joined before parsing, so one that ended inside an unclosed block
  or comment swallowed every sheet after it. `<style type="text/foo">`, a
  `<link>` whose `type` is not CSS and `<link disabled>` no longer apply,
  and `styleEl.disabled` / `sheet.disabled` switch a sheet off.
* A declaration whose `var()` cannot be substituted computes as `unset`
  instead of letting an earlier declaration win, as CSS Variables
  requires; a substituted value carrying `!important` counts as failed.
* Declarations written after a nested rule keep their place in the
  cascade instead of losing to the nested rule.
* A stray `;` or `}` between rules invalidates the rule after it, and a
  selector list with a trailing comma, an empty item or junk is dropped
  whole, as CSS Syntax says and other browsers do. A type selector after
  an id, class or attribute is rejected too.
* Comments inside a media query are whitespace, so
  `@media (min-width: 100px) /* desktop */` applies again.
* An inline `!important` beats a layered `!important` rule.
* `initial` gives inherited properties their real initial value instead
  of behaving like `inherit`; `color: currentColor` takes the parent's
  colour; and `bolder`/`lighter` resolve against the parent's weight with
  the CSS Fonts 4 table.
* `text-shadow`, `orphans`, `widows` and `dominant-baseline` are
  inherited, so a shadow on a container reaches its paragraphs.
* A percentage `line-height` is inherited as a length rather than
  re-applied to each child's font, and `rem` in the root's `font-size`
  refers to the initial size rather than to itself.
* Every layer of a multi-image background resolves its `url()` against
  the stylesheet, not the document.
* A `min()`, `max()` or `clamp()` inside `calc()` resolves against the real
  percentage basis instead of the window width, and NaN or infinite
  `calc()` results are clamped instead of reaching layout.
* `display: contents` computes to `none` on replaced elements, form
  controls and an outermost `<svg>`, so their fallback no longer leaks.
* `translate()` and the `translate` property keep percentages and font
  units, so the `translateX(calc(-50% + 10px))` centring idiom works, and
  `getComputedStyle().transform` no longer folds in `translate`, `rotate`
  and `scale`.
* `selectorText` and a style rule's `cssText` drop comments and write the
  attribute case flag as ` i]`.
* A flex item with a height of its own keeps it. Stretching ignored
  whether an item's height was `auto` and ignored `min-height`/`max-height`,
  so a 20px item in a 100px row came out 100px tall; only auto-height items
  stretch now, within their min and max, and the relayout that stretching
  triggers keeps the flexed width.
* A `position: fixed` box inside a transformed element is positioned and
  painted relative to that element and scrolls with it, as css-transforms
  requires, instead of being pinned to the window.
* Border-box sizing is honoured in more places: floats are placed by their
  border box, so a Bootstrap-3-style row of padded `width: 50%` columns no
  longer wraps; shrink-to-fit widths stop counting padding twice; a
  percentage height inside a border-box parent resolves against its content
  box; and tables honour `box-sizing` and are border-box by default, so a
  bordered `width: 100%` table no longer overhangs its container.
* A float is never narrower than its longest word, and a `width: 0` child no
  longer widens a shrink-to-fit parent.
* Absolutely positioned boxes shrink-wrap including their margins, padding
  and border, honour `min-height`/`max-height`, find their static position
  inside their parent rather than after it, and treat `margin: auto` as 0
  unless both `left` and `right` are set.
* A last child's bottom margin stays inside `overflow: hidden` blocks,
  floats, inline-blocks and parents with a fixed or minimum height instead
  of escaping through them, and the document contains the body's bottom
  margin.
* Flexbox: images and other replaced items stretch across column
  containers, items move with their line when `align-content` grows it, and
  row and column follow a vertical `writing-mode`.
* `min-content`, `max-content`, `fit-content` and `stretch` work in
  `min-width`, `max-width`, `min-height` and `max-height`, and as flex item
  sizes.
* Text around an HTML comment or a hidden element keeps its spaces ("Text
  after", not "Textafter").
* `<center>` and `align="center|right"` move block-level children too,
  including a table nested in an aligned cell.
* `document.fonts.load()` and `FontFace.load()` wait until the web font has
  loaded, and `document.fonts.ready` resolves with the FontFaceSet, so
  scripts that measure text after them see the web font's metrics.
* A text input's line height is never smaller than `normal`, so a reset
  such as `input { line-height: 1px }` no longer clips the field.
* Viewport units in an iframe resolve against the frame, even when a
  stylesheet sizes the frame, and `calc()` expressions with vw/vh follow the
  current viewport rather than the one in effect when the stylesheet was
  parsed.
* An embedding page can no longer read a cross-origin frame's document.
  `iframe.contentDocument` returned the framed page's DOM and
  `contentWindow` its real window whatever their origin, so any page
  could frame another site and read or script it. For a cross-origin
  frame, or one sandboxed without `allow-same-origin`, `contentDocument`
  is now `null` and `contentWindow` (and `window.frames[n]`) a restricted
  window that allows only `postMessage`, the `location` setter, `closed`,
  `length`, `window`/`self`/`frames`/`parent`/`top`/`opener` and
  `close`/`focus`/`blur`. Same-origin, `about:blank` and `srcdoc` frames
  are unaffected.
* `window.scrollTo()`, `scroll()` and `scrollBy()`, and setting
  `scrollTop`/`scrollLeft` on the root element, move the page. They only
  changed the position script read back; the view stayed where it was. They
  now lay out if needed, clamp to the scrollable range and scroll the
  viewport in both axes. `scrollBy()` called bare, as it almost always is,
  read its position from `undefined` and scrolled to NaN, and `scrollY`
  briefly snapped back to the old position before the view caught up.
* `postMessage`'s `targetOrigin` is checked against the origin of the
  window the message goes to. The check read that window's `location`,
  which answers with the URL of whichever frame's script is running, so
  when a frame posted to its parent, the parent seemed to have the
  frame's own origin: a message addressed to the parent's real origin was
  dropped, and one addressed to any other origin -- the case
  `targetOrigin` exists to stop -- was delivered. The top-level window's
  origin now comes from its document, `"/"` means the sender's own origin
  instead of matching everything, and an origin is compared as an origin,
  not as a string prefix.
* A cross-origin iframe can no longer script the page that embeds it.
  A frame's `parent` and `top` were the embedding page's real window, and
  the frame's global object inherited from it, so a framed site could read
  and rewrite the embedder's DOM, `document.cookie` and `localStorage`
  (only its own storage threw SecurityError). A cross-origin frame now
  gets a restricted window proxy for `parent` and `top`, exposing only
  `postMessage`, the `location` setter, `closed`, `length`,
  `window`/`self`/`frames`/`parent`/`top` and `close`/`focus`/`blur`, and
  throwing SecurityError for anything else. Its global inherits
  `Window.prototype` rather than the parent's global. Messages it
  exchanges with the parent carry the right `source` in both directions.
  A frame sandboxed without `allow-same-origin` is treated the same way,
  whatever its URL.
* A framed document's `document.cookie` reads and writes the cookies of
  the frame's own URL. It used to return the embedding page's cookie
  string, and assigning to it replaced that string. Documents that have no
  browsing context -- from `DOMParser`, `cloneNode` or
  `createHTMLDocument` -- are cookie-averse and return the empty string,
  as HTML specifies. The `Document.prototype` accessor also no longer
  hands the top-level page's cookies to another document it is called on.
* An iframe whose load handler navigates it again no longer hangs the
  page. Loading a queued frame fires its load event synchronously, so a
  handler that set `src` once more re-queued the same frame and the loader
  never returned: timers, painting and the rest of the page starved while
  the renderer spun at full CPU. Each pass now loads a frame at most once;
  a frame queued again waits for the next tick.
* A multi-column block splits a list, not just a run of siblings. The
  column code distributed a container's own children and gave up below
  two of them, so `column-width` on a wrapper whose sole child is an
  `<ol>` -- which is how a Wikipedia reference list is built -- laid the
  whole list out in one column. A lone in-flow block child is looked
  through now and its children distributed instead.
* A multi-column block establishes a block formatting context, as the
  spec says, so it sits beside a float rather than under it.
* A block that establishes a formatting context is placed clear of every
  float it spans, not just those beside its top edge. It was narrowed
  against the float band at its first line and kept that width all the
  way down, so a wider float lower down overlapped it.
* A table column is never narrower than its cells' contents need. The
  auto layout measured a cell's minimum through measure_min_width, which
  returns a specified width when it has one, so a cell's own width
  doubled as its minimum: `width: 1%` on a heading cell -- the idiom
  Wikipedia's navboxes use to shrink a column to its label -- left the
  column one per cent of the table wide and its text ran across the cell
  beside it. The floor is the cell's min-content width now, and when the
  minimums together exceed the width the table asked for, the table grows
  to their sum instead of scaling every column down below what it can
  hold, as CSS 2.1 requires.
* `min-width` and `max-width` on a table cell take part in the column
  measures, clamped max-then-min the way the rest of the box model is.
* A table cell inherits `text-align` from the table or the row. The
  default stylesheet pinned `td, th` to `text-align: left`, which no
  browser's does, so `text-align: center` on a `<table>` or a `<tr>`
  reached the caption and nothing else. `vertical-align` likewise moves
  from a pinned `middle` on the cell to the spec's arrangement -- the row
  groups carry it and the cells inherit -- so a row can set it, and
  `align`/`valign` now map on the row, row-group and column elements too.
* An inline-block whose width is a percentage no longer drags the
  intrinsic width of whatever contains it up to the width of the page:
  a percentage is indefinite while intrinsic sizes are measured, so the
  atomic is measured against its own content.
* `content: '[' / ''` renders just the bracket. The alternative text a
  `content` value carries after a slash, for a screen reader to read in
  place of the glyphs, was drawn as part of the text, so MediaWiki's
  section-edit links came out as `[/ edit ]/`.
* A list item styled `display: inline-block` or `display: block` draws no
  bullet; only a `list-item` display generates a marker.
* `<th>` paints no background of its own and `<caption>` is not bold,
  `<figcaption>` is not italic, and `<figure>` and `<dl>` carry the
  margins the HTML rendering rules specify. None of these are in a
  browser's default sheet, and each showed through wherever a page paints
  its own tables or figures.
* The Android app is about 8 MB smaller. The build staged every shared
  library in the dependency sysroot into the APK, including ones this
  build does not link at all -- llama/ggml, gobject-introspection and the
  unused harfbuzz and pcre2 variants -- so 19 of the 51 libraries per ABI
  were dead weight. Only the engine's `DT_NEEDED` closure is packaged now.

1.0.25:
======
* Restyling a large document is roughly 40% faster. The style-sharing
  cache builds a lookup key for every element on every cascade pass by
  serialising that element's matched declarations; on Speedometer 3.1's
  6,650-node complex-DOM pages that key cost more than the cascade it
  was meant to avoid — 76ms of a 190ms pass. The key is now written
  once into a correctly sized buffer instead of through ~2 million
  incremental byte-array appends, the container-query part of the key
  no longer scans every matched custom property when no container is on
  the stack, and the key hash reads eight bytes at a time. Selector
  gathering also jumps straight to the pseudo-element bucket a matched
  selector belongs to rather than testing all ten. The same pass now
  takes 120ms. Across the 22 loadable Speedometer 3.1 TodoMVC workloads
  the aggregate score improves 31.7% (1.328 to 1.749); Vue-Complex-DOM
  drops from 4810ms to 881ms and jQuery-Complex-DOM from 4948ms to
  3140ms. Layout output is byte-identical.
* The toolbar takes the classic look of the Northstar web browser: a
  raised, softly shaded bar with labelled colour buttons for Back,
  Forward, Reload, Stop, Home, Print and Downloads, bevelled hover and
  pressed states, etched separators, an inset address field that shows
  a page icon when there is no certificate state to report, labelled
  Bookmarks and Menu buttons, and the Nordstjernen logo on a dark tile
  at the far right. Stop stays in place and greys out when nothing is
  loading instead of disappearing, Print and Downloads have their own
  toolbar buttons, the Go button is gone (Enter and the address bar do
  the same thing), and the bookmark button uses the bookmark icon, which
  fills in with a gold star when the current page is saved. The README
  screenshot shows the new toolbar.
* Fullscreen mode is announced: when a page calls requestFullscreen the
  shell overlays a notice at the top of the page naming the site's host
  and saying it is now full screen and that Esc exits, in the style of
  Chrome and Firefox, so a page can't hide the address bar and paint a
  spoofed one without the user being told. The notice stays for five
  seconds and can't be covered by page content. Element fullscreen now
  also ends by itself when the fullscreen tab navigates to a new
  document, when another tab is switched to, or when the tab is closed —
  the header and toolbar used to stay hidden across all three. And per
  the Fullscreen API, requestFullscreen() now needs transient user
  activation: called from a timer or on load without a recent click,
  tap or key press it rejects with a TypeError, fires fullscreenerror
  and logs to the console instead of taking over the screen.
  (Reported by Muhammad Wishal.)
* The nightly .deb installs again on a Debian that is a patch release
  behind the build container. dpkg-shlibdeps copied Debian's FFmpeg
  shlibs floor, which is the exact upstream version of the build host's
  FFmpeg, so the package demanded e.g. libavcodec61 (>= 7:7.1.5) and
  dpkg refused it on a system with 7:7.1.1 although the SONAME, and so
  the ABI, is the same. scripts/pack-deb.sh now relaxes the libav* and
  libsw* floors to the FFmpeg major.minor release (>= 7:7.1) while the
  SONAME-numbered package names keep guarding the ABI, logs the final
  Depends line, and no longer Recommends an external media player the
  shell doesn't launch. The container build then installs the .deb, .rpm
  or .apk it produced and runs the installed browser headlessly, so a
  package whose metadata, maintainer scripts, dependencies or installed
  paths are broken fails the nightly stage instead of reaching the
  download links. (Reported by guest271314.)
* The nightly download links no longer 404 when one platform's build
  fails: scripts/nightly.sh publishes a stage's directory only once it
  holds artifacts and otherwise keeps the previous night's files, a
  container that fails after pack-linux.sh still ships its portable zip,
  the Linux zip link falls back from the Ubuntu build to the Debian or
  openSUSE one, dangling stable links are removed instead of left to
  404, and MANIFEST.txt records why a stage failed and that its files
  are stale.
* The README's download table lists the Windows Store and Google Play
  listings and the source release tags; the nightly build links are
  gone from it.
* The .deb no longer bundles the dynamic loader: pack-deb.sh's core
  runtime deny list matched ld-linux only when a dot followed the name.
* The lexbor encoding module is trimmed to UTF-8. Its 43 legacy codecs
  (Big5, GB18030, EUC-JP, Shift_JIS, the ISO-8859 and Windows code
  pages, UTF-16 and the rest) had no caller: page bodies are decoded
  through uchardet and g_convert, and lexbor's own URL parser only ever
  needs UTF-8, yet its codec lookup table kept every conversion table
  alive through the linker. Dropping them removes 11 MB of generated
  source, about 1 MB from each binary that links lexbor, and the
  per-encoding branches from the URL parser's query serializer.
* The C DOMMatrix in js_canvas.c is gone. The startup polyfill defines
  the 4x4 DOMMatrix, DOMMatrixReadOnly, DOMPoint and WebKitCSSMatrix
  and assigned them over the native constructors on every page, so the
  C class was dead at runtime while canvas getTransform() and the SVG
  getCTM()/getScreenCTM() helpers still minted the C flavour, which
  failed instanceof DOMMatrix. They now construct through the page's
  DOMMatrix constructor, and about 250 lines of duplicate matrix code
  and their declarations are removed.
* The startup polyfill no longer stubs the Credential Management API:
  navigator.credentials resolved every call to null and Credential,
  PasswordCredential, FederatedCredential, PublicKeyCredential and
  IdentityCredential were empty data holders, so sites that feature
  detect WebAuthn or password autofill took a code path that could only
  fail. With the properties absent they fall back to plain forms.
* Repository weight: the 1.0.22, 1.0.23 and 1.0.24 splash frames
  (1.4 MB of PNGs nothing referenced) are deleted, and the QuickJS
  tree drops the harness sources the build never compiled (api-test.c,
  lre-test.c, fuzz.c, ctest.c, cxxtest.cc and the WASI reactor) with
  their CMake and Makefile targets.
* CI moves to current toolchains: CodeQL Action v4 (v3 is retired in
  December 2026), setup-java v6, actions/cache restore and save v6 on
  Windows, FreeBSD 15.1 and NetBSD 11.0 VMs (14.2 and 10.0 are past
  end-of-life), NDK r30 (30.0.16248370) for the Android and CodeQL
  builds, and the V8 15.2 monolith for the js_engine=v8 job. The V8
  backend follows the V8 15 embedder API, which requires a type tag on
  every v8::External and aligned internal-field pointer, and the build
  now passes the pointer-compression defines the just-js monolith is
  built with; docs/V8.md notes that the CREL relocations in those
  releases need lld 19 or newer. NDK r30's bionic refuses
  malloc_usable_size under _FORTIFY_SOURCE=3, so the QuickJS allocator
  reports the usable size as unknown on Android instead of calling it.
* about:start wears the Northstar web browser's splash, carried over
  from that project's scripts/gen-splash.py and retitled "Nordstjernen
  web browser" with this release's version read from meson.build: a
  flat, sunny xkcd-style comic of Noah's ark in Comic Neue lettering,
  with the animal pairs walking two by two to the gangplank behind Noah
  and his clipboard ("Two of each. Yes, even browsers."), a wooden ark
  with a flag, portholes, a chimney and a penguin on deck, blue wavy
  water with a spouting whale, a red-and-white lighthouse with its
  keeper waving from the gallery, gulls, a dove, drifting clouds and a
  yellow sun. Every frame is drawn at three times supersampling and the
  32 frames (walking gaits, a bobbing ark, a fluttering flag, chimney
  smoke, spinning sun rays, flapping gulls) are quantized to one shared
  256-colour palette and squeezed by gifsicle into the embedded GIF.
  scripts/gen-splash.sh regenerates it and writes the first frame to
  data/about-splash-<version>.png; the earlier build-splash-art.py and
  build-splash-gif.py generators are gone.
* Container queries evaluate the full condition grammar: not/and/or
  with nesting, size features in plain, boolean and range form
  (double-sided ranges and math functions included), aspect-ratio and
  orientation, unknown features that make the enclosing condition
  false, comma-separated condition lists, name-only rules and vertical
  writing-mode containers. Invalid preludes are dropped from the engine
  and the CSSOM, conditionText serializes canonically, container-name
  and the container shorthand validate their values, and
  CSSContainerRule exposes containerName, containerQuery and
  conditions.
* image-set() is validated against the CSS Images 4 grammar (url and
  string images, gradients, resolutions in x, dppx, dpi and dpcm, a
  type() hint, no duplicate or missing options) and serializes
  canonically; unicode-range validates and canonicalizes its ranges
  (U+26, u+0-7F, U+4?? read back U+26, U+0-7F, U+400-4FF).
* An absolutely positioned box whose static position comes from an rtl
  ancestor sits at that ancestor's content-right edge, and the used
  values of auto insets are reported physically. font-family keeps
  random-item() and -webkit-generic() items, validates random-item()'s
  arguments and rejects a generic family inside a multi-word name; the
  background-position and object-position shorthands accept CSS-wide
  keywords; specified grid track lists serialize their line names.
* Transitions and animations run for every property. The animation
  engine keeps one channel per (element, property): a transition starts
  whenever a property named by transition-property (or "all") computes
  to a different interpolable value, lengths, percentages, calc()
  mixes, numbers, colours, shadow lists and op-compatible transform
  lists interpolate, visibility flips discretely, and keyframe
  animations sample every declared property between the surrounding
  keyframes, so margins, sizes and colours animate, not just opacity
  and transform. Interpolated values are written into the computed
  style after each cascade, so layout, paint and getComputedStyle see
  the in-flight value; a frame whose animated properties affect layout
  marks the page for relayout.
* The Web Animations surface: document.getAnimations() and
  element.getAnimations() return CSSTransition and CSSAnimation
  objects (stable identity per element, property and run) with
  currentTime and startTime that seek the engine, playState, pending,
  the ready and finished promises, play/pause/finish/cancel, the finish
  and cancel events, transitionProperty/animationName and an
  AnimationEffect whose target, getTiming() and getComputedTiming()
  describe the run. Element.animate() builds a keyframe animation from
  a keyframe list or a property-indexed object with duration, delay,
  iterations, direction, fill and easing.
* Several CSS animations run on one element, one per animation-name
  entry, each with its own duration, delay, timing function, iteration
  count, direction, fill mode and play state from the animation
  longhands; animation-timing-function, animation-iteration-count,
  animation-direction, animation-fill-mode, animation-name,
  transition-property and transition-timing-function are real
  longhands that parse, cascade and serialize on their own, and
  AnimationEvent and TransitionEvent are constructible.
* The animation and transition shorthands parse per comma-separated
  item against their full grammar (a time is a duration before it is
  a delay, an easing keyword, steps() or cubic-bezier(), a
  transition-behavior keyword, "auto" and "none" where allowed),
  expand into their longhands with the omitted ones reset, and
  style.animation, style.transition and getComputedStyle rebuild the
  shorthand from the longhands in canonical order. animation-timeline,
  animation-range-start, animation-range-end, animation-composition and
  transition-behavior are properties.
* Animation and transition events fire by phase: animationstart at
  the end of the delay, animationiteration on every iteration boundary,
  animationend once, transitionrun when the transition is created,
  transitionstart after its delay, transitionend when it completes,
  and the cancel events when a run is interrupted; elapsedTime and
  pseudoElement are filled in. A transition can start from an unset
  value (the property's initial value) and a keyframe that leaves a
  property out fills it from the base style.
* attr() is substituted at cascade time, so content: attr(data-x)
  and attr() in other properties follow attribute changes; an attr()
  URL is tainted. Boxes honour width: stretch and height: stretch,
  grid item margins are resolved against the grid area, a canvas
  takes its width and height attributes as dimension hints, and the
  specified style serializes shorthands from their longhands.
* aspect-ratio keeps its numerator and denominator (16 / 9 reads back
  16 / 9), an absolutely positioned box with both insets set is
  aligned inside them by justify-self and align-self rather than
  stretched unconditionally, and a shrink-to-fit abspos box measures
  against the inset width.
* A declaration whose value is exactly one {}-block is a declaration
  rather than a nested style rule when the CSSOM splits a style block,
  and a custom property keeps a {}-block anywhere in its value.
* A table's max-content and min-content widths are measured column by
  column, as css-tables-3 requires: each column takes the widest cell
  it holds, the columns are summed once with the border spacing, and
  captions widen the result. They used to be the sum of every row, so
  a table in a flex or grid item, a floated infobox and a table nested
  in a cell reported several times their real width.
* position: fixed elements stay anchored to the viewport while the
  page scrolls: paint offsets a fixed box by the viewport origin,
  hit-testing applies the same offset so clicks land on the fixed
  element, getBoundingClientRect reports its viewport position, and
  mouse events carry viewport-relative clientX/clientY with document
  coordinates in pageX/pageY. position: sticky boxes are hit-tested
  where they paint through one shared ns_box_sticky_offset that
  resolves percentage and calc() insets against the scrollport and
  measures a sticky box inside an overflow container against that
  container's padding box.
* box-shadow and text-shadow serialize their specified value in
  canonical order (colour, offsets, blur, spread, inset) with 0
  written as 0px, and reject the forms the grammar excludes: a lone
  length, a fifth length, two colours, inset twice, a negative blur,
  a percentage, or a colour splitting the lengths. A shadow without a
  colour takes currentcolor from the computed color, and rgb(0, 255,
  0) with spaces inside the parentheses no longer splits into tokens.
  A colour keyword keeps its lowercase spelling in the specified style
  and the deprecated CSS2 system colours map to their CSS Color 4
  replacements.
* The background shorthand is parsed layer by layer against the
  css-backgrounds grammar: every comma-separated layer sets all eight
  longhands (image, position, size after the slash, repeat,
  attachment, origin and clip, with the colour on the final layer) and
  a longhand the layer leaves out resets to its initial value, so
  background: red no longer keeps an earlier background-image.
  background-attachment is a property, background-clip,
  background-origin and background-attachment take comma-separated
  lists, paint resolves origin and clip per layer, clips the colour by
  the last layer's clip and positions a background-attachment: fixed
  layer against the viewport, background-position keeps the keywords
  it was written with, background-position-x/-y accept x-start,
  y-end and an edge with an offset, and getComputedStyle composes
  border-radius and background-position from their longhands.
* -webkit-border-radius and the -webkit-border-*-radius corners are
  aliases of the unprefixed properties, and the border-radius
  shorthand is validated before any corner is written: a fifth value,
  a negative radius or a second slash rejects the declaration, and the
  specified value collapses each half as a quad.
* transform is validated function by function against css-transforms:
  each function checks its argument count and types, so translate(1px,
  2px, 3px), scale(6, 7, 8) and skewX(0, 0) are rejected, and the
  specified value serializes canonically (percentages in scale()
  become numbers, rotate(0) reads rotate(0deg), 0 lengths read 0px,
  function names are lowercased). The scale, rotate and translate
  properties get the same treatment, transform-origin and
  perspective-origin follow the position grammar, perspective: 1000
  without a unit is rejected, and transform-box is a property.
* The border, border-top/right/bottom/left, border-block-*,
  border-inline-*, outline and column-rule shorthands are validated
  against their grammar before any longhand is written: a second
  width, style or colour, a negative or percentage width, a unitless
  number other than zero or an unknown keyword rejects the whole
  declaration instead of leaving a partial expansion behind, and the
  outline and column-rule shorthands reset the longhands they leave
  out. outline-style accepts auto.
* Percentage border radii resolve against the box (border-radius: 50%
  on a 200x100 box is an ellipse, not a 50px circle), a corner takes a
  horizontal and vertical radius pair (border-top-left-radius: 10px 5px)
  and the border-radius shorthand honours the vertical radii after the
  slash; paint draws elliptical corners and scales overlapping radii
  down together as css-backgrounds requires, and em/rem pairs resolve
  against the font size in the computed style.
* A transition from transform: none no longer resets the target
  transform to identity: the interpolation built its identity endpoint
  on an alias of the target value and zeroed it in place. An animated
  value propagates to descendants only for inherited properties, so a
  child sharing the parent's opacity, width or transform value is no
  longer animated along with it. A transition on a property paint does
  not read live (box-shadow, filter, border colours, visibility) now
  restyles the page each frame instead of showing the stale value.
* A frame's parent.postMessage() reaches the listeners the top document
  added with addEventListener: the message used to be dispatched
  against the frame's own document, so only window.onmessage saw it.
* Setting style.borderTopLeftRadius (or another corner) on an element
  whose inline style carries a later border-radius appends the corner
  after the shorthand, so the new value wins as the CSSOM requires.
* When several options of a single-choice <select> carry the selected
  attribute, the last one wins, as the HTML selectedness setting
  algorithm requires. HTMLOptionsCollection exposes selectedIndex, and
  every event the engine dispatches carries a composed flag.
* quickjs-ng is at v0.16.2: bytecode constant pools are 8-byte
  aligned, proxy traps consult IsExtensible() on the target so a
  nested proxy's trap is observable, ownKeys must return an object,
  the array iteration builtins poll for interrupts so a long loop
  stays interruptible, TypedArray.prototype.with converts through
  ToBigInt on the 64-bit arrays, and the regexp parser rejects the
  identity escapes that are invalid in unicode mode outside a class.
  WAMR is at 2.4.5: the constant-expression loader rejects an invalid
  reference type in ref.null and the fast-interpreter constant table
  can no longer desynchronise its two passes.

1.0.24:
======
* about:start wears a new splash: the same night scene, painted in an
  impressionist hand with a gilded frame, rendered by
  scripts/build-splash-art.py from the version in meson.build so a
  release regenerates the artwork with two scripts and no image editor.
* Flex layout resolves flexible lengths the way css-flexbox-1 §9.7
  describes: one implementation shared by row, wrapping-row and column
  containers distributes free space with the item freezing loop, so flex
  factors below one scale the free space, flex-shrink is weighted by the
  flex base size, and min/max violations are frozen and re-distributed.
  The automatic minimum size of a flex item is min(content size,
  specified size) rather than the specified size, so width: 200px in a
  100px container shrinks as browsers do; min-content and max-content
  minimums are honoured, and a percentage size on a replaced element or
  text control counts as zero for the specified size suggestion.
* Column flex containers wrap: flex-wrap: wrap and wrap-reverse break
  items into lines against the definite main size, align-content places
  the lines (start/end resolve in the inline axis; space-around and
  space-evenly fall back to start when the lines overflow), a wrapping
  container with a single line is still multi-line, rtl mirrors the
  cross axis, column-reverse packs from the main end, auto margins in
  the main axis absorb free space, and an indefinite-height column sizes
  itself from its items' content contributions so flex: 1 items no
  longer collapse.
* Negative free space overflows in the right direction: space-between,
  space-around and space-evenly fall back to start, flex-end and center
  overflow the start edge, and a scroll container packs overflowing
  content toward its start so it stays reachable; row wrap-reverse
  mirrors lines against the container's definite height.
* scrollWidth and scrollHeight include the scroll container's end
  padding and, in rtl, overflow to the left. offsetTop and offsetLeft
  round negative values to nearest and flush pending layout before
  locating the offset parent.
* The static position of an absolutely positioned flex child honours
  start, end, left and right on justify-content and align-self as
  writing-mode-relative keywords, self-start/self-end use the child's
  own direction, and last baseline aligns to the cross end.
* align-items, align-self, align-content, justify-content, justify-items
  and justify-self parse first baseline, last baseline and the safe and
  unsafe prefixes.
* WPT css/css-flexbox: 1465 -> 1997 of 3670 subtests on a 2026-09
  checkout of the horizontal-writing-mode suites.
* An absolutely positioned box whose containing block is a grid
  container takes that block from its grid-column and grid-row lines,
  as css-grid-1 §9 requires: a line inside the explicit grid resolves to
  the edge of the adjacent track, a line outside it, an unknown name or
  a span resolves to the padding edge after the start/end swap, and
  offsets, percentages and shrink-to-fit sizes resolve against that
  area. A shrink-to-fit abspos box no longer squeezes below its
  min-content width when the area is narrower than its content.
* align-content: stretch on a grid container distributes free block
  space only to rows whose max track size is auto; fixed-length rows
  kept their length.
* An absolutely positioned element with an inline-level display that
  follows inline content takes its static position from the line it
  would have occupied, after the preceding text, instead of the top of
  the block.
* document.fonts.ready waits for the web fonts the page needs: it
  flushes style so pending @font-face loads are requested, resolves
  once the loader is idle and marks the document for relayout;
  fonts.status reports loading meanwhile.
* Alignment properties keep their full specified keyword: safe and
  unsafe prefixes, legacy left/center/right, first baseline (computed
  as baseline) and last baseline parse and serialize, and the
  place-self, place-items and place-content shorthands split two-word
  values and serialize a repeated value once.
* offsetTop/offsetLeft flush pending layout before locating the offset
  parent, so a first read during parsing no longer returns viewport
  coordinates.
* Grid containers with direction: rtl lay their columns out from the
  right, and grid-placed absolutely positioned boxes mirror with them.
* repeat(auto-fit, ...) collapses the repeated tracks that no in-flow
  item occupies, so a card grid with fewer cards than columns stretches
  the remaining fr tracks as browsers do.
* A grid item's percentage height resolves against its grid area when
  the rows it spans have definite track sizes, and an absolutely
  positioned child of a grid container with auto offsets takes its
  static position from its grid area, aligned by justify-self and
  align-self.
* getComputedStyle on a grid container returns the used track sizes for
  grid-template-columns and grid-template-rows, with explicit line names
  in place, as CSSOM requires.
* Track lists resolve em against the element's own font size and calc()
  percentages against the track axis; an auto track grows to its
  max-content contribution before fr tracks share the remainder, fr rows
  fill a definite container height, over-constrained minmax() rows
  shrink toward their minimum, percentage rows in an indefinite-height
  grid re-resolve against the final height, and an auto column measures
  its content with real text metrics.
* The grid track-list parser rejects negative sizes, stray commas,
  consecutive or trailing-only line-name lists, reserved words as line
  names, a second auto-repeat and non-fixed tracks beside one, and
  supports line names inside repeat().
* JavaScript can be turned off: an "Enable JavaScript" toggle in
  Settings parses pages with scripting disabled (so noscript content
  renders) and skips script execution entirely — the user decides what
  runs, in the old Mozilla tradition.
* about:mozilla shows the maroon page every browser of this lineage
  owes its readers, and about:config opens the settings page.
* The status bar reads "Done" for a moment when a page finishes
  loading, as it always did.
* Fixed a use-after-free of the session URL: timer, event-dispatch and
  requestAnimationFrame callbacks saved the current URL pointer and restored
  it unconditionally after the callback, so a handler that navigated (a
  fragment click, location.hash =, history.pushState) left the engine
  reading and double-freeing a freed URL. The URL is now restored only when
  it was actually swapped for an iframe realm.
* Added view-source: — Ctrl+U / "Page Source" in the menu shows the current
  page's HTML with classic syntax highlighting. Only chrome-initiated
  navigations can use the scheme; web content is refused.
* input.showPicker() and select.showPicker() are implemented per spec:
  InvalidStateError on disabled or readonly controls, NotAllowedError
  without a user gesture, and a successful call consumes the activation.
  WPT show-picker suites pass 129/129.
* stepUp()/stepDown() follow the spec: they throw InvalidStateError on
  non-numeric input types and step="any", honor the per-type default
  step and scale, round to the step grid, clamp to min/max, and
  serialize date, month, week, time and datetime-local values back to
  their canonical strings. WPT input-stepup 53/53 and time 32/32.
* The color input sanitizes through the CSS color parser: keywords,
  rgb() and #rgb shorthand normalize to lowercase six-digit hex, and
  surrounding whitespace is stripped.
* Label association follows the spec: label.control resolves the for
  attribute against the label's own tree to the first element with that
  id (null when it is not labelable or the attribute is empty),
  label.form returns the associated control's form owner, and .labels
  is a live NodeList.
* The WPT harness's testdriver bridge grants real user activation for
  simulated gestures, and the __nsWpt* hooks are inert outside the
  harness.
* Form constraint validation follows the HTML spec much more closely:
  ValidityState flags are computed for disabled and readonly controls
  (bars from validation affect willValidate, not the flags), valueMissing
  is suppressed on disabled/readonly controls, the pattern attribute
  compiles as a JavaScript regular expression with the v flag (invalid
  patterns are ignored) and applies to each address of a multiple email
  input, tooLong/tooShort fire only after a user edit as the spec's dirty
  value flag requires, and willValidate is false inside a datalist.
  WPT form-validation: patternMismatch, tooLong, tooShort, typeMismatch
  and badInput suites now fully pass.
* Live HTMLCollection/NodeList property semantics follow WebIDL: silent
  sloppy-mode failures and strict TypeErrors for read-only indexed and
  named properties, spec-compliant descriptors, expando support, and
  Object.keys listing only indices; moveBefore() is ParentNode-only;
  replaceChildren() queues a single mutation record; Node.isConnected is
  true for any node whose root is a document.
* JavaScript can be turned off: an "Enable JavaScript" toggle in
  Settings (and the NS_NO_JAVASCRIPT=1 environment variable) parses
  pages with scripting disabled, so noscript content renders, and skips
  script execution entirely.
* about:mozilla shows the maroon page every browser of this lineage
  owes its readers, and about:config opens the settings page.
* The status bar reads "Done" for a moment when a page finishes
  loading, as it always did.
* Popup blocking, in the Firefox 1 tradition: window.open only navigates
  when called within five seconds of a real user gesture (click, key
  press, touch), consumes that activation, and logs blocked attempts to
  the console. navigator.userActivation now reports the live activation
  state instead of constants.
* Ctrl+Enter in the address bar completes a bare name to www.name.com,
  Shift+Enter to .net and Ctrl+Shift+Enter to .org, as in classic
  Firefox.
* Ctrl+Shift+R and Ctrl+F5 reload the page bypassing the HTTP cache.
* Ctrl+D bookmarks the current page, with a matching "Bookmark This Page"
  menu entry, in the classic browser tradition.
* about:book — every browser of the lineage carries its Book.
* Updated the ns-pango subproject pin to the latest upstream commit.
* Optimized event dispatch throughput by lazily evaluating composedPath()
  arrays on demand using active dispatch paths.
* Added node-level listener filtering with NS_NODE_HAS_LISTENERS flag, skipping
  listener array iterations on intermediate DOM tree nodes without listeners.
* Bound standard Event prototype methods (preventDefault, stopPropagation,
  stopImmediatePropagation, cancelBubble, composedPath) directly on Event.prototype.
* Accelerated document.createElement with lowercase ASCII fast paths and
  ASCII-first element name validation.
* Optimized inline style property conversions (camel_to_kebab) and empty initial
  inline style updates avoiding intermediate GString allocations.
* Optimized dataset property lookups by matching target attribute names directly
  without repeated per-attribute string allocations.
* Bypassed redundant storage allocations and event dispatches when setting
  identical localStorage/sessionStorage values.
* Added HTMLDialogElement.showModal() standards compliance validating open and
  connected document state.
* Aligned structuredClone with specification requirements (argument validation
  and transfer options support).
* Optimized CSS.escape for simple identifiers and bounded selector and form
  ancestor traversals.
* Optimized DOM textContent and innerText get/set paths with zero-allocation
  string returns for empty and single text child nodes.
* Optimized DOMTokenList (classList) add, remove, and toggle with single-token
  fast paths avoiding token array and parser allocations.
* Accelerated DOM hierarchy validation in pre-insert checks and ancestor-or-self
  queries with O(1) leaf child checks and bounded traversals.
* Optimized DOM attribute operations (getAttribute, setAttribute, hasAttribute,
  toggleAttribute, removeAttribute) with zero-allocation lowercase ASCII fast paths
  and single-pass string conversion.
* Fast-path selector matching in Element.matches() and Element.closest() for simple
  class (.cls), tag (tag), and id (#id) selectors, avoiding full CSS selector AST
  allocations on delegated event lookups.
* Subtree getElementById queries leverage document root ID indexes and bloom filters
  prior to falling back to full recursive DOM tree traversal.
* CSS line-height unit calculations now cover all root font relative units
  (rem, rlh, rex, rch, rcap, ric), element font relative units (lh, ex, ch,
  cap, ic), viewport percentage units (vh, vw, vmin, vmax, vi, vb, svh, svw,
  lvh, lvw, dvh, dvw), and container query units (cqw, cqh, cqi, cqb, cqmin,
  cqmax) across text layout and painting.
* DOMTokenList (classList) optimizes add and remove operations to skip
  redundant attribute re-serializations and DOM mutation dispatches when the
  underlying token set is unchanged.
* AbortSignal spec compliance: AbortSignal.abort() and AbortSignal.timeout()
  generate standard DOMException instances (AbortError, TimeoutError),
  AbortSignal.any() accepts any iterable signal collection, and prototype
  chains correctly inherit from EventTarget.
* Norwegian regional Accept-Language configuration adds complete fallbacks
  across Bokmål (nb), Nynorsk (nn), and generic Norwegian (no) locales.
* Parser and layout loop safety improvements: bounded counter formatting
  buffers, guaranteed forward pointer progress in pseudo content resolution,
  unified depth-bounded ns_node_root tree traversals, and cycle-protected
  document order comparisons.
* Selection highlights the text it is actually on. The highlight was
  painted as one flat pass over the finished page, in document
  coordinates, after everything else had been drawn — so inside a
  scrolling box it landed wherever the text would have been unscrolled,
  spilled past the box it belonged to, and ignored the transforms and
  clips the glyphs themselves were drawn under. It is now drawn where the
  text is drawn, from a range table computed once per frame and looked up
  per box, so it inherits that box's scroll offset, transform and clip by
  construction. It also goes down before the glyphs rather than over them,
  which is what lets `::selection` carry an opaque background without
  washing the letters out. Selection hit-testing gained the two things the
  click path already had: it adds a scrolling ancestor's scroll offset as
  it descends, so a click inside a scrolled `<div>` selects the line under
  the pointer instead of the line that would be there at scroll zero, and
  it stops at a box that clips its children, so a point below an
  `overflow: hidden` container no longer reaches the content clipped out
  of it.
* `user-select` and `::selection` are read from where the style is. Both
  were looked up on the inline box's own `style`, which inline boxes do
  not carry — the style lives on an ancestor — so the pointer was always
  null and both properties were silently ignored: `user-select: none` text
  copied anyway, and a page's `::selection` colours never appeared. Both
  now walk to the nearest ancestor that has a style, the same way the text
  painter finds its font.
* Copied text reads like the page. Every inline box ended with a newline,
  so a paragraph broken into runs by a `<b>` or an inline-block came out
  one word per line; a `<br>` — which layout carries as U+2028 — came out
  as nothing at all. A line break is now emitted where a block boundary
  is, U+2028 and U+2029 become newlines, and the zero-width characters
  `<wbr>` leaves behind are dropped. Text under `user-select: none` is no
  longer collected at the ends of a range, only in the middle of one.
* Double-click selects a word, triple-click selects the block. Both
  gestures previously did what a single click did. Word edges come from
  the shaper's own break attributes rather than an ASCII rule, so they
  hold for scripts that do not put spaces between words. Shift-click
  extends the existing selection from its anchor instead of dropping it,
  and a drag that selected text no longer activates the link it ended on —
  releasing the mouse after selecting a sentence containing a link used to
  navigate away.
* A page can see and set the selection it is showing. `getSelection()`
  reported an empty, collapsed selection no matter what was selected on
  screen, because nothing ever told the JavaScript engine what the
  selection was; `document.execCommand` answered false to everything and
  `navigator.clipboard.write` was absent. The page selection now flows
  into the engine on every change, so `toString()`, `type`,
  `isCollapsed`, `rangeCount` and the range's `getBoundingClientRect()`
  describe the real one; `execCommand` performs `copy`, `cut`,
  `selectAll` and `unselect`, with `queryCommandSupported` and
  `queryCommandEnabled` agreeing about them; and `clipboard.writeText`
  and `clipboard.write` reach the system clipboard by way of an
  `X-Clipboard` flag on the render response, which the shell answers with
  a `/clipboard` fetch from the renderer. Reading the clipboard stays
  refused — there is no permission prompt behind which to put it.
* The heading of `about:nordstjernen` names the browser's version and
  nothing else. It carried the JavaScript engine's version beside it, which
  told a reader looking for the browser's own version that there were two
  numbers to choose between; QuickJS is still listed under *Version &
  libraries* further down the same page, next to lexbor, Pango, SQLite and
  the rest, which is where a reader goes looking for it.
* The heading drops its tagline too. *The unique, legendary web browser*
  sat between the version and the sentence that says what the browser is,
  making a reader read past an advertisement to reach the description.
* A custom property can say what it holds. `@property` was parsed for its
  `inherits` and `initial-value` descriptors and nothing else: the `syntax`
  descriptor was read past, so the one thing the rule exists to declare —
  the grammar its value has to match — was never checked, and
  `CSS.registerProperty` was absent entirely, which is how most of the
  libraries that use registered properties reach for them. Both now go
  through one grammar: `src/css_prop_syntax.c` parses a `<syntax>` string
  into its alternatives and multipliers and matches a value against it,
  including the arithmetic, so `calc(7in - 12px)` is a `<length>` and
  `calc(5px + 10%)` is not, and so is the computational-independence rule
  that makes `10em` a legal length in a stylesheet but not as an initial
  value. A rule missing `syntax` or `inherits`, or carrying an
  `initial-value` its own syntax rejects, is now dropped rather than half
  honoured, and a declaration whose value does not match the registered
  syntax falls back to the initial or inherited value instead of being
  taken at face value. `CSS.registerProperty` throws the errors the API is
  specified to throw — `SyntaxError` for a name, syntax or initial value it
  cannot accept, `InvalidModificationError` for a second registration — and
  the CSSOM grew `CSSPropertyRule`, so `name`, `syntax`, `inherits` and
  `initialValue` read back off the rule.
* Viewport units inside a frame measure that frame. `vw`, `vh`, `vmin`,
  `vmax` and their small/large/dynamic spellings resolved against the
  top-level window wherever they appeared, so a 200x100 iframe laid its
  `100vw` box out at the width of the whole browser. The cascade now
  swaps in the frame's own viewport while it walks a nested document,
  the way the media-query evaluation already did, and only when the
  frame's size is actually known — from its `width`/`height` attributes,
  an inline size, or the last layout — so a frame whose size has not been
  measured yet keeps the behaviour it had rather than guessing at
  300x150. `data/fixtures` aside, a 200x100 `<iframe>` whose content asks
  for `100vw` by `50vh` now lays that box out at 200x50.
* A registered custom property computes its value instead of carrying the
  text it was written with. `<length>` arrives in pixels whatever unit it
  was authored in and whatever the element's font size is, a
  `<length-percentage>` that mixes the two serializes as the `calc()` the
  CSSOM specifies, `<angle>` lands in degrees, `<time>` in seconds,
  `<resolution>` in `dppx`, `<integer>` rounded, `<color>` as `rgb()` with
  `currentcolor` resolved against the element's own colour, `<string>`
  requoted, and a `<transform-function>` with its arguments computed the
  same way. The computation runs once the element's font metrics are
  known, so `--x: 14em` on a ten-pixel element is `140px` and an inherited
  value keeps the number its parent computed.
* A property registered through `CSS.registerProperty` now restyles the
  page. Incremental restyle skips a pass when no stylesheet has changed,
  and a registration changes no stylesheet, so a property registered from
  script had no effect until something else happened to dirty the tree.
  The registration is part of the signature that decides whether the pass
  can be skipped.
* `@counter-style` rules with a name no counter style may take — `none`, a
  CSS-wide keyword, or one of the six predefined styles the spec forbids
  overriding — are dropped instead of entering the stylesheet, and
  `CSSCounterStyleRule` reports its `name`.
* The CSS tokenizer decides what starts an identifier the way the Syntax
  specification does. A `-` was treated as the beginning of a name whatever
  followed it, so the subtraction in `calc(7in - 12px)` tokenized as an
  identifier rather than an operator; a hyphen now only starts a name when
  a name character, a second hyphen or an escape follows it, and the same
  rule governs the unit after a number and the name after `@`.
* The page itself snaps. `scroll-snap-type` worked on scroll containers
  only, which left out the arrangement almost every page that asks for
  snapping actually uses: full-height sections down the document, with the
  property on `html` or `body` and nothing overflowing in between. The
  document scroller is the one scroller that is not a box — the shell owns
  its offsets in a `GtkAdjustment`, and the renderer only learns them as the
  coordinates it is asked to paint from — so the snap positions its
  descendants offer were never consulted. The solver no longer derives the
  snapport from the scrolling box: it takes one, so the viewport can supply
  its own, and the renderer resolves the proposed offset against the root
  element's `scroll-snap-type` on the way into a frame and returns the
  snapped one to the shell on the render response, the channel
  `scrollIntoView` and fragment navigation already use to move a tab's
  scroll position. Every source of document scrolling therefore snaps — the
  wheel, the scrollbar, the keyboard — because each ends in a frame rendered
  at a new offset, and it works the same in process-per-tab and
  `--single-process` because both drive the same renderer service.
  `scroll-padding` on the root insets the viewport snapport as it insets a
  box's, `mandatory` and `proximity` keep their meanings, and the horizontal
  axis now rides back beside the vertical one. `html` is the root element
  and `body` is honoured as a source too, matching how the root's `overflow`
  is already read here. Verified on
  `data/render-tests/scroll-snap-viewport.html`, four sections of `100vh` in
  a 953-pixel viewport: proposed offsets of 100, 600 and 1400 resolve to
  953, 2000 to 1906 and anything past the end to 2859, while
  `scroll-snap.html`, whose snapping is all inside boxes, and the ordinary
  layout pages resolve to no snap and scroll exactly as before.
* `Ctrl+P` prints in the default process-per-tab mode. Printing paginates
  in the renderer and drew onto cairo recording surfaces, which cross no
  process boundary, so every window that was not started with
  `--single-process` — which is every window, by default — answered
  *Nothing to print*. The renderer now rasterises each finished sheet and
  writes it into the runtime directory the way the page export already
  hands a file across, and the shell loads the sheets back and feeds them
  to the same `GtkPrintOperation` as before, dividing out the scale they
  were rendered at. Pagination, `@media print`, `@page` and the `break-*`
  properties are the single code path they always were; single-process
  printing still hands the recording surfaces over untouched, so it keeps
  printing vectors.

1.0.23:
======
* The browser prints. `Ctrl+P`, or *Print…* in the menu, lays the page out
  for paper and hands the sheets to the operating system's own print
  dialog through `GtkPrintOperation` — CUPS on Linux, the Win32 printer
  dialog on Windows, the Cocoa panel on macOS — so no printing code is
  written per platform and no dependency is added. The sheets are cairo
  recording surfaces, which cross no process boundary, so the print
  action needs the in-process renderer: it works under
  `--single-process` (or `NS_SINGLE_PROCESS=1`) and reports *Nothing to
  print* otherwise. `--dump=print:FILE` renders the same pagination to a
  multi-page PDF from any mode and needs no printer.
* The engine gained the parts of CSS a printer needs. `@media print` now
  matches — the media type was hardcoded to `screen`, so a page's print
  stylesheet was simply ignored. `@page` sets the sheet size from a name
  (`A4`, `letter`, `legal`, `ledger`, the A/B series), from one or two
  lengths, or from `portrait`/`landscape`, along with its margins.
  `break-before`, `break-after` and `break-inside` — with the legacy
  `page-break-*` spellings mapping onto them and `always` becoming `page`
  — decide where a sheet may end. A sheet is cut at a forced break when
  one falls before the page is full; otherwise the cut is pulled up above
  any box it would split, which is every leaf box, every line of a
  paragraph, and anything asking for `break-inside: avoid`. Printing
  restores the on-screen layout afterwards.
  `data/render-tests/print-pagination.html` comes out as three A4 sheets
  with every card whole, the `@media print` paragraph swapped in for the
  screen one, and the forced break starting sheet three.
* `offsetLeft` and `offsetTop` are measured from the offsetParent again.
  Both returned a document coordinate, built from the margin box rather
  than the border box, so an element inside any positioned ancestor
  reported where it sat on the page instead of where it sat in its
  parent. CSSOM View asks for the distance from the offsetParent's
  padding edge, with a statically positioned `body` or root the exception
  every engine makes. A great many pages measure this way, and so does
  the `checkLayout` harness most of WPT's layout tests are written
  against — in the sibling GPL edition, where this was measured, fixing
  it plus the flex change below took `css/css-flexbox` from 653 to 1437
  of the same 3535 subtests.
* An absolutely positioned child of a flex container is placed where the
  flexbox specification says. It landed at the container's content-box
  origin whatever the container asked for; CSS Flexbox 4.1 gives it a
  static position from `justify-content` and its own `align-self`, as
  though it were the only flex item, and `flex-direction: *-reverse`,
  `flex-wrap: wrap-reverse` and `direction: rtl` each turn around the
  axis they govern. Vertical writing modes are not covered — flex layout
  itself is horizontal-only here.
* `flex-wrap: wrap-reverse` puts the first line last. Lines wrapped, but
  the cross axis was never turned around, so the first line stayed at the
  top and `align-content: flex-start` stayed at the top with it. The
  lines are now mirrored within the container after `align-content` has
  placed them, and each item within its line, which reverses
  `align-items: flex-start`/`flex-end` along with them.
* CSS Scroll Snap. `scroll-snap-type` on a scroll container, with
  `scroll-snap-align` on the things inside it, moves the container onto
  the nearest snap position once a scroll lands — from the wheel, and
  from `scrollTop`/`scrollLeft`. `scroll-padding` on the container and
  `scroll-margin` on an item inset the snapport and outset the snap area,
  both as shorthands and per side; `mandatory` always snaps, `proximity`
  only from within half a page. A wheel tick shorter than the gap between
  two snap positions still moves the reader forward rather than falling
  back to the one behind. `data/render-tests/scroll-snap.html` walks both
  axes. This is scroll containers only: the document scroller belongs to
  the window, not to a box, so `scroll-snap-type` on `html` or `body`
  does nothing yet.
* A regular expression whose `v`-flag class contains the empty string,
  `/[\q{}]/v`, no longer writes outside the string set it is building.
  The JavaScript engine is refreshed onto quickjs-ng 0.16.1, which
  carries that fix along with the iterator proposals — `Iterator.concat`,
  `Iterator.prototype.join`, `includes`, and the chunking proposal's
  `chunks` and `windows`, with `take` and `drop` now rejecting
  out-of-range limits — resizable externally managed ArrayBuffers, and a
  parser that no longer rescans the line to report an identifier's
  column.
* A processing instruction is parsed as one. `<?target data?>` produced a
  comment that the engine then took apart again by hand, guessing where
  the target ended; the HTML parser now implements the specification's
  own rules, so the node carries a real target and data, a target the
  specification disallows stays a comment, and serializing one writes
  `<?target data>` instead of dropping the target on the floor — which
  the XML parser's processing instructions had been doing all along.
  Two URL fixes come with the same refresh: a caret in a path is
  percent-encoded, and a URL that has userinfo but no host is rejected.
* Two paragraphs measuring the same words come out the same width. The
  text shaper cached a piece ending in a space together with the kerning
  the following word had induced on it, so "Type of" was laid out narrow
  after "Type A" had been drawn, and which paragraph came out wrong
  depended on what the process had already done. The shaper now asks
  HarfBuzz which pieces are safe to store.
* The bundled MP3 decoder and the WebGPU headers match their upstreams
  again — the headers move to wgpu-native v29.0.1.1, the release the
  build actually downloads.
* The third-party notices name every library in the binary. pl_mpeg and
  minimp3 are compiled into it and were listed nowhere; the wgpu-native
  headers were missing too; lexbor's `NOTICE` file, which Apache 2.0
  requires be propagated, was recorded as not existing.
* The build instructions install what the build requires. The
  Debian/Ubuntu, Fedora and openSUSE package lines omitted FFmpeg, which
  `meson setup` has refused to proceed without on Linux since inline WebM
  landed, so following them exactly produced a failing configure.
* The documentation stops pointing at files that are not there:
  `src/mobile.c`, `src/tab_worker.c`, `src/env.c`, `src/media.c` and
  `docs/ipc-http-experiment.md` were all cited by name. The mobile-site
  note described a per-host list the browser does not have — the choice
  is made once for the whole build — and the threading model still
  documented a per-tab worker thread that the move to process-per-tab
  removed.
* Every link to the project's own repository points at
  nordstjernen-web/nordstjernen-browser, its new home: the README badges
  and release-tag link, the AppStream metainfo, the Debian, RPM and Alpine
  packaging, the Java POM and Gradle metadata, and the build documentation.
  The Alpine recipe also follows the archive root GitHub names after the
  repository rather than the package, so the tarball it fetches for 1.0.22
  unpacks where the build looks for it, with a checksum to match.
* A distribution package built from a release tarball declares the FFmpeg
  libav\* libraries it needs. meson has required them on Linux and Windows
  since inline WebM landed, but the Debian control file, the OBS and Fedora
  spec files, the Alpine APKBUILD and the source-RPM recipe all still
  described them as optional, so each of those builds failed at configure
  time on a clean machine. The nightly container build no longer treats a
  missing FFmpeg as a reason to carry on either, and the Linux and Windows
  build guides list the packages.
* A package built where the build host has no network reaches the system
  Pango. ns-pango is cloned by meson at setup time, which an OBS worker, an
  sbuild chroot, a mock root and Alpine's build phase all forbid, so those
  recipes now pass -Dns-pango=disabled and build-depend on Pango itself.
* A source RPM carries a version rpm accepts and installs what it built.
  The generated spec spelled a development version with the hyphen rpm
  rejects, and hand-installed two of the four binaries with none of the
  runtime data, so the browser it packaged could not start a renderer or
  find its translations.
* The Debian tree has the changelog dpkg-buildpackage needs, and its rules
  file configures the build the way the packaging documentation says.
* The documentation index lists the architecture, iOS, extensions,
  vendored-engine and wpt-fast documents that were missing from it, and no
  longer points at two documents that are not there. The OBS packaging notes
  no longer describe a _service file the repository does not carry, and the
  HTML compatibility table describes what actually happens when an
  undecodable media element is clicked.
* The Android, iOS and Java hosts compile again. Each of them builds its
  bridge against the engine's headers with nothing on the include path but
  `src/`, and printing put `<glib.h>` and `print.h` into three of those
  headers — `libnordstjernen.h`, which is the one the engine installs,
  `renderer_serve.h` and `rproc_http.h`. `print.h` reaches on to cairo, the
  CSS engine and the layout tree, none of which those builds can see, so
  the Java bridge, the Android bridge and the Swift bridging header all
  stopped finding what they included while the engine itself, which has the
  full include path, kept building. Every print entry point moves to
  `print.h` beside the pagination it drives, and the three headers are
  self-contained again.
* The Android bridge attaches its in-process renderer again: the callback
  that hands one over began returning the connection the print path needs
  rather than a status code, and Android's still returned the status code,
  which its compiler rejects outright.
* The NetBSD build no longer rests on one package mirror. It took whatever
  `pkg_add` defaults to, which is `ftp.netbsd.org` over plain HTTP, and when
  that host refused connections every dependency failed to install and the
  job died before a compiler ran. It now names the CDN the NetBSD sets
  already come from first and that host second, and retries, so one mirror
  being down is no longer the end of the build.

1.0.22:
======
* A mouse or pointer event carries the window it was dispatched in.
  `UIEvent.view` is that window and every event the engine synthesised
  for a click, a drag or a hover reported null, which is a value no
  browser produces.
* A grid item placed by area name is aligned to its row. Items placed
  through `grid-template-areas` went down a layout path that never read
  `align-items` or `align-self`: each was put at the top of its row at its
  own height, where the default is to stretch. On lichess.org the lobby's
  start-button column stayed 179 pixels tall beside a 600-pixel
  neighbour, and the player counts pinned to its bottom edge came to rest
  on top of the buttons.
* A single flex line is as tall as the container says. A row flex
  container with a definite height has a line exactly that tall, and a
  stretched item gets that height whether its content fits or not. The
  line was sized to the taller of the container and its content instead,
  so one over-tall item dragged the whole line past the height the author
  asked for.
* A percentage inside `min()`, `max()` and `clamp()` is measured against
  the box rather than the viewport. These functions were folded to a
  single pixel value during parsing, when the only basis available was
  the viewport width, so the comparison ran against the wrong number: in
  a 400-pixel column `min(300px, 50%)` came out 300 instead of 200.
* A dialog opened from script renders, and a modal one is centred.
  `showModal()` and `show()` set the open attribute without telling the
  style engine, so the element kept the `display: none` it was matched
  with at parse time and had no box at all. The user-agent sheet now also
  carries the modal rule the HTML specification defines, and an
  out-of-flow box asking for an intrinsic height is no longer stretched
  between its top and bottom offsets.
* A grid track can be measured in any length unit. `grid-template-columns`
  understood px, %, fr, em and rem, and quietly dropped every track it
  could not read, which moved each remaining track one place to the left.
  lichess.org asks for five columns with a `1vw` margin at either end;
  three arrived, its `<main>` landed in a column with no room in it, and
  the whole site laid out zero pixels wide down a 26000-pixel page. Track
  lengths now go through the same reader as every other length, and a
  track list that still cannot be read is discarded whole rather than
  closed up, because a missing list leaves the columns to
  `grid-template-areas` while a shifted one leaves nothing standing.
* A positioned box answers the pointer in the layer it paints in. Hit
  testing ranked a box against its own siblings and nothing else, so a
  fixed, high `z-index` overlay never rose above content in another branch
  of the page even though the painter drew it on top: an overlay's button
  was visible and the content behind it took the click. Positioned boxes
  are now collected and tried in the order the painter flushes them, and a
  modal dialog in the top layer is tried before the document.
* An `<svg>` that paints nothing hands the pointer to what is under it.
  SVG hit-tests as `visiblePainted` -- a shape answers where it draws and
  nowhere else -- but the engine lays an `<svg>` out as one replaced box
  and let that box answer for its whole rectangle. chess.com stretches an
  `<svg>` of rank and file labels over the board, so a piece could be
  picked up and never put down: the labels swallowed the pointerup that
  ends the drag.
* An SVG `font-size` attribute survives the cascade. A presentation
  attribute is author style at the very bottom of the cascade, so a real
  declaration beats it but inheritance must not; the renderer read the
  attribute and then overwrote it from the computed style, which always
  has a font-size because font-size is inherited. Every `<text>` drew at
  the page's font size scaled by the viewBox -- chess.com's board
  coordinates came out four times their size and spilled across the board.
* A worker shares the storage of the page that started it. The worker
  runtime was built without a storage partition, and IndexedDB reads that
  partition to find the origin's databases, so every `indexedDB` call
  inside a worker threw "Storage is unavailable" and chess.com reported
  its opening database as unusable.
* `prefers-color-scheme` answers with the scheme the desktop is actually
  using. Nothing set it at all, so every window reported "light" however
  dark the desktop was: a site's dark stylesheet never applied. The shell
  judges the scheme by the luminance of the foreground colour the theme
  resolves for its window -- which holds for any theme, rather than only
  the ones that set GTK 3's `gtk-application-prefer-dark-theme` -- honours
  an explicit `color_scheme` setting over it, hands the answer to each
  renderer it starts, and re-evaluates when the theme changes underneath a
  running window. The internal pages -- start, about, settings, history and
  the error page -- gain the dark half they never had, and stop declaring
  themselves light-only.
* Text is laid out through three new ns-pango caches: the unicode break
  attributes, the items a paragraph was cut into, and shaping keyed on a
  word rather than on a run. Intrinsic sizing means the same paragraph is
  laid out for min-content, for max-content, for the real width and again
  to paint it, and the line breaker cuts a run wherever a line ends and
  shapes the piece again -- so a paragraph shared no cache entry even with
  itself. On this repository's own test page the shape cache now serves
  1512 lookups against 67 misses, the break cache 312 against 24 and the
  item cache 277 against 63. The layout dump is byte-identical to the one
  the previous pin produced.
* The status line behaves like a status line. It held a permanent row under
  the page saying "Done" for the life of every visit, and hovering a link
  wrote the URL there while moving off it wrote nothing -- so the last link
  the pointer touched stayed on screen indefinitely, naming a destination
  the cursor had left. It now floats over the bottom-left corner of the
  page, appears only while it has something to say, and clears when the
  pointer leaves a link or a load finishes. Notices the window raises
  itself -- a bookmark added, a session recovered -- fade after five
  seconds.
* The toolbar menu is grouped into tab, view, page and tool sections and
  gains Zoom In/Out/Reset, Full Screen, History and the two save entries;
  zoom, full screen and history had keyboard shortcuts but no visible
  affordance, and saving a page was reachable only by right-clicking it.
  Items whose action carries more than one accelerator name one explicitly,
  because GTK shows nothing when a shortcut is ambiguous. A popover menu
  also takes the height its items need: GtkPopoverMenu builds a section's
  separator after the popover has negotiated its size, so the last item was
  always clipped.
* The page zoom is shown beside the address bar while the page is scaled,
  and resets when clicked. Zooming said "Zoom 121%" in the status line for a
  moment and then left no trace, so a window could sit at any magnification
  with nothing on screen admitting it -- and 121% is where successive tenths
  land. The steps now follow the usual ladder: 90, 100, 110, 125, 150.
* The bookmark button says whether the page is bookmarked, carrying a hollow
  star that fills once the page is on the list, and the popover's action
  becomes "Remove this bookmark" where it would otherwise do nothing --
  adding a URL already on the list is a no-op, so pressing it a second time
  silently did nothing.
* Escape in the address bar reverts it to the URL the window is showing.
  It restored focus to the page but left whatever had been typed sitting in
  the bar, naming a page that was not on screen. Escape also closes the find
  bar, which its own tooltip already promised, and stops a load in progress.
  The connection indicator moves inside the entry, where the padlock
  belongs, and the title bar no longer reads "Nordstjernen 1.0.22 —
  Nordstjernen 1.0.22" on a page with no title of its own.
* A text field shows as much of its value as it has room for. An `<input>`
  was given a visible window of exactly as many characters as its `size`
  attribute names, and CSS that widened the control -- `flex-grow`,
  `width: 100%` -- only stretched the painted frame, so a field with room
  for sixty characters still scrolled its text away after twenty. The
  window now comes from the used content width: it grows when the box is
  wider than `size` asks for, and shrinks when a definite CSS width is
  narrower, where the value used to be painted straight through the
  control's own border.
* An inline-block, inline-flex or inline-grid sits on the line's baseline.
  Two things put it elsewhere: the shape rect handed to the shaper aligned
  the box's bottom margin edge to the baseline -- only the fallback CSS 2.1
  gives a box with no in-flow line boxes of its own -- and the placement
  pass then ignored the shaper's answer and pinned the box to the top of
  the line. A badge or button written inline with a sentence was drawn with
  its own text floating above the words either side of it, and the line box
  grew to cover the overshoot.
* A `#fragment` stays anchored while the rest of the page loads. The scroll
  position was computed once, from whatever layout existed at navigation
  time, so images decoding above the target pushed it down afterwards and
  the view landed short of the heading it was asked for. The target is held
  and its position re-applied until the document goes quiet or the reader
  scrolls away.
* A navigation that ends with nothing to render gets an error page whatever
  its scheme. Only `https://` failures had one, so a missing `file://` path
  came back 404 with an empty body and rendered as a blank white page --
  no heading, no URL, nothing to act on. The classifier has also stopped
  blaming the network for everything it does not recognise: an unmatched
  transport message falls through to the status code, and a file URL is
  described as a file rather than as an unreachable server.
* Resolving an `ex`, `ch`, `cap` or `ic` length no longer shapes four probe
  glyphs every time. The metrics oracle built a layout and measured `x`,
  `H`, `0` and the water ideograph on each call, and the cascade asks once
  per element a rule matches, so a stylesheet that sizes fields in `ch`
  paid for four layouts on every one of them. The answer depends on nothing
  but the family, size, weight and slant, so it is measured once per font
  and kept until the font map changes under it.
* An IndexedDB write no longer walks the origin's whole storage directory.
  Every `put` recomputed the origin's quota by opening each `.sqlite` file
  beside the current one, asking it for `page_count` and closing it again --
  a directory scan and a fresh SQLite connection per record written, inside
  the write transaction. A page that stores a burst of records stalled the
  browser in the filesystem for as long as the burst lasted: starting a game
  on chess.com left the main thread inside `CreateFile` and it never came
  back. Cache the siblings' total for five seconds; the current database is
  still measured live, so the limit is enforced as before.
* `document.styleSheets` includes the sheets a page links to, with their
  `href` and their rules. Only inline `<style>` blocks had a populated
  `CSSStyleSheet`; a `<link rel=stylesheet>` produced one with a null href
  and an empty `cssRules`, because the sheet was built from the element's
  own text content and a link has none. nrk.no went from 13 reachable rules
  to 1895. The engine keeps a reference to the CSS it already fetched for
  the cascade, so nothing is downloaded or stored twice.
* `getComputedStyle(el).cssFloat` reports the used float. The accessor read
  the declaration block directly rather than going through whichever
  `getPropertyValue` the object carries, so on a computed style -- which has
  its own -- it always came back as the empty string, while the equivalent
  `getPropertyValue('float')` answered correctly.
* Instantiating a module through the `WebAssembly` JS API runs the module's
  start section and nothing else. WAMR, built for a standalone runtime, also
  called `_initialize`, `__wasm_call_ctors` and `__post_instantiate` from
  inside `wasm_runtime_instantiate` -- but on the web those are ordinary
  exports the JS glue calls itself, after it has pointed its heap views at
  the instance's memory. Running them first meant an Emscripten module tore
  down on its own first WASI call: chess.com's analysis engine died in
  `environ_sizes_get` before `new WebAssembly.Instance` had returned.
* A finished keyframe animation keeps the value `animation-fill-mode`
  says it should. The engine sampled the last keyframe correctly, then threw
  the sample away: every getter the painter calls required the animation to
  still be running, so at the moment it ended the box snapped back to its
  specified value. The common `opacity: 0` plus a `fade ... forwards`
  animation therefore faded in and vanished again within one frame, and the
  content stayed invisible for the life of the page -- chess.com's bot
  gallery, which is exactly that pattern, was a set of empty boxes.
* `AbortSignal` is an interface object, not a bare namespace. It was a plain
  object carrying `abort()`, `timeout()` and `any()`, and the signals an
  `AbortController` hands out did not inherit from it, so `signal instanceof
  AbortSignal` -- the guard every fetch wrapper writes -- threw "invalid
  'instanceof' right operand" instead of answering. chess.com's RPC client
  turned that TypeError into a 500 and never issued its first request.
  `MessagePort` gains the same treatment in the window: it existed only in
  workers, so ports came back with no prototype at all.
* A grid container's max-content width is the sum of its columns, not the
  width of its widest item. Anything that shrink-wraps a grid -- a float, a
  table cell, an inline-grid, `width: max-content` -- was sized as if the
  columns were stacked, so the tracks overflowed the box they were given.
  bbc.com's "LIVE" flag is a floated two-column grid, and the headline
  beside it started inside the flag rather than after it.
* Flex and grid items measure their intrinsic sizes in the font they will
  actually be drawn in. The base size of a flex item and the min-content
  floor of a flex or grid item were measured against the item's own style
  rather than the style its text inherits, so text in a web font was sized
  by the fallback face. An item then got a base size a pixel or two under
  what the real font needs and wrapped mid-phrase however much room the
  container had -- dn.no's nav pills broke "DN Helg" and "DN i VM" across
  two lines inside a box wide enough for either.
* An absolutely positioned box with `width: auto` gets the shrink-to-fit
  width CSS 2.1 asks for -- its max-content size clamped to the available
  space and floored at min-content -- measured by shaping the text. It used
  to be guessed from a character count at 0.65em each, so every tooltip,
  dropdown, badge and popover came out at a width unrelated to its
  contents: a seven-character label was sized 81px where the text needs 63.
* Event-listener objects follow the Web IDL callback-interface algorithm.
  `handleEvent` is looked up for every dispatch, non-callable values and
  throwing getters are reported as uncaught listener exceptions, and generic
  `EventTarget` objects no longer discard object listeners. The focused DOM
  event test moves from three passing subtests out of six to all six.
* Checkbox and radio activation keeps the state required by HTML's legacy
  pre-activation and canceled-activation steps. `indeterminate` is a real
  cloned input state, a canceled radio click restores the previously checked
  group member, synthetic `click()` events are untrusted and cannot recurse on
  the same element, and the resulting `input` and `change` events are not
  cancelable. Three focused input tests move from 52/80 to 80/80 subtests.
* `CSSStyleSheet.insertRule()` and `deleteRule()` enforce their required
  arguments, while the deprecated but web-visible `addRule()` and
  `removeRule()` methods mutate both constructed and document sheets. The
  CSSStyleSheet interface test moves from 8/17 to 17/17 subtests.
* A worker scope gets the same JavaScript platform the page does.
  Workers were built from a hand-kept list of C-side globals and never
  ran the polyfill bundle, so `indexedDB`, `Headers`, `Blob`,
  `AbortController`, `AbortSignal`, `ReadableStream`, `WritableStream`,
  `TransformStream` and `caches` were all absent inside one. The bundle
  now runs in the worker too, up to the end of the IndexedDB section and
  no further -- everything past that point is DOM and window surface a
  worker must not have. Of the 27 globals a worker is expected to carry,
  14 were missing and 3 are: `FileReader`, `WebSocket` and nested
  `Worker`. chess.com's play page opened its database inside a worker
  and threw on the first line.
* `min-content` and `max-content` grid tracks size to their content
  instead of stretching like `auto`. A box with a definite width now
  contributes that width to min-content, an intrinsic track is measured
  against the space left once `minmax()` tracks are at their minimums
  rather than their maximums, a `min-content` track is never scaled below
  its content, and a grid container's own min-content is the sum of its
  columns rather than its widest child. Together these stop a nested grid
  -- chess.com's board -- from overflowing the panel beside it.
* An SVG with a `viewBox` but no width or height is sized the way CSS
  says: its ratio fitted inside the 300x150 default object size. It was
  rasterised into a square, so the artwork was letterboxed and then
  stretched into the page's box; Wikipedia's wordmark came out a smear.
* A declaration that uses `var()` no longer overrides the declarations
  that follow it. Such a declaration is held back until custom properties
  are known, and it was then re-inserted after every plain declaration in
  its block rather than at the place it was written, so it won every
  conflict with a later one. `color: var(--c); color: green` computed
  blue, `background: var(--c); background-color: red` computed blue, and
  `margin: var(--w); margin-left: 40px` kept the shorthand's margin. The
  New York Times sets `* { outline: var(--size) solid var(--accent);
  outline-color: #0000 }` -- an outline on every element that is
  transparent until something takes focus -- so the front page was drawn
  as a grid of blue boxes, one around every element on it. A held-back
  declaration now keeps its position in its block.
* A regexp search skips the positions that cannot start a match. A
  pattern without the sticky flag is compiled with a `.*?` prologue, so
  the matcher was re-entered at every index of the subject: `/^zebra/`
  walked all 880KB of a string to fail at the first assertion 880,000
  times. The search now reads what a match must begin with -- a start
  anchor, a single character, or a character class turned into a
  256-bit table -- and skips ahead with `memchr` or a table probe
  instead, the way V8's Irregexp does. A failing literal search over
  880KB drops from 5.6ms to 0.25ms, a leading character class from
  10.8ms to 0.35ms, a case-insensitive literal from 8.5ms to 0.85ms,
  and an anchored pattern from 6.1ms to nothing. Ten thousand
  `test()` calls that miss on a short string fall from 200ms to 2.2ms.
  A pattern that starts with an alternation is not covered and still
  runs as before. Verified by differential fuzzing: 40,000 random
  pattern/subject/flag combinations produce byte-identical `exec`,
  `replace`, `split` and `search` results before and after.
* The local `\p{RGI_Emoji}` tables are gone. Upstream quickjs-ng has
  since grown the general properties-of-strings machinery, which covers
  `RGI_Emoji` along with `Basic_Emoji` and the flag, tag, ZWJ, modifier
  and keycap sequences, and composes with the `v` flag's set operations
  -- so the vendored Emoji 17.0 sequence data, its generator and the
  local emit path were dropped for it.
* Grid items can be placed on named lines. A line named in the track
  list was parsed as a track, rejected, and dropped, so `grid-column:
  main` resolved to nothing and the item was auto placed, which collapses
  the layout of any page that names its lines. Names are now recorded and
  resolved, an area called `foo` also defines `foo-start` and `foo-end`,
  an end line repeating the start's name means the next line with that
  name, and an item with a column but no row keeps its column. chess.com's
  play page draws its board again.
* Headless `--viewport` takes `WIDTHxHEIGHT`. It read an integer and
  insisted the argument ended there, so `--viewport=1280x900` was dropped
  without a word and the page laid out at the default width. Anything it
  cannot read is now an error rather than silence.
* A page on an origin that does not speak QUIC no longer stalls for the
  whole connect timeout. Whenever libcurl was built with HTTP/3, every
  request asked for it, so the first hop to an origin that silently drops
  UDP on 443 waited out the 15-second navigation connect timeout (6 for a
  subresource) and returned a timeout rather than falling back. The
  timeout also counted as a connection failure, which parked the host in
  the unreachable cache for two minutes and failed every subsequent
  request to it -- so acid3.acidtests.org took 15 seconds to answer and
  then lost all of its subresources. Requests now ask for HTTP/2 and are
  upgraded to HTTP/3 by the alt-svc cache, the way an origin advertises
  it; `NS_FORCE_HTTP3=1` still asks for HTTP/3 outright. Acid3 loads in
  2.2 seconds instead of 15.5, and three seconds in it has run 65 of its
  tests rather than 12.
* Table cells centre their content vertically again. A cell with no
  `vertical-align` of its own fell back to the initial `baseline`, so in
  a row taller than the cell's own line the text sat at the top. Every
  browser's user-agent sheet gives cells `middle`, which is what pages
  written as tables expect. Cells now default to `middle`, and an author
  rule or a `valign` attribute still overrides it.
* An image is drawn inside its own borders, padding and margin. The
  painter placed the bitmap at the box's margin-box origin and gave it
  the content size, so a bordered image covered its own top and left
  borders and a margin shifted the picture instead of the box. Replaced
  content now starts where the content box starts, the way inline SVG
  and MathML already did, and the placeholder, alt text and drop shadow
  follow it.
* `OfflineAudioContext` renders audio instead of silence. Every `create*`
  method returned the same generic node, `connect()` recorded no edge,
  `start()` and `stop()` did nothing, `AudioBuffer` could not hold
  samples -- `getChannelData` minted a fresh zeroed array on every call --
  and `startRendering()` resolved a buffer of zeros. Nodes now carry their
  kind, the graph is recorded, buffers keep one array per channel, and
  `src/webaudio.c` renders oscillators, gain, a dynamics compressor, the
  RBJ biquad types, delay, wave shaping, constant sources and buffer
  playback. Rendering is mono, summed into every channel, and `AudioParam`
  automation is not applied. The context, nodes and buffers also brand
  themselves for `Object.prototype.toString`.
* The toolbar reads by colour again: back and forward green, reload blue,
  and a red stop button between reload and home that appears only while a
  page is loading. Stop marks the in-flight frame stale, ends the loading
  state and drops the busy cursor; it does not abort the network request.
  The title bar is shorter, the home button is set off from the address
  bar, and the security shield is drawn smaller than the buttons.
* `about:nordstjernen` lists the user agent, resolved the way a request
  resolves it.
* The start page is titled "Home" rather than "Nordstjernen", so its tab
  and window title say what the page is.
* The embedded ns-pango build no longer asks for link-time optimization.
  The rest of the tree links without LTO, so a clang build on Windows
  archived the fork as LLVM bitcode that the mingw linker could not read
  ("archive has no index" / "file format not recognized"). The subproject
  keeps `-O3` and the release `NDEBUG`.
* The CSSOM rule interfaces carry their real names and classes. Every
  interface the polyfill synthesises reported `name: "ctor"`, because the
  constructor was an anonymous function expression, and `@import` and
  `@keyframes` were plain `CSSRule` objects even though their `type` said
  3 and 7, so `instanceof` and `Object.prototype.toString` disagreed with
  `type`. They now get `CSSImportRule` and `CSSKeyframesRule`, `@namespace`
  and `@counter-style` get theirs, and the grouping at-rules that were all
  lumped under `CSSGroupingRule` get `CSSContainerRule`,
  `CSSLayerBlockRule` and `CSSScopeRule`.

Java
----
* The URL bar warned "Not encrypted" on every HTTPS page. Both the Java and
  the Android shell numbered the engine's transport-security states
  themselves and got them backwards -- 1 is a validated chain, not plain
  HTTP -- so a valid certificate showed the warning and an untrusted one
  showed the lock. They now follow `ns_security` and tell an untrusted
  certificate apart from an unencrypted connection.
* `java/pom.xml` builds the library with Maven: the same sources, the same
  manifest, and the jar, sources and javadoc artifacts, plus the metadata a
  public repository needs and a `release` profile that signs them and
  publishes to the Maven Central portal. `build.gradle` grows the matching
  `maven-publish` block, so `gradle publishToMavenLocal` and `mvn install`
  produce interchangeable artifacts, and it reads its version back out of
  `pom.xml` so the number lives in one place.
* `RemoteBrowser` spoke about a third of the renderer's control protocol. It
  now covers transport security and the server address, the live page size
  the render headers carry, the scroll position a page asks for (anchors,
  `scrollTo`, focus), camera prompts, window actions, overflow scrolling,
  in-page scrollbar dragging, `contextmenu` delivery, file drops, page dumps,
  the focused editable, idle ticks, caret blinking, and back/forward-cache
  traversal. `RemotePage` grows the `text`, `links`, `linkAt`, `dump`, `eval`
  and `renderToFile` it always claimed to mirror from `Page`, and `links`
  becomes a `/dump` kind so it has an endpoint to reach.
* Both clients had their own copy of a JSON reader that found a key anywhere
  in the document, including inside a string value. They now share one that
  only matches a quoted token immediately followed by a colon. Frames convert
  the renderer's BGRA to the raster's ARGB in one bulk copy rather than a
  per-pixel loop.
* The Swing browser uses all of it: a lock or warning beside the URL, wheel
  notches offered to the scroller under the pointer before the page,
  draggable in-page scrollbars, pages that draw their own context menu,
  camera prompts, page-source and layout/network/performance inspectors,
  History and Settings entries, files dropped onto the page, a blinking
  caret, and several windows (`Ctrl+N`, or `Ctrl+Shift+N` for a private one
  whose renderer keeps no cookies, cache or history) each with its own
  renderer process.

Android
-------
* The app had a back stack but no forward, no find-in-page, and no way to
  select page text -- and it silently dropped the WebGL and camera
  permission requests the engine raised, so a page asking for either got
  neither a prompt nor an answer. The JNI bridge now carries find, selection,
  favicons, transport security, both permission prompts and their
  resolutions, media resolution, PDF export, `contextmenu` delivery, `eval`,
  the scroll position a page asks for, and viewport changes; it also stops
  leaking the camera, audio and window-action strings on every rendered
  frame.
* The shell grows a forward button and a real history list that survives
  process death, a find bar, long-press-to-select with a copy/share action
  bar, a lock or warning at the head of the URL bar, and back/forward that
  reuses the renderer's back/forward cache instead of refetching.
* Rotation no longer refetches the page: the open document is re-laid out at
  the new viewport, so scripts, form state and the reading position survive
  turning the device.
* Platform integration: sharing a page or a selection, pull-to-refresh,
  printing through the system print service (Android's Save as PDF), pinning
  a page to the launcher, static app shortcuts, handling shared text,
  `WEB_SEARCH` and `PROCESS_TEXT` intents, and handing media the engine
  cannot play inline to another app.

CSS
---
* `prefers-color-scheme` and `prefers-reduced-motion` were hardcoded to
  `light` and `no-preference` with no way to change them.
  `ns_browser_set_color_scheme` and `ns_browser_set_reduced_motion` let an
  embedder mirror the platform's theme and animation settings into the
  cascade; the Android shell follows the system dark theme and the "remove
  animations" accessibility switch through them.

Text layout
-----------
* Desktop builds shape text through ns-pango, a fork of Pango carried as a
  meson subproject, instead of the system Pango. Pango keeps no cache that
  outlives a `PangoLayout`, so the same bytes were shaped by HarfBuzz once to
  measure an inline run and again to paint it, and a table cell was shaped
  for `min-content`, for `max-content` and once more to lay out. The fork
  caches finished glyph strings process-wide -- keyed on the font, bidi
  level, gravity, script, language, analysis and show flags, text transform,
  OpenType features and the item bytes -- and caches
  `pango_context_get_metrics` per font description, which resolving
  `line-height: normal` asks for on every inline run. On a table-heavy page
  the cache serves 92% of shaping requests and cuts layout time 24%; a
  text-heavy page falls 13%. Every symbol in the fork is renamed, because
  GTK loads the system Pango into the same process and GObject aborts when
  two libraries register the same type name. Android and iOS keep the system
  Pango, which has the backends they need, and `src/ns_pango_names.h` maps
  the renamed API back for them. Rendering is unchanged: the fixture smoke
  set matches its baselines and a corpus covering RTL and bidi, CJK, the
  white-space modes, intrinsic sizing, spacing, tabs, ellipsis, columns,
  inline atomics, decorations and font features renders byte-identically on
  both paths.

1.0.21:
======

Media and images
* The vendored pl_mpeg no longer reads past a frame plane. Half-pel motion
  compensation samples `s[si + 1]`, `s[si + dw]` and `s[si + dw + 1]`, but
  `plm_video_process_macroblock` bounds only `s[si]`, so a macroblock on
  the bottom row reads up to one row plus one byte beyond the plane it
  samples -- absorbed by the next plane for interior planes, and off the
  end of the allocation for the last one. This is reachable from page
  content: `ns_video_player_new` hands an MPEG-1 `<video>` body to
  `plm_create_with_memory`, and `ns_video_backend_next` decodes it. Fuzzing
  the decoder under AddressSanitizer with mutated streams reported it as a
  heap-buffer overflow read. The three frames are allocated as one chunk,
  which is now padded by that overshoot and zeroed, so the read stays
  inside the allocation and a corrupt stream decodes deterministically.
  Valid video is unaffected: no bound is tightened, so no macroblock that
  decoded before is rejected now.
* Animated images decode as animations on the engine's own fetch path.
  `ns_image_decode_body` routed GIF, APNG and animated WebP to the
  animation decoder, but the two fetch handlers in `engine.c` -- the ones
  headless rendering and the browser's own image pass use -- called
  `ns_image_decode_bytes` instead, which only ever returns a still frame.
  An animated image fetched through those paths therefore froze on frame
  one. Both now go through `ns_image_cache_insert_encoded`, and the
  still-versus-animated decision lives in one function rather than three
  copies that had already drifted.

Media capture and WebRTC
* `MediaStream` and `MediaStreamTrack` are constructors, and the streams
  and tracks `getUserMedia` hands back are instances of them, so
  `stream instanceof MediaStream` holds and `new MediaStream([track])`
  works. They were plain object literals with the right methods, which
  passes a duck-typing check and fails everything else.
* `RTCSessionDescription` and `RTCIceCandidate` exist. Signalling code
  wraps the objects it receives before handing them to the peer
  connection, so their absence stopped a session at the first offer.
  `RTCIceCandidate` rejects an initialiser carrying neither `sdpMid` nor
  `sdpMLineIndex`, as the specification requires.
* `RTCRtpSender`, `RTCRtpReceiver` and `RTCRtpTransceiver` exist, with
  `getCapabilities` on the two that define it.
* `RTCPeerConnection` gains `addTrack`, `removeTrack`, `addTransceiver`,
  `getConfiguration`, `setConfiguration`, `restartIce` and the
  `generateCertificate` static, and `getSenders`, `getReceivers` and
  `getTransceivers` return what was added to the connection instead of
  always returning an empty array.
  This is API surface: there is still no ICE agent, no DTLS and no SRTP,
  so a connection never leaves the `new` state. What changes is that
  feature detection and object construction no longer throw partway
  through a page's setup code.

Adaptive streaming
* HLS and DASH play. `data/js/streaming.js` adopts any `<video>` or
  `<audio>` whose source is an `.m3u8` or `.mpd` -- by extension or by
  `type` -- parses the manifest, picks a rendition, and feeds segments
  through Media Source Extensions, which is how the sites that use these
  formats already expect to be served. HLS covers master and media
  playlists, `EXT-X-MAP` initialisation segments, `EXT-X-BYTERANGE`,
  separate `EXT-X-MEDIA` audio renditions, and live playlists, which are
  re-fetched on the target-duration cadence and merged by media sequence.
  DASH covers `SegmentTemplate` with either `SegmentTimeline` or a fixed
  segment duration, `$Number$`/`$Time$`/`$RepresentationID$`/`$Bandwidth$`
  substitution with `%0Nd` padding, `BaseURL`, and separate audio and
  video adaptation sets. Rendition choice prefers the highest bandwidth at
  1080p or below among the codecs the build can actually decode. The
  player keeps roughly thirty seconds buffered ahead of the playhead and,
  on `QuotaExceededError`, evicts everything more than ten seconds behind
  it and retries rather than giving up.
* `video/mp2t` is an accepted Media Source type. Browsers reject it
  because their Media Source pipelines take fragmented MP4 and WebM only,
  which is why HLS players written in JavaScript transmux MPEG-TS before
  appending. Appended bytes here go to libavformat, which demuxes
  transport streams natively, so the transmuxing step is wasted work and
  segments can be handed over as they arrive.

Media Source Extensions
* `navigator.mediaCapabilities.decodingInfo()` answered from a hardcoded
  substring list -- WebM, VP8, VP9, Opus and WAV -- so it reported
  `supported: false` for every MP4 and AAC configuration even though
  `canPlayType` reported `probably` for the same string. Adaptive players
  ask `decodingInfo` which rendition to fetch, so a browser that denies
  H.264 there selects nothing and never starts. Container and codec
  support now resolve through one shared table that `canPlayType`,
  `decodingInfo` and `MediaSource.isTypeSupported` all consult, and a
  configuration is supported only when every stream in it is -- audio and
  video both, not whichever one was inspected first.
* `MediaSource.isTypeSupported` is a native call rather than a
  round-trip through `document.createElement('video').canPlayType`, and
  answers are memoised. Adaptive players probe it hundreds of times while
  building the format ladder; each probe used to allocate an element.
  It also answers for the segmented containers only, as the specification
  requires, instead of inheriting `canPlayType`'s whole-file container
  list.
* A single failed `appendBuffer` no longer wedges a `SourceBuffer` for
  the rest of the page's life. Any native rejection set a `_quotaFull`
  latch that made every later append throw `QuotaExceededError`, and the
  latch cleared only on a successful `remove()`. Quota is now checked
  against the buffer's real byte count before the append is queued, so it
  throws `QuotaExceededError` synchronously the way the specification
  says and the way players expect when they run their eviction path; a
  genuine decode failure runs the append-error steps instead, ending the
  media source. A zero-length append is a no-op rather than an error.
* `SourceBuffer` reports `audioTracks`, `videoTracks` and `textTracks`,
  and `AudioTrackList`, `VideoTrackList` and `TextTrackList` exist as
  constructors.
* `MediaSource.readyState`, `sourceBuffers` and `activeSourceBuffers`,
  and `SourceBuffer.updating`, moved from per-instance properties to
  prototype accessors, where feature detection looks for them.
* `MediaSourceHandle`, `MediaSource.prototype.handle`,
  `MediaSource.canConstructInDedicatedWorker`, `ManagedMediaSource` and
  `ManagedSourceBuffer` are present.
* AC-3, E-AC-3, FLAC, ALAC, MP3-in-MP4 (`mp4a.69`, `mp4a.6b`) and the
  AAC object types beyond `mp4a.40` resolve to their decoders, and
  `video/quicktime`, `audio/aac`, `audio/flac` and `audio/wav` are
  recognised containers.
* The headless renderer builds a video cache, wires the Media Source
  callbacks, ticks the cache from the settle loop and forwards media
  events back to the document, so appended segments reach the demuxer
  there instead of every `appendBuffer` failing for want of a callback,
  and `video.buffered` reports the real demuxed range. Media Source
  behaviour is now reproducible from `--headless`.

Images and graphics
* Animated PNG plays. Wuffs already decoded APNG frames and the
  animation loop is format-agnostic, but the callers only routed GIF
  magic to it and the animation decoder itself hardcoded the GIF
  signature check and the GIF decoder, so a PNG was rejected inside the
  function meant to decode it. Both now use the same format detection
  the still path uses. An APNG is recognised as the spec defines it, by
  an `acTL` chunk before the first `IDAT`, so a still PNG never pays for
  the animation decoder. The decode-pipeline documentation had listed
  APNG as supported already; it is now accurate.
* The vendored Wuffs moves to v0.4.0-alpha.10, nine months newer than
  the alpha.9 the tree carried. The release adds the VP8 decoder, so
  lossy WebP -- the common case on the web -- now decodes through the
  memory-safe path instead of libwebp. `MODULE__VP8` was already set in
  the subproject's build flags, where it had been a no-op because the
  module did not exist in alpha.9. libwebp and libwebpdemux are left
  serving animated WebP and nothing else.
* gdk-pixbuf no longer decodes page images. Every format the web
  actually uses is already handled in-tree -- ICO, then Wuffs for PNG,
  GIF, BMP and JPEG, then libwebp, then libavif, then the in-engine SVG
  renderer -- so the pixbuf fallback had been reduced to TIFF, TGA, PPM
  and ICNS, none of which Chrome or Firefox render either. What it cost
  was the ability to know what parses untrusted bytes:
  `gdk_pixbuf_get_formats` enumerates loader plugins installed on the
  user's machine, so the set of decoders reachable from a web page was
  decided at runtime, varied per system, and could not be audited from
  the build. The decode chain now ends after SVG: an unsupported format
  fails to decode instead of falling through to a plugin. GTK 4 still
  depends on gdk-pixbuf for its icon theme, so a desktop build links it
  either way -- what goes away is the browser feeding it. The mobile
  builds, which never had it, are unaffected.
  `ns_image_pixbuf_supports_mime` is renamed `ns_image_supports_mime`.
* libavif is optional on the desktop builds too. It was a hard
  `dependency()` off the mobile path, so a desktop tree without it would
  not configure at all, even though every AVIF call site already sat
  behind `NS_HAVE_AVIF` and `image_avif.c` was already compiled
  conditionally. The new `avif` meson feature defaults to `auto`, so a
  host that has libavif is unchanged; `-Davif=disabled` drops it and
  AVIF images fail to decode like any other unsupported format. libavif
  pulls in a complete AV1 decoder for a format that is rare on the web.
  The README listed libavif as both required and optional; it is now
  listed once, as optional.
* `var()` resolves inside SVG presentation attributes. A custom property
  set by a stylesheet rule now reaches `r="var(--radii)"` or
  `fill="var(--tint)"`, so a class can retheme an inline icon's colour
  and geometry the way it does for ordinary CSS properties.
* `mask` is honoured on SVG elements. The referenced `<mask>` renders to
  an offscreen surface whose sRGB luminance becomes the alpha the
  element is composited through, so a white mask shows the element,
  black hides it, and a gradient fades it. Group opacity and masking
  combine.
* `marker-start`, `marker-mid` and `marker-end` draw their `<marker>` on
  path, line, polyline and polygon vertices. Vertices and their tangents
  come from the built Cairo path, so arcs and curves orient the same way
  straight segments do, and a mid vertex uses the bisector of its two
  tangents. `markerUnits="strokeWidth"` scales the marker with the
  stroke, `orient="auto"` and `auto-start-reverse` rotate it, and
  `refX`/`refY` are mapped through the marker's own `viewBox` before
  positioning.
* `vector-effect: non-scaling-stroke` keeps a stroke's width in device
  space instead of scaling it with the current transform.
* SVG is rendered by the engine instead of librsvg. `src/svg.c` walks
  the SVG DOM and paints it through the same Cairo surface, cascade and
  font stack that HTML uses, and `librsvg` is gone from the dependency
  list, the packaging manifests and the CI images. Inline `<svg>` was
  previously re-serialised to XML and handed to librsvg as an opaque
  raster, so the document's own stylesheet could never reach inside it:
  `fill: currentColor`, `svg .icon { fill: … }` and script-driven
  geometry changes were invisible. SVG elements now take part in the
  normal cascade, so `fill`, `stroke`, `stroke-width`,
  `stroke-dasharray`, `fill-rule`, `stop-color`, `text-anchor`,
  `paint-order` and the SVG geometry properties `x`, `y`, `cx`, `cy`,
  `r`, `rx`, `ry` are real CSS properties that inherit like the rest.
  Covered: paths including elliptical arcs and smooth-curve
  continuation, rect/circle/ellipse/line/polyline/polygon, `viewBox`
  and `preserveAspectRatio`, nested `<svg>`, `<g>`, `<use>`,
  `<symbol>`, `<switch>`, `<defs>`, linear and radial gradients with
  `href` inheritance, `spreadMethod`, `gradientUnits` and
  `gradientTransform`, `clipPath`, group opacity, dashing, and `<text>`
  shaped through Pango. A standalone `.svg` document sizes to the
  viewport rather than to a 300x150 default. Android and iOS, which
  dropped librsvg with the rest of the desktop stack, gain SVG for the
  first time.

CSS
* `text-decoration-color` reaches the painted line. The colour was only
  read from the *block's* style, never from the inline run that carries
  the decoration, so an underline set on an `<a>` was always drawn in
  the text colour -- and a fully transparent one, the idiom behind every
  "underline grows in on hover" teaser, was drawn as a solid line. On
  Tidens Krav and the other Amedia fronts that put an underline under
  every headline and nav link. The decoration attributes now carry the
  style of the element that turned them on, so
  `text-decoration-color: #e00` paints red, and a decoration whose
  resolved colour is fully transparent is not emitted at all. A
  decoration propagated from an ancestor still paints in the ancestor's
  colour, as the spec requires.
* A flex item that is itself a flex or grid container re-aligns its own
  children after the cross-axis stretch resizes it. The item laid its
  children out at its content height, and the stretch then overwrote
  that height in place without a second pass, so anything the item
  centred or bottom-aligned stayed where the pre-stretch height had put
  it. VG's masthead is the shape that shows it: a 56px-tall `header`
  flex row, a logo link inside it that is a flex container with
  `align-items: center`, and a 24px logo that rendered flush against the
  top of the bar instead of centred on it. The column-flex path already
  re-ran layout for a resized item; the row and wrapped-row paths now do
  the same, and only when the stretch actually changed the height.
* A `container-type: inline-size` (or `size`) element no longer sizes
  itself from its own contents. CSS Contain 3 gives such an element
  inline-size containment, so its intrinsic inline sizes are computed as
  if it had no children; the engine measured the children anyway. On a
  page whose container queries feed back into the container -- headlines
  sized in `cqw`, the pattern VG, Aftenposten and the other Schibsted
  fronts use -- that closed a loop: wide contents made the container
  measure wide, `cqw` then resolved against the inflated width and made
  the contents wider still. VG's lead teaser laid out 1245px wide inside
  a 734px column and its headline computed to 276px where the site asks
  for 157px. The three intrinsic-width paths (`measure_natural_width`,
  `measure_min_width`, `estimate_natural_width`) now return zero content
  contribution for such a box, so an explicit `width` still wins and a
  flex or grid item shrinks to the space its parent gives it.
* A square border is painted inside its border box rather than centred
  on the edge. Each side was stroked along the border-box boundary with
  the line width set to the border width, and Cairo centres a stroke on
  its path, so every bordered element rendered half a border wider than
  it laid out on each side -- a 4px border occupied 6..9 and 60..63
  where the box model puts it at 8..11 and 58..61. Layout was always
  right; only the paint was wrong, so borders overlapped whatever sat
  next to them. Rounded borders and border-image already inset
  correctly and are unchanged.
* An `<iframe>` becomes visible as soon as its document loads, on a
  quiet page as well as a busy one. The UA sheet hides frames until the
  engine stamps `data-nd-frame-loaded` on them, but that stamp is
  written by the loader rather than through the scripted attribute
  path, so it never invalidated style. The frame kept the cached
  `display: none` and produced no box at all — its document parsed and
  its scripts ran, entirely unpainted — until some unrelated mutation
  happened to force a restyle. Pages with continuous script activity
  masked it; a page whose only content was a frame never showed it. The
  three places that add or remove the attribute now mark it dirty.
* Inline atomic boxes contribute their full height to the individual
  wrapped line that contains them. Multi-line form controls and table
  cells now reserve the correct vertical space instead of allowing later
  lines to overlap following content, fixing the Google footer position.
* Inherited properties set on the root element reach the rest of the
  page. The UA stylesheet declared `color`, `font-family`, `font-size`
  and `line-height` on `html, body` together, and a UA declaration on
  `body` outranks inheritance from `html` — so a page styling only
  `html` (`html{font-family:"Helvetica Neue","Segoe UI",Arial,
  sans-serif}` on lite.duckduckgo.com) had its font, colour and size
  dropped at `body` and rendered in the UA serif default. The
  declarations now sit on `html` alone and `body` inherits them.
* A concrete font family is used when the system actually has it.
  `Arial`, `Helvetica`, `Segoe UI`, `Roboto` and the SF Pro names were
  rewritten to generic `sans-serif` unconditionally, which resolved
  through fontconfig to whatever the default sans happened to be —
  Noto Sans rather than the requested Segoe UI or Arial. Each name is
  now resolved against the installed families first, and substituted
  by `sans-serif` only when it is missing.
* `calc()` serializes per CSS Values 4 instead of being echoed back as
  authored. A typed math sum — one coefficient per unit, sitting beside
  the px/pct/em/rem value layout uses — sums terms of the same type,
  folds absolute lengths, angles, times, frequencies and resolutions to
  their canonical unit, and distributes products and quotients by a
  number. A sum that cannot reduce to a single term serializes sorted:
  number, then percentage, then dimensions in ASCII-alphabetical unit
  order. `calc(1px + 1%)` is `calc(1% + 1px)`,
  `calc(1px + 2em + 3rem + 4%)` is `calc(4% + 2em + 1px + 3rem)`,
  `calc(2 * (1px + 1em))` is `calc(2em + 2px)`, and a single-argument
  `min()`/`max()` reduces to `calc()`. The quad shorthands serialize the
  same sum rather than dropping every term but px, so
  `margin: calc(1px + 1em) 2px` no longer reads back as `1px 2px`.
  Comparisons that need layout, such as `min(20px, 10%)`, still stay as
  authored.
* `border-image` is implemented (CSS Backgrounds 3): the five longhands
  (`border-image-source`/`-slice`/`-width`/`-outset`/`-repeat`), the
  `border-image` shorthand and its `-webkit-` alias parse, cascade,
  serialize canonically and reach `getComputedStyle`; the `border`
  shorthand resets them. The painter nine-slices the source — raster
  `url()` images and gradients alike — honouring `fill`, percentage and
  number slices, `auto`/length/percentage/number widths, outsets and all
  four `stretch`/`repeat`/`round`/`space` tiling modes, and replaces the
  element's border style while it renders.
* Media Queries Level 4: the heuristic matcher is replaced by a real
  evaluation engine (`src/css_media.c`) with grammar-complete parsing
  (range syntax, boolean context, nested conditions, `and`/`or`/`not`,
  general-enclosed), Kleene three-valued logic and CSSOM media-list
  serialization (`not all` for unparseable queries). Iframe documents
  evaluate against their own viewport, `matchMedia` and
  `CSSMediaRule.media` expose the serialized form, `CSSMediaRule.media`
  is a real `MediaList`, and changing a `media` attribute restyles.
* The native `sheet` / `document.styleSheets` stubs that shadowed the
  real CSSOM are gone. `style.sheet.cssRules` now returns the parsed
  rule tree and `style.sheet === document.styleSheets[i]` holds.
* A script read of a resolved value flushes every pending mutation, not
  just the first one in the task. `getComputedStyle`,
  `getBoundingClientRect`, `offsetWidth`, `scrollIntoView` and the rest
  force a synchronous reflow whenever the document is dirty; the
  wall-clock interval and the oscillation dampener now apply only to the
  rendering tick, where they belong. Mutating a style and reading it
  back in the same task returns the new value, and `:has()`
  invalidation, inset resolution and stylesheet insertion are observable
  immediately.
  Two gaps this makes visible, which the frozen styles had been hiding:
  `scrollWidth`/`scrollHeight` are wrong on inline-level boxes
  (`inline-block`, `inline-flex`, `inline-grid`), and `attr()` is
  substituted when generated content is rendered but not when
  `getComputedStyle().content` is serialized.
* Cascade layers are ordered as a tree rather than by first-declaration
  order across the whole document: sublayers sort inside their parent,
  a layer's own declarations act as its implicit final sublayer, and
  nested anonymous layers stay nested instead of escaping to the top
  level.
* The incremental restyle pass identifies stylesheets by a parse-time
  serial instead of by address. A reparsed `<style>` reusing the freed
  block of the sheet it replaced used to look unchanged, which froze the
  page at stale styles.
* `@scope` preludes are parsed against the grammar and invalid ones drop
  the rule; the prelude is serialized canonically.
* `StyleSheet.media` is a live `MediaList` that writes back to the
  owner node's `media` attribute, and `ShadowRoot.styleSheets` is empty
  for a disconnected tree.
* CSS Display Level 3: `display` is a structured computed value — outer
  type, inner type, list-item flag and layout-internal kind — resolved
  once in the cascade instead of a keyword string that layout, paint and
  the CSSOM each re-read with `strcmp`. Multi-word canonical forms now
  reach layout, so `display: flow-root list-item` keeps its box instead
  of losing it; blockification of floated and absolutely positioned
  boxes has one implementation rather than three; `-webkit-box` and
  `-webkit-inline-box` map to flex and inline-flex.
* Anonymous table boxes are generated around any run of table-internal
  siblings, per CSS 2.1 17.2.1, so `display: table-row` and
  `display: table-row-group` outside a table lay out as tables instead
  of collapsing into the surrounding inline content.
* CSSOM: declaration blocks are canonicalized, rule mutations apply
  synchronously, constructed stylesheets are backed by live rules,
  at-rules are exposed on declarations, and shorthand serialization
  covers the quad shorthands, `all` and the complete shorthand
  families.
* `getComputedStyle` resolved values: insets absolutize against the
  correct containing block, static insets follow the writing mode,
  automatic minimum sizes resolve, and pseudo-element styles compute.
* Declaration grammar is enforced: values with unbalanced brackets or
  quotes, stray `!` or `;`, out-of-place `auto`/`normal`, negative
  values on non-negative properties, or transform functions with bad
  units are dropped instead of being half-parsed.
* `content-visibility: hidden` applies size containment, and
  `writing-mode: vertical-rl/lr` with `text-orientation`
  `upright`/`mixed`/`sideways` measures and paints vertical inline runs.
* Flexbox honours the automatic (content-based) minimum size, and
  column stretch no longer double-counts item margins.
* Media queries inside a frame evaluate against the frame's own size,
  not a 300x150 guess. The viewport pushed while collecting a frame's
  stylesheets came from the frame's inline `style` attribute or its
  `width`/`height` content attributes, so a frame sized by a stylesheet
  rule -- `iframe { width: 100% }`, the common responsive-embed pattern --
  was measured as 300x150 and its `@media (min-width: ...)` blocks
  resolved against a size the frame never had. Layout now records each
  frame's content box, collection prefers it over the default, and when
  the recorded size disagrees with the one a viewport-dependent frame
  sheet was collected under, style and layout run once more so the frame
  settles on its real size. Frames whose CSS carries no width, height,
  aspect-ratio or orientation query never trigger the extra pass. Acid3
  goes from 98/100 to 99/100.
* A frame document's own stylesheet can style its root element. Sheets
  inside an iframe are rewritten to be scoped to the frame's root, and
  every selector whose subject was not literally `html` or `:root` got a
  descendant combinator — so `* { … }` or `.cls { … }` in a framed
  document matched everything inside the frame except the frame's own
  `<html>`, and `getComputedStyle` on that element reported no value for
  any property. The scope marker now also attaches directly to the
  subject compound, and lands before a pseudo-element rather than after
  it. Shadow scopes are unchanged: a shadow host is still not styled by
  its own shadow tree. Acid3 goes from 97/100 to 98/100.
* `getComputedStyle(el).someUnknownName` is `undefined` rather than the
  empty string. The proxy in front of a computed declaration answered
  every string key through `getPropertyValue`; its `has` trap already
  distinguished supported properties from unknown ones, and `get` now
  draws the same line. jQuery's `css()` returns
  `computed.getPropertyValue(name) || computed[name]` and expects
  `undefined` for a property the engine does not know.

Scripting
* Removed the IE-only `attachEvent` and `detachEvent`. They were exposed
  on Element, Document and Window as no-op stubs that returned true and
  registered nothing. Libraries still feature-detect them to select a
  legacy path: RequireJS, finding a native-looking `attachEvent`, bound
  its script-load callback to `onreadystatechange` instead of
  `addEventListener`, the stub swallowed it, and every module load ended
  in "Load timeout for modules". jQuery's test suite could not get past
  its RequireJS bootstrap before this.
* `DOMParser` reports the line and column of an XML parse error. The
  synthesized `parsererror` document carried the bare text "XML parsing
  error"; it now names the position the parser stopped at.

Scripting
* `Intl.DateTimeFormat` names months and weekdays in the requested
  language, and picks the clock the locale actually uses. The month and
  weekday tables held English names only and the hour cycle defaulted to
  12-hour whatever the locale, so `new Date().toLocaleTimeString()` on a
  Norwegian desktop read "2:47:00 PM" instead of "14:47:00", and
  `toLocaleDateString('nb-NO', {weekday: 'long', month: 'long'})` read
  "Tuesday, July 28" instead of "tirsdag 28. juli". Every Nordic news
  front page shows a formatted date, so this was visible on all of them
  -- Aftonbladet's masthead read "TUESDAY, JULY 28, 2026". Month and
  weekday names are now carried for the fourteen languages whose date
  *patterns* the formatter already knew (the Nordic five plus German,
  Dutch, French, Spanish, Italian, Portuguese, Polish and Russian);
  `short` and `narrow` are derived from the long name by UTF-8-safe
  truncation rather than byte truncation, which previously cut a
  multi-byte name mid-character. The 12-hour default is now restricted
  to the locales that use one, `en` (outside GB/IE/ZA) and a dozen
  others; everything else formats h23. Swedish and Lithuanian numeric
  dates serialize in ISO order (`2026-07-28`), and the day-month-year
  languages get their own literals -- the ordinal period in Norwegian,
  Danish, German, Finnish and Icelandic, ` de ` in Spanish and
  Portuguese -- instead of the English comma layout. An explicit
  `hour12`/`hourCycle` option still wins, and `en-US` output is
  unchanged.

Networking
* A top-level navigation follows a redirect that leaves HTTPS. Any
  redirect off `https://` was refused outright, which is the right rule
  for a subresource -- that is mixed content -- but not for a document
  the user asked for. Sunnmørsposten is the common shape: `smp.no`
  answers 302 to `http://www.smp.no/`, whose server immediately sends
  302 back to `https://www.smp.no/`, so the whole site was unreachable
  and rendered as "That address looks malformed". Navigations now follow
  the hop and report the resulting scheme in the security indicator;
  subresource fetches are still blocked exactly as before.
* HTTP/3 can receive a response larger than a megabyte. nghttp3's
  `nghttp3_conn_read_stream()` returns the bytes it consumed *excluding* the
  DATA frame payload — the application is required to extend QUIC's stream
  and connection flow-control credit for the body itself, from the
  `recv_data` callback. The backend extended only for what nghttp3 reported,
  so credit for body bytes was never returned: every HTTP/3 transfer
  deadlocked the moment it reached the 1 MB
  `initial_max_stream_data_bidi_local` advertised at connection setup, and
  sat there until the request timeout expired 30 seconds later. A 10.7 MB
  script that HTTP/2 fetched in 140 ms failed outright over HTTP/3; it now
  completes.
* HTTP/3 resolves the origin over IPv6 as well as IPv4. The QUIC socket
  asked `getaddrinfo` for `AF_INET` only and used the first result, so on an
  IPv6-only network every HTTP/3 hop failed to connect and fell back to
  HTTP/2. It now asks for `AF_UNSPEC` and tries each address in turn, the
  way the TCP path does.
* A UDP socket that reports `EAGAIN` no longer fails the HTTP/3 request. The
  QUIC socket is non-blocking, so a full send buffer is an ordinary
  condition under load; it was treated as a fatal write error and abandoned
  the connection. The datagram is now dropped and left to QUIC loss
  recovery, which is what it is for.
* HTTP/3 loss-recovery and idle timers fire while packets are arriving.
  `ngtcp2_conn_handle_expiry()` was called only when the poll timed out, so
  a connection with steady inbound traffic never processed an expired PTO or
  ACK timer. It is now called every iteration, which is a no-op when nothing
  is due.
* The nghttp2 backend reads the final response of a request that begins
  with an informational one. A `103 Early Hints` — what Cloudflare, Fastly
  and Shopify send ahead of the real response — arrives as a first HEADERS
  block, and libnghttp2 categorises the *final* HEADERS that follows as
  `NGHTTP2_HCAT_HEADERS` rather than `NGHTTP2_HCAT_RESPONSE`, the same
  category it gives trailers. The header callback accepted only
  `HCAT_RESPONSE`, so the page kept the interim status and every real
  header was discarded: no `Content-Type` (an HTML document rendered as
  plain text, its source visible), no `Content-Encoding` (a gzip body
  handed to the sink still compressed), no `Set-Cookie`. The callback now
  admits the block that follows an informational response and drops the
  interim headers instead of the final ones; trailers are still ignored.
  The HTTP/1.1 fallback had the same defect and worse — it treated the
  blank line ending the interim response as the end of all headers, so the
  entire second response, status line included, became the body. It now
  skips interim blocks and parses the response after them.
* A timed-out or cancelled HTTP/2 stream is detached from its session.
  `ns_h2_io_scan_timeouts` sent RST_STREAM and released the request, which
  lives on the requesting thread's stack, but left the session's
  `stream_user_data` pointing at it. DATA or HEADERS already in flight for
  that stream — the ordinary case, since RST_STREAM races a response — then
  drove the header and body callbacks through a dangling pointer and wrote
  into a returned stack frame. Streams are now detached with
  `nghttp2_session_set_stream_user_data()` before the request is released.
* HTTP/2 downloads are no longer capped by the default connection-level
  flow-control window. The session advertised an 8 MB
  `SETTINGS_INITIAL_WINDOW_SIZE`, but per RFC 9113 §6.9.2 that setting
  governs streams only: the connection window stayed at the protocol
  default of 65535 bytes, which throttles *aggregate* throughput on a
  connection to one window per round trip — about 640 KB/s at 100 ms RTT no
  matter how many streams are multiplexed over it. The connection window is
  now raised to match with `nghttp2_session_set_local_window_size()`.
* A request the server refused is retried on a fresh connection. When a
  pooled connection goes away, libnghttp2 closes the streams the server
  never processed with `NGHTTP2_REFUSED_STREAM` — the code exists precisely
  so the request can be sent again — but the retry only covered streams
  that had not been submitted, so a subresource lost this race and failed
  outright instead of being refetched.
* Idle HTTP/2 connections are closed. The pool defined an idle timeout, a
  reuse ceiling and a per-origin cap and enforced none of them: every
  origin visited kept a connection, its TLS state, three file descriptors
  and a live I/O thread polling four times a second until the browser
  exited. A connection with no streams for a minute now stops its I/O
  thread, and the pool drops connections that are dead, idle-expired, over
  the reuse ceiling or beyond the per-origin cap.
* The nghttp2 backend uses the `nghttp2_ssize` API on the versions that
  have it. Upstream deprecated the `ssize_t`-based entry points in favour
  of `…2` variants in 1.60.0 and lets an application compile the old ones
  out entirely with `NGHTTP2_NO_SSIZE_T`; the backend now defines that
  macro and calls `nghttp2_submit_request2()`,
  `nghttp2_session_mem_recv2()` and
  `nghttp2_session_callbacks_set_send_callback2()` when the headers are new
  enough, keeping the deprecated names only as the fallback for older
  libnghttp2. This is what a toolchain without `ssize_t` needs, and it
  makes a future upstream removal a non-event.
* An informational response no longer contributes headers to the HTTP/3
  response that follows it, matching the HTTP/2 and HTTP/1.1 paths.
* The request identity a fetch coalesces and preloads on no longer depends
  on the order its `Accept`-style headers happen to be listed in; the header
  lines are sorted into the key.
* `Vary: Origin` no longer defeats the HTTP cache. `Origin` is now one of
  the headers the cache can resolve at lookup time: `net.c` computes the
  value it will send once and uses that same string both as the request
  header and as the cache selector, so the two can never disagree, and
  the absence of an `Origin` selects distinctly from any present one.
  Google serves its stylesheets `public, immutable, max-age=31536000`
  with `Vary: Origin`; those were being refetched on every load and are
  now cached.
* ES module fetches join the same request identity as every other
  subresource. The module loader passed no top-level URL, so a module was
  partitioned in the HTTP cache under its own site rather than the
  document's — two unrelated sites importing the same module shared one
  cache entry — and it neither coalesced with nor consumed the preload
  issued for the same `<script type=module src>`, since that preload
  carries the JavaScript `Accept` and the module fetch did not. It now
  passes the document URL and the script `Accept`.
* Shutting down no longer hangs a caller waiting on a coalesced fetch.
  `ns_net_drain` discarded queued fetch tasks without telling the
  coalescer, so a task that led a group left the group behind: blocking
  joiners waited on a condition nobody would signal again, and
  asynchronous joiners never had their callback run. A blocking joiner
  now also gives up when the network layer starts aborting, and in any
  case five seconds past the longest transfer timeout a leader can have,
  instead of waiting without a bound. These waits happen on worker
  threads that teardown joins, so one that never returned took the
  joining thread down with it.
* The HTTP cache selects the right variant of a negotiated response.
  `cache.c` keyed entries on URL and partition and stored nothing about
  `Vary`, so a resource served `Vary: Accept` and referenced both as a
  stylesheet and as a script was fetched once and that single variant
  handed to both — a `<script>` element could receive CSS. Entries now
  carry the response's `Vary` and are keyed on a selector built from the
  request headers it names, with an indexed base key so a lookup can walk
  the variants stored for a URL and match the right one. `Accept`,
  `Accept-Language` and `User-Agent` are resolved; `Accept-Encoding` is
  ignored because bodies are stored decoded, which keeps the web's most
  common `Vary` from fragmenting the cache; anything else, including
  `Vary: *`, is not stored rather than stored wrongly. The preload scan
  deduplicates candidates on (URL, destination) instead of URL alone, so
  both variants are preloaded. The cache schema is versioned through
  `PRAGMA user_version` and an upgrade discards the old cache.
* The speculative preloader hands its bytes to the loader that needs
  them through a single deduplication point keyed on the request's
  identity. Preload responses used to be parked in a private store
  keyed on the bare URL and consulted ahead of the HTTP cache. That
  store ignored the cache partition, so within its 20-second window one
  site could be served bytes another site had fetched with that site's
  cookies; it ignored `no-store`; it recorded a placeholder for every
  fetch it started but only removed entries when a loader consumed one,
  so failed preloads and preloaded images — which nothing consumed —
  permanently occupied its 32 slots until the preloader silently
  stopped preloading anything. Deduplication now happens in one place.
  The in-flight coalescer keys on method, URL, cache partition and
  request headers rather than URL plus referrer, and every entry point
  joins it — `ns_net_request_async` and the blocking fetchers as well
  as `ns_net_fetch_async` — so a loader that arrives while a preload is
  still in flight waits for it instead of issuing a second request. A
  preload that finishes first is held in a preload map under that same
  key, handed over by the fetch layer itself so there is no window in
  which a resource is in neither place, and dropped when the next
  navigation begins. The preloader now sends the `Accept` header its
  consumer will send, so content-negotiated resources match. The
  separate external-script prefetcher, a third path over the same URLs,
  is gone. A page with six scripts and five stylesheets issues exactly
  one request per resource, counted at the origin.

Layout and rendering
* Box `x`/`y` uniformly means the margin-box origin, which fixes flex
  and grid items with margins rendering and measuring double-shifted;
  `getBoundingClientRect` derives the border box the same way the
  painter does. Grid row placement was still adding the item's top
  margin on top of that origin, so a negative margin moved the item the
  wrong way by twice the amount.
* Flex items are sized by the flex algorithm rather than by their own
  `width`. `layout_block` used to read `width` back out of the style and
  ignore the main size the container had assigned, so nothing ever
  shrank — `flex-shrink: 1` is the initial value, so every
  over-constrained flex row overflowed instead of fitting.
* The flex main axis is reversed when exactly one of
  `flex-direction: row-reverse` and `direction: rtl` applies, and items
  are then packed from the opposite edge. `row-reverse` used to reverse
  the item order but still pack against the left edge, and `rtl` was
  ignored for the main axis entirely.
* `scrollWidth`/`scrollHeight` measure the real scrollable overflow
  region from descendant border boxes, including overflow from
  negative margins on non-scrolling boxes.
* Escaping floats and compact line boxes lay out correctly, the
  non-rendering elements (`area`, `base`, `link`, `meta`, `param`,
  `source`, `track`, …) are excluded from inline layout, anonymous
  table cells lay out as blocks, and malformed legacy declarations are
  skipped rather than derailing the rest of the block.

HTML, DOM and JavaScript
* Core content-attribute reflection is complete, and the text-control
  selection APIs (`selectionStart`/`End`/`Direction`,
  `setSelectionRange`, `textLength`) match the spec including per-type
  applicability and the `IndexSizeError`/`InvalidStateError` cases.
* Mutation observers and shadow trees align with the spec: old-value
  records for attributes and character data, `attachShadow` options,
  `assignedSlot`, and `ShadowRoot.styleSheets`/`activeElement`/
  `elementFromPoint`.
* Frames are isolated per document: events, event handlers and the
  `Performance` objects belong to the frame's own realm, so scripts
  inside an iframe see their own `window` and `document`.
* Speedometer 3.1 fixes: nested custom-element upgrades during
  construction, unhandled rejections queued to the microtask
  checkpoint, module scripts in iframes seeing the frame realm,
  `ownerDocument` returning the realm wrapper, frame windows in the
  event propagation path, images in frames resolving relative URLs
  against the frame document, and the always-rejecting
  `navigator.wakeLock` stub removed so feature detection falls back
  cleanly.
* Promise rejection events: cancelable `unhandledrejection` carrying
  `promise`/`reason`, with `rejectionhandled` for rejections handled
  later.
* `XMLHttpRequest` gains the upload object, the full progress event
  set, `responseXML` and method/state validation; `MessageEvent` and
  `ExtendableMessageEvent` follow the messaging spec.
* `Navigator` and friends (`MimeTypeArray`, `PluginArray`,
  `NetworkInformation`, `StorageManager`, `UserActivation`,
  `NavigatorUAData`, `MediaCapabilities`, `MediaDevices`) are real
  interfaces with non-constructible prototypes, so brand and
  `instanceof` checks agree.
* Cookie Store API: promise-based `cookieStore.get`/`getAll`/`set`/
  `delete` over the document cookie jar.
* Legacy media and timing surfaces: the `HTMLMediaElement`/
  `MediaError` constants, `PerformanceTiming` and
  `PerformanceNavigation`; `blob:` URLs work as module script sources.
* `<input type=email>` validation follows the HTML email grammar, and
  Trusted Types plus void-element serialization are tightened.
* Reading an `<iframe>`'s `contentDocument` or `contentWindow` more than
  once no longer aborts the process. The realm document's API was
  installed onto the per-node wrapper every time it was requested, and
  the second install hit QuickJS's "property already exists" abort in
  `JS_DefineAutoInitProperty`. The install now runs once per wrapper, so
  repeat reads return the same document, as the DOM requires.

Identity and privacy
* `Sec-CH-UA` and `navigator.userAgentData` report `"Nordstjernen"`
  instead of impersonating Chromium and Google Chrome;
  `getHighEntropyValues` fills in `formFactors`, `platformVersion`,
  `architecture`, `bitness`, `wow64`, `model`, `uaFullVersion` and
  `fullVersionList`.
* New `--private` command-line flag starts the browser in private
  browsing mode.
* The Chrome compatibility token in the user agent moves to 150.

Media
* MSE segments are tracked with their byte offset and probed time
  range. `SourceBuffer.remove()` rebuilds the helper input from the
  initialization segment plus the retained segments, which keeps
  long-running adaptive playback (YouTube, Vimeo) inside its byte
  quota, and `SourceBuffer.buffered` reports the retained demux range
  instead of a synthetic zero-based one. `remove()` no longer refuses
  ranges that are not a clean prefix.
* `@font-face` sources are only fetched when their `unicode-range`
  covers a codepoint the page actually uses, so font-heavy pages stop
  competing with streaming playback for bandwidth.
* `<video>`/`<audio>` expose `seeking` and `played`, and fire
  `ratechange` and the seek events.

Layout
* A flex item with padding or a border is no longer sized smaller than
  its content. The automatic flex base size came from
  measure_natural_width(), which already reports a content width, and
  then subtracted the item's own padding and border a second time — the
  surrounding code tracks those separately. Items came out exactly one
  padding-and-border narrower than they should be, so buttons and labels
  in a flex row wrapped mid-phrase for no reason. A row of consent
  buttons that Chrome lays out as two single-line pills was wrapping to
  two lines each; it now matches. Items without padding were unaffected,
  which is why this survived so long.

Headless
* `--dump=png` no longer crashes on pages that keep scripting busy while
  media is fetched. The video prefetch held box pointers across a
  blocking fetch, and the nested main loop that fetch runs can relayout
  the page and free them underneath it. It now resolves the URLs, then
  re-finds the box before attaching, so no box outlives a fetch.

Performance
* A page using container units settles in two container passes instead
  of three. Container queries are resolved by iterating cascade and
  layout until the styles stop changing, up to three times. The loop
  compared the two style tables to decide, which meant the third pass
  always ran a full cascade before discovering it had nothing to do.
  It now compares the container geometry the cascade actually reads --
  and only the axis a query can observe, so the block size of a
  `container-type: inline-size` element, which no query and no `cqh`
  unit can ever see, no longer counts as a change. VG's front page is
  the shape this was costing: seventeen inline-size containers whose
  heights kept moving while their widths had already converged, so
  every relayout paid for three cascades and three layouts. A relayout
  there drops from ~1.6 s to ~0.6 s, and the page, which previously
  never finished rendering at all, now settles in 23 s.
* Nested flex rows no longer lay out in exponential time. A flex item
  that stretches to the line's cross size was laid out once against its
  natural height and then, because its own children had been aligned
  against the wrong height, laid out a second time. `align-items:
  stretch` is the default, so this doubled the work at every level of
  flex nesting: layout cost 2^depth. A synthetic page of nested
  stretched rows took 6.4 s at depth 14, 71 s at depth 16, and killed
  the renderer at depth 18. The stretched cross size is known before the
  item is laid out, so it is now handed down as the item's definite
  height on the first pass and the second pass is skipped. The same page
  takes 39 ms at depth 14, 89 ms at depth 16, and 1.3 s at depth 20.
  Real pages are shallower but wide: this is layout work removed from
  every flex row on every page, not only deep ones.
* Container queries no longer defeat incremental restyle. The second
  cascade pass — the one that runs with container sizes known — took the
  branch that throws the previous pass's computed styles away, so every
  page using `@container` re-cascaded every element from scratch on
  every relayout, forever. The cache now survives that pass, and rules
  carrying a container condition no longer force conservative
  invalidation keys either: those rules are inert in the first pass,
  which is the only one incremental restyle runs in. On a 1610-element
  container-query page churning through 13 relayouts, cascade time drops
  from 47ms to 12ms and style reuse goes from 0 to 1609 of 1610
  elements per pass.
* A `:has()` selector whose subject is matched by an attribute — say
  `[data-state]:has(...)` — keys invalidation on that attribute instead
  of switching incremental restyle off for the whole page. `class`, `id`
  and `style` are excluded, since keying on those matches nearly every
  element and floods the document anyway.
* `:nth-child`/`:nth-last-child` sibling indices are computed once per
  selector-matching batch instead of per element.
* Live DOM collections use the QuickJS array-index atom fast path, and
  childlist invalidation is narrowed to the affected parent.

User interface
* The `about:start` splash is redesigned as a 1997 Netscape release
  screen — beveled chrome, dithered sky, receding cyberspace grid —
  then reworked into a daytime scene whose ground is a procedurally
  generated planet surface sampled through the real pinhole
  projection, with an ocean-to-snow terrain ramp, clouds, a specular
  sun column and atmospheric haze into the limb. The loading bar is
  gone and the animation is 322 KB, down from 1.8 MB.
* Android: the kebab icon is readable on the toolbar, nested consent
  dialogs scroll, and the renderer thread no longer frees the
  shell-owned framebuffer.

Documentation, build and CI
* A whole-system architecture poster (`docs/Software-Architecture.png`)
  maps the process tree, IPC boundaries, engine pipeline, module
  dependencies and information flows; the generators live in
  `scripts/arch-diagram/`.
* The CSS and HTML compatibility documents are refreshed, and
  `docs/media.md` describes the MSE eviction model.
* CI hardens the V8 artifact download with retries and integrity
  checks, and the V8 smoke test now covers microtasks and timers.
* The readme links the openSUSE RPM directly.
