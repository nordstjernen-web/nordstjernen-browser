/* Nordstjernen — optional spell checking over the Enchant library.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "spellcheck.h"

#include <string.h>

#ifdef NS_HAVE_ENCHANT

#include <enchant.h>

static EnchantBroker *g_broker;
static GPtrArray     *g_dicts;
static GPtrArray     *g_dict_tags;

static char *
normalize_lang(const char *raw)
{
    if (!raw || !*raw) return NULL;
    gsize n = strcspn(raw, ".@: \t");
    if (n == 0) return NULL;
    char *t = g_strndup(raw, n);
    for (char *p = t; *p; p++)
        if (*p == '-') *p = '_';
    return t;
}

static void
try_load(const char *tag)
{
    if (!tag || !*tag) return;
    for (guint i = 0; i < g_dict_tags->len; i++)
        if (strcmp(g_ptr_array_index(g_dict_tags, i), tag) == 0)
            return;
    if (!enchant_broker_dict_exists(g_broker, tag))
        return;
    EnchantDict *d = enchant_broker_request_dict(g_broker, tag);
    if (!d) return;
    g_ptr_array_add(g_dicts, d);
    g_ptr_array_add(g_dict_tags, g_strdup(tag));
}

void
ns_spell_init(void)
{
    if (g_broker) return;
    g_broker = enchant_broker_init();
    if (!g_broker) return;
    g_dicts = g_ptr_array_new();
    g_dict_tags = g_ptr_array_new_with_free_func(g_free);

    const char *const *names = g_get_language_names();
    for (int i = 0; names && names[i]; i++) {
        char *t = normalize_lang(names[i]);
        if (t) { try_load(t); g_free(t); }
    }
    try_load("en_US");
    try_load("en_GB");
    try_load("en");

    /* Force any lazy backend initialisation (dictionary mmap, affix tables,
     * plugin dlopen) to happen now, before the renderer seals its sandbox —
     * after which the filesystem and extra syscalls are unavailable. */
    for (guint i = 0; i < g_dicts->len; i++) {
        EnchantDict *d = g_ptr_array_index(g_dicts, i);
        enchant_dict_check(d, "test", 4);
        size_t ns = 0;
        char **s = enchant_dict_suggest(d, "teh", 3, &ns);
        if (s) enchant_dict_free_string_list(d, s);
    }
}

gboolean
ns_spell_available(void)
{
    return g_dicts && g_dicts->len > 0;
}

static EnchantDict *
pick_dict(const char *lang)
{
    if (!ns_spell_available()) return NULL;
    char *t = normalize_lang(lang);
    if (t) {
        for (guint i = 0; i < g_dict_tags->len; i++)
            if (g_ascii_strcasecmp(g_ptr_array_index(g_dict_tags, i), t) == 0) {
                EnchantDict *d = g_ptr_array_index(g_dicts, i);
                g_free(t);
                return d;
            }
        gsize pn = strcspn(t, "_");
        for (guint i = 0; i < g_dict_tags->len; i++) {
            const char *dt = g_ptr_array_index(g_dict_tags, i);
            if (g_ascii_strncasecmp(dt, t, pn) == 0 &&
                (dt[pn] == '\0' || dt[pn] == '_')) {
                EnchantDict *d = g_ptr_array_index(g_dicts, i);
                g_free(t);
                return d;
            }
        }
        g_free(t);
    }
    return g_ptr_array_index(g_dicts, 0);
}

gboolean
ns_spell_word_ok(const char *word, gssize len, const char *lang)
{
    if (!word) return TRUE;
    EnchantDict *d = pick_dict(lang);
    if (!d) return TRUE;
    if (len < 0) len = (gssize)strlen(word);
    if (len == 0) return TRUE;
    return enchant_dict_check(d, word, len) <= 0;
}

#else /* !NS_HAVE_ENCHANT */

void ns_spell_init(void) {}

gboolean ns_spell_available(void) { return FALSE; }

gboolean
ns_spell_word_ok(const char *word, gssize len, const char *lang)
{
    (void)word; (void)len; (void)lang;
    return TRUE;
}

#endif
