/* Nordstjernen: WOFF2 web font decoder over libbrotlidec. */

#ifndef NS_WOFF2_H
#define NS_WOFF2_H

#include <glib.h>

G_BEGIN_DECLS

gboolean ns_woff2_is_woff2(const guint8 *data, gsize len);

/* Decode a single-font WOFF2 file into an OpenType/TrueType SFNT that
 * FreeType and fontconfig can load. Returns a g_malloc'd buffer or NULL. */
guint8  *ns_woff2_to_sfnt(const guint8 *data, gsize len, gsize *out_len,
                          gboolean *out_cff);

G_END_DECLS

#endif
