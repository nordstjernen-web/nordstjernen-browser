/* ns-pango
 * ns-break-cache.c: Cross-layout cache of unicode break attributes.
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

#include "ns-break-cache.h"
#include "ns-shape-cache.h"
#include "ns-pango-cache.h"

/* default_break() is the single most expensive thing a layout does that does
 * not touch a font: UAX #14 line breaking and UAX #29 grapheme, word and
 * sentence segmentation, one pass with four-character lookaround, over the
 * whole paragraph. Pango runs it once per NsPangoLayout, and a browser builds
 * a layout over the same paragraph for min-content, for max-content, for the
 * real width and again to paint it.
 *
 * It reads nothing but the text -- its NsPangoAnalysis argument is unused and
 * has been since Pango 1.44 -- so the result is a pure function of the bytes,
 * and the tailoring that follows it (ns_pango_tailor_break, which is per item
 * and does depend on script and language, and ns_pango_attr_break) is applied
 * on top of what this returns rather than being cached with it.
 */

#define NS_BREAK_CACHE_MAX_ENTRIES 8000
#define NS_BREAK_CACHE_MAX_TEXT    (64 * 1024)
#define NS_BREAK_CACHE_MAX_BYTES   (16 * 1024 * 1024)

/* Sharded for the same reason the shape cache is, and for one more: even a
 * lookup takes a reader lock, and a reader lock is an atomic read-modify-write.
 * One lock for the whole cache would put every layout on every thread onto the
 * same cache line whether or not any of them ever wrote.
 */
#define NS_BREAK_CACHE_SHARDS      16
#define NS_BREAK_CACHE_SHARD(hash) (((hash) >> 27) & (NS_BREAK_CACHE_SHARDS - 1))

/* Which shard a paragraph lands in comes off the top of the hash, and a shard
 * that takes more than its share evicts sooner than the others. djb2 carries
 * most of a short string's entropy in the low bits, so it gets an avalanche
 * step before anything reads the top of it.
 */
static inline guint
mix_hash (guint h)
{
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;

  return h;
}

typedef struct
{
  guint       hash;
  guint32     length;
  guint32     n_attrs;
  const char *text;
  gint        read;
} BreakKey;

/* A stored entry keeps the text it was keyed on and the attributes it produced
 * in one allocation, and points its key at the copy. A probe borrows the
 * caller's text instead, so a lookup -- which is what almost every visit is --
 * allocates nothing.
 */
typedef struct
{
  BreakKey       key;
  NsPangoLogAttr *attrs;
  char           text[1];
} BreakEntry;

typedef struct
{
  GRWLock     lock;
  GHashTable *table;
  gsize       bytes;
} BreakShard;

static BreakShard shards[NS_BREAK_CACHE_SHARDS];

static gint break_hits;
static gint break_misses;
static gint break_skips;

static guint
break_key_hash (gconstpointer v)
{
  return ((const BreakKey *) v)->hash;
}

static gboolean
break_key_equal (gconstpointer a,
                 gconstpointer b)
{
  const BreakKey *ka = a;
  const BreakKey *kb = b;

  return ka->hash == kb->hash &&
         ka->length == kb->length &&
         memcmp (ka->text, kb->text, ka->length) == 0;
}

static void
break_entry_free (gpointer data)
{
  BreakEntry *entry = data;

  g_free (entry->attrs);
  g_free (entry);
}

static gsize
break_entry_size (const BreakEntry *entry)
{
  return sizeof (BreakEntry) + entry->key.length +
         entry->key.n_attrs * sizeof (NsPangoLogAttr);
}

/* default_break() stops at an embedded NUL and treats it as the end of the
 * paragraph, so the number of attributes it writes is not a function of
 * @length there. Pango's own text never contains one, so rather than
 * reproduce that rule the cache declines the text -- which the hash pass
 * notices for free.
 */
static gboolean
hash_text (const char *text,
           int         length,
           guint      *out)
{
  guint hash = 5381;

  for (int i = 0; i < length; i++)
    {
      if (G_UNLIKELY (text[i] == '\0'))
        return FALSE;
      hash = hash * 33 + (guchar) text[i];
    }

  *out = mix_hash (hash * 33 + (guint) length);

  return TRUE;
}

static gboolean
probe_init (BreakKey   *key,
            const char *text,
            int         length)
{
  if (!ns_pango_caches_enabled () ||
      text == NULL ||
      length <= 0 ||
      length > NS_BREAK_CACHE_MAX_TEXT ||
      !hash_text (text, length, &key->hash))
    return FALSE;

  key->length = (guint32) length;
  key->n_attrs = 0;
  key->text = text;
  key->read = FALSE;

  return TRUE;
}

gboolean
ns_pango_break_cache_fill (const char     *text,
                           int             length,
                           NsPangoLogAttr *attrs,
                           int             attrs_len)
{
  BreakShard *shard;
  BreakKey probe;
  BreakEntry *entry;

  if (!probe_init (&probe, text, length))
    {
      g_atomic_int_inc (&break_skips);
      return FALSE;
    }

  shard = &shards[NS_BREAK_CACHE_SHARD (probe.hash)];

  g_rw_lock_reader_lock (&shard->lock);

  entry = shard->table != NULL ? g_hash_table_lookup (shard->table, &probe) : NULL;
  if (entry == NULL || (attrs_len >= 0 && entry->key.n_attrs > (guint32) attrs_len))
    {
      g_rw_lock_reader_unlock (&shard->lock);
      g_atomic_int_inc (&break_misses);
      return FALSE;
    }

  /* Marking the entry is what lets eviction keep the working set, exactly as in
   * the shape cache: a sweep drops only what nothing has asked for since the
   * previous sweep, and several readers can be marking at once.
   */
  g_atomic_int_set (&entry->key.read, TRUE);
  memcpy (attrs, entry->attrs, entry->key.n_attrs * sizeof (NsPangoLogAttr));

  g_rw_lock_reader_unlock (&shard->lock);

  g_atomic_int_inc (&break_hits);

  return TRUE;
}

/* Both of these run with the shard held for writing. */

static void
drop_unread_entries (BreakShard *shard)
{
  GHashTableIter iter;
  gpointer k, v;
  gsize freed = 0;

  g_hash_table_iter_init (&iter, shard->table);
  while (g_hash_table_iter_next (&iter, &k, &v))
    {
      BreakEntry *entry = v;

      if (entry->key.read)
        entry->key.read = FALSE;
      else
        {
          freed += break_entry_size (entry);
          g_hash_table_iter_remove (&iter);
        }
    }

  shard->bytes -= MIN (freed, shard->bytes);
}

static void
make_room (BreakShard *shard)
{
  if (g_hash_table_size (shard->table) < NS_BREAK_CACHE_MAX_ENTRIES / NS_BREAK_CACHE_SHARDS &&
      shard->bytes < NS_BREAK_CACHE_MAX_BYTES / NS_BREAK_CACHE_SHARDS)
    return;

  drop_unread_entries (shard);

  if (g_hash_table_size (shard->table) >= NS_BREAK_CACHE_MAX_ENTRIES / NS_BREAK_CACHE_SHARDS / 2 ||
      shard->bytes >= NS_BREAK_CACHE_MAX_BYTES / NS_BREAK_CACHE_SHARDS / 2)
    {
      g_hash_table_remove_all (shard->table);
      shard->bytes = 0;
    }
}

void
ns_pango_break_cache_store (const char           *text,
                            int                   length,
                            const NsPangoLogAttr *attrs)
{
  BreakShard *shard;
  BreakKey probe;
  BreakEntry *entry;
  guint32 n_attrs;

  if (!probe_init (&probe, text, length))
    return;

  n_attrs = (guint32) g_utf8_strlen (text, length) + 1;

  entry = g_malloc (sizeof (BreakEntry) + length);
  entry->key = probe;
  entry->key.n_attrs = n_attrs;
  entry->attrs = g_memdup2 (attrs, n_attrs * sizeof (NsPangoLogAttr));
  memcpy (entry->text, text, length);
  entry->key.text = entry->text;

  shard = &shards[NS_BREAK_CACHE_SHARD (probe.hash)];

  g_rw_lock_writer_lock (&shard->lock);

  if (G_UNLIKELY (shard->table == NULL))
    shard->table = g_hash_table_new_full (break_key_hash, break_key_equal,
                                          NULL, break_entry_free);
  else
    make_room (shard);

  {
    const BreakEntry *old = g_hash_table_lookup (shard->table, &entry->key);

    if (old != NULL)
      shard->bytes -= MIN (break_entry_size (old), shard->bytes);
  }

  shard->bytes += break_entry_size (entry);
  g_hash_table_replace (shard->table, &entry->key, entry);

  g_rw_lock_writer_unlock (&shard->lock);
}

void
ns_pango_break_cache_trim (void)
{
  for (guint i = 0; i < NS_BREAK_CACHE_SHARDS; i++)
    {
      g_rw_lock_writer_lock (&shards[i].lock);
      if (shards[i].table != NULL)
        drop_unread_entries (&shards[i]);
      g_rw_lock_writer_unlock (&shards[i].lock);
    }
}

void
ns_pango_break_cache_clear (void)
{
  for (guint i = 0; i < NS_BREAK_CACHE_SHARDS; i++)
    {
      g_rw_lock_writer_lock (&shards[i].lock);

      if (shards[i].table != NULL)
        g_hash_table_remove_all (shards[i].table);
      shards[i].bytes = 0;

      g_rw_lock_writer_unlock (&shards[i].lock);
    }
}

void
ns_pango_break_cache_stats (guint64 *hits,
                            guint64 *misses,
                            guint64 *skipped,
                            guint64 *entries)
{
  if (hits) *hits = (guint) g_atomic_int_get (&break_hits);
  if (misses) *misses = (guint) g_atomic_int_get (&break_misses);
  if (skipped) *skipped = (guint) g_atomic_int_get (&break_skips);

  if (entries)
    {
      *entries = 0;

      for (guint i = 0; i < NS_BREAK_CACHE_SHARDS; i++)
        {
          g_rw_lock_reader_lock (&shards[i].lock);
          if (shards[i].table != NULL)
            *entries += g_hash_table_size (shards[i].table);
          g_rw_lock_reader_unlock (&shards[i].lock);
        }
    }
}
