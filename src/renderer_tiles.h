/* Nordstjernen: paint a page into shared-memory tiles for the window's compositor. */

#ifndef NS_RENDERER_TILES_H
#define NS_RENDERER_TILES_H

#include <glib.h>

#include "libnordstjernen.h"

G_BEGIN_DECLS

typedef struct ns_tiles ns_tiles;

typedef struct ns_tiles_view {
    long   sx, sy;
    int    vw, vh;
    double scale;
    int    page_h;
} ns_tiles_view;

ns_tiles *ns_tiles_new(void);
void ns_tiles_free(ns_tiles *t);
gboolean ns_tiles_requested(const char *body);
int ns_tiles_render(ns_tiles *t, ns_browser *b, const char *body,
                    const ns_tiles_view *view, gboolean invalid,
                    unsigned char *fb, size_t fb_size, GString *desc);

G_END_DECLS

#endif
