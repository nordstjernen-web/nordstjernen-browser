/* Nordstjernen — JavaScript bytecode cache.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "bytecode_cache.h"

#include "config.h"

#include <glib/gstdio.h>
#include <string.h>

#define NS_BYTECODE_CACHE_MEM_CAP_BYTES   (16u * 1024u * 1024u)
#define NS_BYTECODE_CACHE_VALUE_CAP_BYTES (4u  * 1024u * 1024u)
#define NS_BYTECODE_CACHE_FORMAT_VERSION  2026052201u

typedef struct ns_bytecode_cache_entry {
    guint8 *bytes;
    gsize   len;
    gint64  used_us;
} ns_bytecode_cache_entry;

static GHashTable *g_mem;
static GMutex      g_lock;
static guint64     g_mem_bytes;
static char       *g_dir;

static void
ns_bytecode_cache_entry_free(gpointer data)
{
    ns_bytecode_cache_entry *e = data;
    if (!e) return;
    g_free(e->bytes);
    g_free(e);
}

void
ns_bytecode_cache_init(void)
{
    g_mutex_lock(&g_lock);
    if (!g_mem)
        g_mem = g_hash_table_new_full(g_str_hash, g_str_equal,
                                      g_free, ns_bytecode_cache_entry_free);
    if (!g_dir) {
        const ns_config *c = ns_config_get();
        if (!c || c->cache_enabled) {
            const char *base = g_get_user_cache_dir();
            g_dir = g_build_filename(base, NS_APP_DIR_NAME, "jsbc", NULL);
            g_mkdir_with_parents(g_dir, 0700);
        }
    }
    g_mutex_unlock(&g_lock);
}

void
ns_bytecode_cache_shutdown(void)
{
    g_mutex_lock(&g_lock);
    if (g_mem) {
        g_hash_table_destroy(g_mem);
        g_mem = NULL;
    }
    g_mem_bytes = 0;
    g_clear_pointer(&g_dir, g_free);
    g_mutex_unlock(&g_lock);
}

static void
hash_source(const char *src, gsize len, char out[65])
{
    GChecksum *c = g_checksum_new(G_CHECKSUM_SHA256);
    g_checksum_update(c, (const guchar *)(src ? src : ""), (gssize)len);
    g_strlcpy(out, g_checksum_get_string(c), 65);
    g_checksum_free(c);
}

static char *
disk_path_for(const char *key)
{
    g_mutex_lock(&g_lock);
    char *base = g_strdup(g_dir);
    g_mutex_unlock(&g_lock);
    if (!base || !key || !*key) { g_free(base); return NULL; }
    char sub[3] = { key[0], key[1], '\0' };
    char *dir = g_build_filename(base, sub, NULL);
    g_mkdir_with_parents(dir, 0700);
    char *file = g_build_filename(dir, key + 2, NULL);
    g_free(dir);
    g_free(base);
    return file;
}

static guint8 *
read_disk(const char *key, gsize *out_len)
{
    char *path = disk_path_for(key);
    if (!path) return NULL;
    GStatBuf stbuf;
    if (g_stat(path, &stbuf) != 0 || stbuf.st_size < 8 ||
        (guint64)stbuf.st_size > (guint64)NS_BYTECODE_CACHE_VALUE_CAP_BYTES + 8) {
        g_free(path);
        return NULL;
    }
    gchar *contents = NULL;
    gsize length = 0;
    gboolean ok = g_file_get_contents(path, &contents, &length, NULL);
    g_free(path);
    if (!ok) return NULL;
    if (length < 4 + 4) { g_free(contents); return NULL; }
    guint32 magic = 0, fmt = 0;
    memcpy(&magic, contents,     4);
    memcpy(&fmt,   contents + 4, 4);
    if (magic != GUINT32_FROM_LE(0x4E4A4243u) ||
        fmt   != GUINT32_FROM_LE(NS_BYTECODE_CACHE_FORMAT_VERSION)) {
        g_free(contents);
        return NULL;
    }
    gsize bc_len = length - 8;
    if (bc_len == 0 || bc_len > NS_BYTECODE_CACHE_VALUE_CAP_BYTES) {
        g_free(contents);
        return NULL;
    }
    guint8 *out = g_malloc(bc_len);
    memcpy(out, contents + 8, bc_len);
    g_free(contents);
    if (out_len) *out_len = bc_len;
    return out;
}

static void
write_disk(const char *key, const guint8 *bc, gsize bc_len)
{
    char *path = disk_path_for(key);
    if (!path) return;
    guint32 magic = GUINT32_TO_LE(0x4E4A4243u);
    guint32 fmt   = GUINT32_TO_LE(NS_BYTECODE_CACHE_FORMAT_VERSION);
    char *tmp = g_strdup_printf("%s.tmp", path);
    FILE *f = g_fopen(tmp, "wb");
    if (!f) { g_free(tmp); g_free(path); return; }
    gboolean wrote = fwrite(&magic, 1, 4, f) == 4 &&
                     fwrite(&fmt,   1, 4, f) == 4 &&
                     fwrite(bc,     1, bc_len, f) == bc_len &&
                     ferror(f) == 0;
    if (fclose(f) != 0 || !wrote) {
        g_unlink(tmp);
        g_free(tmp); g_free(path);
        return;
    }
    if (g_rename(tmp, path) != 0)
        g_unlink(tmp);
    g_free(tmp);
    g_free(path);
}

static void
evict_until_fits(gsize want_bytes)
{
    if (!g_mem) return;
    while (g_mem_bytes + want_bytes > NS_BYTECODE_CACHE_MEM_CAP_BYTES) {
        gint64   oldest_us = G_MAXINT64;
        gpointer oldest_key = NULL;
        ns_bytecode_cache_entry *oldest_entry = NULL;
        GHashTableIter it;
        gpointer k, v;
        g_hash_table_iter_init(&it, g_mem);
        while (g_hash_table_iter_next(&it, &k, &v)) {
            ns_bytecode_cache_entry *e = v;
            if (e->used_us < oldest_us) {
                oldest_us = e->used_us;
                oldest_key = k;
                oldest_entry = e;
            }
        }
        if (!oldest_entry) break;
        g_mem_bytes -= oldest_entry->len;
        g_hash_table_remove(g_mem, oldest_key);
    }
}

guint8 *
ns_bytecode_cache_get(const char *src, gsize src_len, gsize *out_len)
{
    if (!src || !src_len) return NULL;
    char key[65];
    hash_source(src, src_len, key);

    g_mutex_lock(&g_lock);
    if (!g_mem) {
        g_mutex_unlock(&g_lock);
        return NULL;
    }
    ns_bytecode_cache_entry *e = g_hash_table_lookup(g_mem, key);
    if (e) {
        e->used_us = g_get_monotonic_time();
        guint8 *copy = g_memdup2(e->bytes, e->len);
        if (out_len) *out_len = e->len;
        g_mutex_unlock(&g_lock);
        return copy;
    }
    g_mutex_unlock(&g_lock);

    gsize disk_len = 0;
    guint8 *disk = read_disk(key, &disk_len);
    if (!disk) return NULL;

    g_mutex_lock(&g_lock);
    if (g_mem && disk_len <= NS_BYTECODE_CACHE_VALUE_CAP_BYTES) {
        if (!g_hash_table_lookup(g_mem, key)) {
            evict_until_fits(disk_len);
            ns_bytecode_cache_entry *ne = g_new0(ns_bytecode_cache_entry, 1);
            ne->bytes = g_memdup2(disk, disk_len);
            ne->len = disk_len;
            ne->used_us = g_get_monotonic_time();
            g_hash_table_insert(g_mem, g_strdup(key), ne);
            g_mem_bytes += disk_len;
        }
    }
    g_mutex_unlock(&g_lock);
    if (out_len) *out_len = disk_len;
    return disk;
}

void
ns_bytecode_cache_put(const char *src, gsize src_len,
              const guint8 *bc, gsize bc_len)
{
    if (!src || !src_len || !bc || !bc_len) return;
    if (bc_len > NS_BYTECODE_CACHE_VALUE_CAP_BYTES) return;
    char key[65];
    hash_source(src, src_len, key);

    g_mutex_lock(&g_lock);
    if (!g_mem) { g_mutex_unlock(&g_lock); return; }
    ns_bytecode_cache_entry *existing = g_hash_table_lookup(g_mem, key);
    if (existing) {
        existing->used_us = g_get_monotonic_time();
        g_mutex_unlock(&g_lock);
        return;
    }
    evict_until_fits(bc_len);
    ns_bytecode_cache_entry *ne = g_new0(ns_bytecode_cache_entry, 1);
    ne->bytes = g_memdup2(bc, bc_len);
    ne->len = bc_len;
    ne->used_us = g_get_monotonic_time();
    g_hash_table_insert(g_mem, g_strdup(key), ne);
    g_mem_bytes += bc_len;
    g_mutex_unlock(&g_lock);

    const ns_config *cfg = ns_config_get();
    if (!cfg || !cfg->private_mode)
        write_disk(key, bc, bc_len);
}
