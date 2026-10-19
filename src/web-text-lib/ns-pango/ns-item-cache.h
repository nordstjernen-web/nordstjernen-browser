/* ns-pango
 * ns-item-cache.h: Cross-layout cache of itemised paragraphs.
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

#ifndef __NS_ITEM_CACHE_H__
#define __NS_ITEM_CACHE_H__

#include <ns-pango/pango-context.h>
#include <ns-pango/pango-attributes.h>
#include <ns-pango/pango-item.h>

G_BEGIN_DECLS

GList *ns_pango_item_cache_lookup (NsPangoContext   *context,
                                   NsPangoDirection  base_dir,
                                   const char       *text,
                                   int               start_index,
                                   int               length,
                                   NsPangoAttrList  *attrs);

void   ns_pango_item_cache_insert (NsPangoContext   *context,
                                   NsPangoDirection  base_dir,
                                   const char       *text,
                                   int               start_index,
                                   int               length,
                                   NsPangoAttrList  *attrs,
                                   GList            *items);

void   ns_pango_item_cache_trim   (void);

void   ns_pango_item_cache_clear  (void);

G_END_DECLS

#endif /* __NS_ITEM_CACHE_H__ */
