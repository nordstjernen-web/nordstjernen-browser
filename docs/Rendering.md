# Rendering and scrolling

Nordstjernen paints rendered HTML through GTK's `GtkDrawingArea` and Cairo.
Layout is still a live `ns_box` tree, so the main draw path must stay on the
GTK/main thread. Scrolling performance is therefore improved by reducing the
amount of Cairo/Pango work repeated per scroll frame, not by moving live paint
work to another thread.

## Retained article tile cache

Rendered HTML pages now keep a per-window retained paint cache in
`src/tile_cache.c`. The cache stores Cairo image surfaces for fixed-height
vertical page tiles. During `ns_draw_render()`, a static page paint blits cached
tiles and only renders missing tiles synchronously.

The cache key includes the layout root pointer, target paint width, current
search query, active search match, and search case-sensitivity flag. Any layout
tree rebuild changes the root pointer and clears the cache. Active text
selection and focused editable controls bypass the cache because their visual
state changes too often to retain safely.

Pages containing `position: sticky` boxes also bypass the cache. Sticky offsets
are derived from the live paint clip, so baking them into fixed document-space
tiles would stick the element to each tile boundary instead of the viewport.
Such pages fall back to the direct paint path, which clips to the real
overscanned viewport.

The tile cache is intentionally retained on the main thread. Cairo surfaces are
created and consumed from the same thread as GTK drawing, avoiding cross-thread
ownership of live layout, DOM nodes, Pango objects, or GTK state.

After a cacheable draw, Nordstjernen schedules one low-priority idle prewarm for
the next uncached tile adjacent to the overscanned viewport. That keeps nearby
article content ready for the next wheel scroll while limiting idle work to a
single tile per draw cycle. It may run while a deferred relayout is pending
because that retained layout is still the one being drawn; the subsequent layout
rebuild cancels pending prewarm work and clears the cache.

## Invalidation

The cache is cleared when the underlying static paint can change:

- layout tree drop or rebuild;
- CSS/style dirtying, webfont arrival, zoom changes, and page teardown;
- JavaScript repaint callbacks;
- image/video animation ticks;
- video readiness;
- internal overflow scrolling inside a page.

Still-image readiness is more targeted. If the loaded image does not require
relayout, only the tile range occupied by the image box is invalidated. This
keeps nearby article tiles hot while allowing loaded thumbnails or broken-image
markers to replace placeholders immediately.

## Profiling signal

With `NS_PROFILE=1`, paint profile lines now include tile-cache counters:

```text
tiles=<count> hit=<hits> miss=<misses> cache=<MiB>MB
```

On the Trinidad and Tobago Wikipedia article, the expected pattern is:

- first paint after layout: several misses while visible/overscanned tiles are
  populated;
- repeated paints without visual changes: all hits and almost no box walk;
- scrolling into new content: mostly hits plus one or two misses for newly
  visible tiles;
- image arrival without relayout: mostly hits plus a miss for the image's tile.

This does not make the first layout free. It shifts repeated scroll-frame work
from full article painting to surface blits, while keeping cache misses
synchronous so blank tile gaps are not displayed.
