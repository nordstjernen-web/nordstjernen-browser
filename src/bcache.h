/* Nordstjernen — JavaScript bytecode cache (in-memory + on-disk).
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_BCACHE_H
#define NS_BCACHE_H

#include <glib.h>

G_BEGIN_DECLS

void          ns_bcache_init(void);
void          ns_bcache_shutdown(void);

guint8 *ns_bcache_get(const char *src, gsize src_len, gsize *out_len);
void    ns_bcache_put(const char *src, gsize src_len,
                      const guint8 *bc, gsize bc_len);

G_END_DECLS

#endif
