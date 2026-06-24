/* Nordstjernen — JavaScript bytecode cache (in-memory + on-disk).
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NS_BYTECODE_CACHE_H
#define NS_BYTECODE_CACHE_H

#include <glib.h>

G_BEGIN_DECLS

void          ns_bytecode_cache_init(void);
void          ns_bytecode_cache_shutdown(void);

guint8 *ns_bytecode_cache_get(const char *src, gsize src_len, gsize *out_len);
void    ns_bytecode_cache_put(const char *src, gsize src_len,
                      const guint8 *bc, gsize bc_len);

G_END_DECLS

#endif
