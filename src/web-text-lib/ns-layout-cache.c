/* ns-pango
 * ns-layout-cache.c: Cross-layout cache of broken lines.
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

#include "config.h"

#include <string.h>

#include "ns-layout-cache.h"
#include "ns-item-cache.h"
#include "ns-pango-cache.h"
#include "ns-shape-cache.h"
#include "pango-layout-private.h"
#include "pango-attributes-private.h"

#define NS_LAYOUT_CACHE_MAX_ENTRIES 8192
#define NS_LAYOUT_CACHE_MAX_TEXT    (16 * 1024)
#define NS_LAYOUT_CACHE_MAX_BYTES   (24 * 1024 * 1024)

#define ATTR_OVERHEAD_ESTIMATE (sizeof (NsPangoAttribute) + sizeof (gpointer) + 64)

typedef struct
{
  NsPangoLayoutKey key;
  GSList          *lines;
  guint            line_count;
  NsPangoLogAttr  *log_attrs;
  int              n_chars;
  int              tab_width;
  gunichar         decimal;
  gboolean         is_wrapped;
  gboolean         is_ellipsized;
  gsize            bytes;
  gint             read;
  char             owned_text[1];
} LayoutEntry;

static GRWLock     cache_lock;
static GHashTable *cache_table;
static gsize       cache_bytes;

static gint layout_hits;
static gint layout_misses;
static gint layout_skips;

static inline guint
mix_hash (guint h)
{
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;

  return h;
}

static gboolean
tab_arrays_equal (NsPangoTabArray *a,
                  NsPangoTabArray *b)
{
  int n;

  if (a == b)
    return TRUE;

  if (a == NULL || b == NULL)
    return FALSE;

  n = ns_pango_tab_array_get_size (a);
  if (n != ns_pango_tab_array_get_size (b) ||
      ns_pango_tab_array_get_positions_in_pixels (a) !=
      ns_pango_tab_array_get_positions_in_pixels (b))
    return FALSE;

  for (int i = 0; i < n; i++)
    {
      NsPangoTabAlign align_a, align_b;
      int location_a, location_b;

      ns_pango_tab_array_get_tab (a, i, &align_a, &location_a);
      ns_pango_tab_array_get_tab (b, i, &align_b, &location_b);

      if (align_a != align_b || location_a != location_b ||
          ns_pango_tab_array_get_decimal_point (a, i) !=
          ns_pango_tab_array_get_decimal_point (b, i))
        return FALSE;
    }

  return TRUE;
}

static guint
layout_key_hash (gconstpointer v)
{
  return ((const NsPangoLayoutKey *) v)->hash;
}

static gboolean
layout_key_equal (gconstpointer a,
                  gconstpointer b)
{
  const NsPangoLayoutKey *ka = a;
  const NsPangoLayoutKey *kb = b;

  if (ka->hash != kb->hash ||
      ka->context != kb->context ||
      ka->serial != kb->serial ||
      ka->length != kb->length ||
      ka->width != kb->width ||
      ka->height != kb->height ||
      ka->indent != kb->indent ||
      ka->spacing != kb->spacing ||
      ka->line_spacing != kb->line_spacing ||
      ka->flags != kb->flags ||
      memcmp (ka->text, kb->text, ka->length) != 0)
    return FALSE;

  if ((ka->font_desc == NULL) != (kb->font_desc == NULL) ||
      (ka->font_desc != NULL &&
       !ns_pango_font_description_equal (ka->font_desc, kb->font_desc)))
    return FALSE;

  return tab_arrays_equal (ka->tabs, kb->tabs) &&
         ns_pango_attr_lists_equal_in_order (ka->attrs, kb->attrs);
}

static void
layout_entry_free (gpointer data)
{
  LayoutEntry *entry = data;

  g_slist_free_full (entry->lines, (GDestroyNotify) ns_pango_layout_line_unref);
  g_free (entry->log_attrs);
  if (entry->key.attrs)
    ns_pango_attr_list_unref (entry->key.attrs);
  if (entry->key.font_desc)
    ns_pango_font_description_free (entry->key.font_desc);
  if (entry->key.tabs)
    ns_pango_tab_array_free (entry->key.tabs);
  g_object_unref (entry->key.context);
  g_free (entry);
}

gboolean
ns_pango_layout_cache_key_init (NsPangoLayoutKey *key,
                                NsPangoLayout    *layout)
{
  guint hash = 5381;

  if (!ns_pango_caches_enabled () ||
      layout->context == NULL || layout->text == NULL ||
      layout->length > NS_LAYOUT_CACHE_MAX_TEXT)
    {
      g_atomic_int_inc (&layout_skips);
      return FALSE;
    }

  for (int i = 0; i < layout->length; i++)
    hash = hash * 33 + (guchar) layout->text[i];

  key->context = layout->context;
  key->serial = ns_pango_context_get_serial (layout->context);
  key->epoch = ns_pango_shape_cache_epoch ();
  key->misses = ns_pango_shape_cache_miss_count ();
  key->length = layout->length;
  key->text = layout->text;
  key->attrs = layout->attrs;
  key->font_desc = layout->font_desc;
  key->tabs = layout->tabs;
  key->width = layout->width;
  key->height = layout->height;
  key->indent = layout->indent;
  key->spacing = layout->spacing;
  key->line_spacing = layout->line_spacing;
  key->flags = (guint) layout->justify |
               (guint) layout->justify_last_line << 1 |
               (guint) layout->single_paragraph << 2 |
               (guint) layout->auto_dir << 3 |
               (guint) layout->wrap << 4 |
               (guint) layout->ellipsize << 6;

  hash = hash * 33 + key->serial;
  hash = hash * 33 + (guint) key->width;
  hash = hash * 33 + (guint) key->height;
  hash = hash * 33 + (guint) key->indent;
  hash = hash * 33 + (guint) key->spacing;
  hash = hash * 33 + key->flags;
  if (key->font_desc)
    hash = hash * 33 + ns_pango_font_description_hash (key->font_desc);
  if (key->tabs)
    hash = hash * 33 + (guint) ns_pango_tab_array_get_size (key->tabs);
  key->hash = mix_hash (ns_pango_attr_lists_hash (hash, key->attrs));

  return TRUE;
}

gboolean
ns_pango_layout_cache_restore (const NsPangoLayoutKey *key,
                               NsPangoLayout          *layout)
{
  LayoutEntry *entry;

  g_rw_lock_reader_lock (&cache_lock);

  entry = cache_table != NULL ? g_hash_table_lookup (cache_table, key) : NULL;
  if (entry == NULL || entry->key.epoch != key->epoch)
    {
      g_rw_lock_reader_unlock (&cache_lock);
      g_atomic_int_inc (&layout_misses);
      return FALSE;
    }

  g_atomic_int_set (&entry->read, TRUE);

  layout->lines = _ns_pango_layout_copy_lines (entry->lines, layout);
  layout->line_count = entry->line_count;
  layout->is_wrapped = entry->is_wrapped;
  layout->is_ellipsized = entry->is_ellipsized;
  if (layout->log_attrs == NULL)
    layout->log_attrs = g_memdup2 (entry->log_attrs,
                                   (entry->n_chars + 1) * sizeof (NsPangoLogAttr));
  if (layout->tab_width == -1)
    layout->tab_width = entry->tab_width;
  if (layout->decimal == 0)
    layout->decimal = entry->decimal;

  g_rw_lock_reader_unlock (&cache_lock);

  g_atomic_int_inc (&layout_hits);

  return TRUE;
}

static void
drop_unread_entries (void)
{
  GHashTableIter iter;
  gpointer k, v;
  gsize freed = 0;

  g_hash_table_iter_init (&iter, cache_table);
  while (g_hash_table_iter_next (&iter, &k, &v))
    {
      LayoutEntry *entry = v;

      if (entry->read)
        entry->read = FALSE;
      else
        {
          freed += entry->bytes;
          g_hash_table_iter_remove (&iter);
        }
    }

  cache_bytes -= MIN (freed, cache_bytes);
}

static void
make_room (void)
{
  if (g_hash_table_size (cache_table) < NS_LAYOUT_CACHE_MAX_ENTRIES &&
      cache_bytes < NS_LAYOUT_CACHE_MAX_BYTES)
    return;

  drop_unread_entries ();

  if (g_hash_table_size (cache_table) >= NS_LAYOUT_CACHE_MAX_ENTRIES / 2 ||
      cache_bytes >= NS_LAYOUT_CACHE_MAX_BYTES / 2)
    {
      g_hash_table_remove_all (cache_table);
      cache_bytes = 0;
    }
}

void
ns_pango_layout_cache_store (const NsPangoLayoutKey *key,
                             NsPangoLayout          *layout)
{
  LayoutEntry *entry;
  GPtrArray *attrs;

  if (layout->lines == NULL ||
      key->serial != ns_pango_context_get_serial (layout->context) ||
      key->epoch != ns_pango_shape_cache_epoch () ||
      key->misses != ns_pango_shape_cache_miss_count ())
    return;

  entry = g_malloc (sizeof (LayoutEntry) + key->length);
  entry->key = *key;
  memcpy (entry->owned_text, key->text, key->length);
  entry->key.text = entry->owned_text;
  entry->key.attrs = key->attrs ? ns_pango_attr_list_copy (key->attrs) : NULL;
  entry->key.font_desc = key->font_desc ? ns_pango_font_description_copy (key->font_desc) : NULL;
  entry->key.tabs = key->tabs ? ns_pango_tab_array_copy (key->tabs) : NULL;
  g_object_ref (key->context);

  entry->lines = _ns_pango_layout_copy_lines (layout->lines, NULL);
  entry->line_count = layout->line_count;
  entry->n_chars = layout->n_chars;
  entry->log_attrs = g_memdup2 (layout->log_attrs,
                                (layout->n_chars + 1) * sizeof (NsPangoLogAttr));
  entry->tab_width = layout->tab_width;
  entry->decimal = layout->decimal;
  entry->is_wrapped = layout->is_wrapped;
  entry->is_ellipsized = layout->is_ellipsized;
  entry->read = FALSE;

  attrs = entry->key.attrs ? entry->key.attrs->attributes : NULL;
  entry->bytes = sizeof (LayoutEntry) + key->length +
                 (layout->n_chars + 1) * sizeof (NsPangoLogAttr) +
                 _ns_pango_layout_lines_size (entry->lines) +
                 (attrs != NULL ? attrs->len * ATTR_OVERHEAD_ESTIMATE : 0);

  g_rw_lock_writer_lock (&cache_lock);

  if (G_UNLIKELY (cache_table == NULL))
    cache_table = g_hash_table_new_full (layout_key_hash, layout_key_equal,
                                         NULL, layout_entry_free);
  else
    make_room ();

  {
    const LayoutEntry *old = g_hash_table_lookup (cache_table, &entry->key);

    if (old != NULL)
      cache_bytes -= MIN (old->bytes, cache_bytes);
  }

  cache_bytes += entry->bytes;

  g_hash_table_replace (cache_table, &entry->key, entry);

  g_rw_lock_writer_unlock (&cache_lock);
}

void
ns_pango_layout_cache_clear (void)
{
  g_rw_lock_writer_lock (&cache_lock);

  if (cache_table != NULL)
    g_hash_table_remove_all (cache_table);
  cache_bytes = 0;

  g_rw_lock_writer_unlock (&cache_lock);
}

void
ns_pango_layout_cache_stats (guint64 *hits,
                             guint64 *misses,
                             guint64 *skipped,
                             guint64 *entries)
{
  if (hits) *hits = (guint) g_atomic_int_get (&layout_hits);
  if (misses) *misses = (guint) g_atomic_int_get (&layout_misses);
  if (skipped) *skipped = (guint) g_atomic_int_get (&layout_skips);

  if (entries)
    {
      g_rw_lock_reader_lock (&cache_lock);
      *entries = cache_table != NULL ? g_hash_table_size (cache_table) : 0;
      g_rw_lock_reader_unlock (&cache_lock);
    }
}
