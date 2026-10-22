/* ns-pango
 * ns-layout-cache.h: Cross-layout cache of broken lines.
 *
 * Copyright (C) 2026 Northstar contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __NS_LAYOUT_CACHE_H__
#define __NS_LAYOUT_CACHE_H__

#include <web-text-lib/pango-layout.h>

G_BEGIN_DECLS

typedef struct
{
  guint                   hash;
  gpointer                context;
  guint                   serial;
  guint                   epoch;
  guint                   misses;
  int                     length;
  const char             *text;
  NsPangoAttrList        *attrs;
  NsPangoFontDescription *font_desc;
  NsPangoTabArray        *tabs;
  int                     width;
  int                     height;
  int                     indent;
  int                     spacing;
  float                   line_spacing;
  guint                   flags;
} NsPangoLayoutKey;

gboolean ns_pango_layout_cache_key_init (NsPangoLayoutKey *key,
                                         NsPangoLayout    *layout);

gboolean ns_pango_layout_cache_restore  (const NsPangoLayoutKey *key,
                                         NsPangoLayout          *layout);

void     ns_pango_layout_cache_store    (const NsPangoLayoutKey *key,
                                         NsPangoLayout          *layout);

void     ns_pango_layout_cache_clear    (void);

G_END_DECLS

#endif
