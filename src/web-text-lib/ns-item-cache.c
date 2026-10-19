/* ns-pango
 * ns-item-cache.c: Cross-layout cache of itemised paragraphs.
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

#include "ns-item-cache.h"
#include "ns-pango-cache.h"
#include "ns-shape-cache.h"
#include "pango-item-private.h"
#include "pango-attributes-private.h"

/* Itemising is what a layout does before it can shape: bidi levels, script
 * runs, emoji runs, width runs, and a font looked up for every character that
 * the run before it could not cover. It is the last of the three passes over a
 * paragraph that a browser repeats -- for min-content, for max-content, for the
 * real width and again to paint -- that was still being redone each time.
 *
 * The items it returns depend on the text, the base direction, the attributes
 * that reach font selection, where the paragraph sits in the text the
 * attributes are ranges over, and the context: its font description, language,
 * gravity and matrix, and which fonts the font map holds. All but the text, the
 * offset and the attributes are folded into the context's serial, which changes
 * whenever any of them does -- including when a web font arrives -- so an entry
 * keyed on a stale serial is simply never found again.
 *
 * The offset is in the key because the attributes are not clipped to the
 * paragraph: a layout over "Hello\nHello" with bold on the first five bytes
 * itemises both paragraphs with the same list, and only the offset says which
 * of them the bold reaches. Keying on it costs hits only for the same
 * paragraph at different offsets in different texts, which a browser laying
 * out one inline box per layout never produces.
 *
 * The items are handed back as copies, because the line breaker splits and
 * rewrites them.
 */

/* A count of entries is not a bound on anything: an entry holds a paragraph,
 * and a paragraph here can be 64 KB, so four thousand of them is a quarter of a
 * gigabyte of text before counting the items, the attributes, or the fonts and
 * contexts an entry keeps alive. A page is free to be made of large distinct
 * paragraphs, so the bound that matters is the one in bytes.
 */
#define NS_ITEM_CACHE_MAX_ENTRIES 4000
#define NS_ITEM_CACHE_MAX_TEXT    (64 * 1024)
#define NS_ITEM_CACHE_MAX_BYTES   (16 * 1024 * 1024)
#define NS_ITEM_CACHE_SHARDS      16
#define NS_ITEM_CACHE_SHARD(hash) (((hash) >> 27) & (NS_ITEM_CACHE_SHARDS - 1))

typedef struct
{
  guint            hash;
  gpointer         context;
  guint            serial;
  guint32          base_dir;
  guint32          start_index;
  guint32          length;
  const char      *text;
  NsPangoAttrList *attrs;      /* borrowed in a probe, owned in an entry */
  gint             read;
} ItemKey;

typedef struct
{
  ItemKey  key;
  GList   *items;              /* offsets relative to the slice, not the text */
  gsize    bytes;              /* what this entry costs, against the shard's budget */
  char     owned_text[1];
} ItemEntry;

/* What an item costs beyond the struct itself: a list link, a reference on a
 * font, and whatever attributes came with it. Attributes are shared and refcounted
 * and their sizes vary, so this is an estimate rather than a measurement -- it
 * only has to be steady enough to keep the budget from drifting.
 */
#define ITEM_OVERHEAD_ESTIMATE (sizeof (NsPangoItem) + 2 * sizeof (GList) + 64)

/* The key keeps its own deep copy of the attribute list -- ns_pango_attr_list_copy
 * copies every attribute, it does not take a reference -- and that copy is the
 * whole list the layout was built with, not the part covering this paragraph.
 * A page whose body carries a few hundred attributes therefore pays for them
 * once per cached paragraph, which is enough to be the largest thing in the
 * cache and was not counted against the ceiling at all. The largest attribute
 * is a font description, so a pointer's worth of slack per attribute is the
 * same kind of estimate as the line above.
 */
#define ATTR_OVERHEAD_ESTIMATE (sizeof (NsPangoAttribute) + sizeof (gpointer) + 64)

static gsize
item_entry_size (const ItemEntry *entry)
{
  GPtrArray *attrs = entry->key.attrs ? entry->key.attrs->attributes : NULL;

  return sizeof (ItemEntry) + entry->key.length +
         g_list_length (entry->items) * ITEM_OVERHEAD_ESTIMATE +
         (attrs != NULL ? attrs->len * ATTR_OVERHEAD_ESTIMATE : 0);
}

typedef struct
{
  GRWLock     lock;
  GHashTable *table;
  gsize       bytes;
} ItemShard;

static ItemShard shards[NS_ITEM_CACHE_SHARDS];

static gint item_hits;
static gint item_misses;
static gint item_skips;

static inline guint
mix_hash (guint h)
{
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;

  return h;
}

static guint
item_key_hash (gconstpointer v)
{
  return ((const ItemKey *) v)->hash;
}

/* The attribute lists are compared in full. Two layouts over the same paragraph
 * build their own lists with the same content, which is exactly the case worth
 * catching, so identity would never match and only equality will do.
 *
 * They are compared in order, though, and not with ns_pango_attr_list_equal().
 * That function answers whether two lists hold the same attributes in any
 * order, and it does so by scanning the second list for each attribute of the
 * first: quadratic, and paid here once per paragraph per layout, so a page
 * whose body carries a few hundred spans would spend longer comparing lists
 * than itemising. A list keeps its attributes sorted by start index, and a
 * browser builds the list for a paragraph the same way every time it lays the
 * paragraph out, so lists that are equal are also in the same order -- and
 * two that are not will simply miss, which costs an itemise and never a wrong
 * answer.
 */
static gboolean
attr_lists_equal_in_order (NsPangoAttrList *a,
                           NsPangoAttrList *b)
{
  GPtrArray *pa, *pb;

  if (a == b)
    return TRUE;

  if (a == NULL || b == NULL)
    return FALSE;

  pa = a->attributes;
  pb = b->attributes;

  if (pa == NULL || pb == NULL)
    return pa == pb;

  if (pa->len != pb->len)
    return FALSE;

  for (guint i = 0; i < pa->len; i++)
    {
      const NsPangoAttribute *x = g_ptr_array_index (pa, i);
      const NsPangoAttribute *y = g_ptr_array_index (pb, i);

      if (x->start_index != y->start_index ||
          x->end_index != y->end_index ||
          !ns_pango_attribute_equal (x, y))
        return FALSE;
    }

  return TRUE;
}

static gboolean
item_key_equal (gconstpointer a,
                gconstpointer b)
{
  const ItemKey *ka = a;
  const ItemKey *kb = b;

  if (ka->hash != kb->hash ||
      ka->context != kb->context ||
      ka->serial != kb->serial ||
      ka->base_dir != kb->base_dir ||
      ka->start_index != kb->start_index ||
      ka->length != kb->length ||
      memcmp (ka->text, kb->text, ka->length) != 0)
    return FALSE;

  return attr_lists_equal_in_order (ka->attrs, kb->attrs);
}

static void
item_entry_free (gpointer data)
{
  ItemEntry *entry = data;

  g_list_free_full (entry->items, (GDestroyNotify) ns_pango_item_free);
  if (entry->key.attrs)
    ns_pango_attr_list_unref (entry->key.attrs);
  g_object_unref (entry->key.context);
  g_free (entry);
}

/* The hash only has to agree with itself; equality above is what decides. So it
 * takes the cheap half of the attributes -- how many there are and where they
 * begin and end -- and leaves comparing their values to the equal function.
 */
static guint
hash_attrs (guint            hash,
            NsPangoAttrList *attrs)
{
  GPtrArray *a = attrs ? attrs->attributes : NULL;

  if (a == NULL)
    return hash;

  hash = hash * 33 + a->len;
  for (guint i = 0; i < a->len; i++)
    {
      const NsPangoAttribute *attr = g_ptr_array_index (a, i);

      hash = hash * 33 + (guint) attr->klass->type;
      hash = hash * 33 + attr->start_index;
      hash = hash * 33 + attr->end_index;
    }

  return hash;
}

static gboolean
probe_init (ItemKey          *key,
            NsPangoContext   *context,
            NsPangoDirection  base_dir,
            const char       *text,
            int               start_index,
            int               length,
            NsPangoAttrList  *attrs)
{
  const char *slice = text + start_index;
  guint hash = 5381;

  if (!ns_pango_caches_enabled () ||
      context == NULL || length <= 0 || length > NS_ITEM_CACHE_MAX_TEXT)
    return FALSE;

  for (int i = 0; i < length; i++)
    hash = hash * 33 + (guchar) slice[i];

  key->serial = ns_pango_context_get_serial (context);
  key->context = context;
  key->base_dir = (guint32) base_dir;
  key->start_index = (guint32) start_index;
  key->length = (guint32) length;
  key->text = slice;
  key->attrs = attrs;
  key->read = FALSE;

  hash = hash * 33 + key->serial;
  hash = hash * 33 + (guint) base_dir;
  hash = hash * 33 + (guint) start_index;
  hash = hash * 33 + (guint) length;
  key->hash = mix_hash (hash_attrs (hash, attrs));

  return TRUE;
}

/* An item names where it sits in the whole text it came from, so a cached one
 * is stored relative to the slice and put back wherever the slice now begins.
 */
static GList *
copy_items (GList *items,
            int    byte_shift,
            int    char_shift)
{
  GList *out = NULL;

  for (GList *l = items; l; l = l->next)
    {
      NsPangoItem *copy = ns_pango_item_copy (l->data);
      NsPangoItemPrivate *priv = (NsPangoItemPrivate *) copy;

      priv->offset += byte_shift;
      priv->char_offset += char_shift;
      out = g_list_prepend (out, copy);
    }

  return g_list_reverse (out);
}

GList *
ns_pango_item_cache_lookup (NsPangoContext   *context,
                            NsPangoDirection  base_dir,
                            const char       *text,
                            int               start_index,
                            int               length,
                            NsPangoAttrList  *attrs)
{
  ItemShard *shard;
  ItemKey probe;
  ItemEntry *entry;
  GList *items;

  if (!probe_init (&probe, context, base_dir, text, start_index, length, attrs))
    {
      g_atomic_int_inc (&item_skips);
      return NULL;
    }

  shard = &shards[NS_ITEM_CACHE_SHARD (probe.hash)];

  g_rw_lock_reader_lock (&shard->lock);

  entry = shard->table != NULL ? g_hash_table_lookup (shard->table, &probe) : NULL;
  if (entry == NULL)
    {
      g_rw_lock_reader_unlock (&shard->lock);
      g_atomic_int_inc (&item_misses);
      return NULL;
    }

  g_atomic_int_set (&entry->key.read, TRUE);
  items = copy_items (entry->items, start_index,
                      (int) g_utf8_strlen (text, start_index));

  g_rw_lock_reader_unlock (&shard->lock);

  g_atomic_int_inc (&item_hits);

  return items;
}

/* Both of these run with the shard held for writing. */

static void
drop_unread_entries (ItemShard *shard)
{
  GHashTableIter iter;
  gpointer k, v;
  gsize freed = 0;

  if (shard->table == NULL)
    return;

  g_hash_table_iter_init (&iter, shard->table);
  while (g_hash_table_iter_next (&iter, &k, &v))
    {
      ItemEntry *entry = v;

      if (entry->key.read)
        entry->key.read = FALSE;
      else
        {
          freed += entry->bytes;
          g_hash_table_iter_remove (&iter);
        }
    }

  shard->bytes -= MIN (freed, shard->bytes);
}

static void
make_room (ItemShard *shard)
{
  if (g_hash_table_size (shard->table) < NS_ITEM_CACHE_MAX_ENTRIES / NS_ITEM_CACHE_SHARDS &&
      shard->bytes < NS_ITEM_CACHE_MAX_BYTES / NS_ITEM_CACHE_SHARDS)
    return;

  drop_unread_entries (shard);

  if (g_hash_table_size (shard->table) >= NS_ITEM_CACHE_MAX_ENTRIES / NS_ITEM_CACHE_SHARDS / 2 ||
      shard->bytes >= NS_ITEM_CACHE_MAX_BYTES / NS_ITEM_CACHE_SHARDS / 2)
    {
      g_hash_table_remove_all (shard->table);
      shard->bytes = 0;
    }
}

void
ns_pango_item_cache_insert (NsPangoContext   *context,
                            NsPangoDirection  base_dir,
                            const char       *text,
                            int               start_index,
                            int               length,
                            NsPangoAttrList  *attrs,
                            GList            *items)
{
  ItemShard *shard;
  ItemKey probe;
  ItemEntry *entry;

  if (items == NULL ||
      !probe_init (&probe, context, base_dir, text, start_index, length, attrs))
    return;

  entry = g_malloc (sizeof (ItemEntry) + length);
  entry->key = probe;
  memcpy (entry->owned_text, text + start_index, length);
  entry->key.text = entry->owned_text;
  entry->key.attrs = attrs ? ns_pango_attr_list_copy (attrs) : NULL;
  g_object_ref (context);
  entry->items = copy_items (items, -start_index,
                             -(int) g_utf8_strlen (text, start_index));
  entry->bytes = item_entry_size (entry);

  shard = &shards[NS_ITEM_CACHE_SHARD (probe.hash)];

  g_rw_lock_writer_lock (&shard->lock);

  if (G_UNLIKELY (shard->table == NULL))
    shard->table = g_hash_table_new_full (item_key_hash, item_key_equal,
                                          NULL, item_entry_free);
  else
    make_room (shard);

  {
    const ItemEntry *old = g_hash_table_lookup (shard->table, &entry->key);

    if (old != NULL)
      shard->bytes -= MIN (old->bytes, shard->bytes);
  }

  shard->bytes += entry->bytes;

  g_hash_table_replace (shard->table, &entry->key, entry);

  g_rw_lock_writer_unlock (&shard->lock);
}

/* Drops what nothing has asked for since the last sweep, without touching the
 * working set -- what a browser wants when it is asked to give memory back but
 * is still displaying the page.
 */
void
ns_pango_item_cache_trim (void)
{
  for (guint i = 0; i < NS_ITEM_CACHE_SHARDS; i++)
    {
      g_rw_lock_writer_lock (&shards[i].lock);
      drop_unread_entries (&shards[i]);
      g_rw_lock_writer_unlock (&shards[i].lock);
    }
}

void
ns_pango_item_cache_clear (void)
{
  for (guint i = 0; i < NS_ITEM_CACHE_SHARDS; i++)
    {
      g_rw_lock_writer_lock (&shards[i].lock);

      if (shards[i].table != NULL)
        g_hash_table_remove_all (shards[i].table);
      shards[i].bytes = 0;

      g_rw_lock_writer_unlock (&shards[i].lock);
    }
}

void
ns_pango_item_cache_stats (guint64 *hits,
                           guint64 *misses,
                           guint64 *skipped,
                           guint64 *entries)
{
  if (hits) *hits = (guint) g_atomic_int_get (&item_hits);
  if (misses) *misses = (guint) g_atomic_int_get (&item_misses);
  if (skipped) *skipped = (guint) g_atomic_int_get (&item_skips);

  if (entries)
    {
      *entries = 0;

      for (guint i = 0; i < NS_ITEM_CACHE_SHARDS; i++)
        {
          g_rw_lock_reader_lock (&shards[i].lock);
          if (shards[i].table != NULL)
            *entries += g_hash_table_size (shards[i].table);
          g_rw_lock_reader_unlock (&shards[i].lock);
        }
    }
}
