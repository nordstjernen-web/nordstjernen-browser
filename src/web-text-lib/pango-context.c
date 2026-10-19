/* Pango
 * pango-context.c: Contexts for itemization and shaping
 *
 * Copyright (C) 2000, 2006 Red Hat Software
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
#include <stdlib.h>

#include "pango-context.h"
#include "pango-context-private.h"
#include "pango-impl-utils.h"
#include "ns-shape-cache.h"

#include "pango-font-private.h"
#include "pango-item-private.h"
#include "pango-fontset.h"
#include "pango-fontmap-private.h"
#include "pango-script-private.h"
#include "pango-emoji-private.h"

/**
 * NsPangoContext:
 *
 * A `NsPangoContext` stores global information used to control the
 * itemization process.
 *
 * The information stored by `NsPangoContext` includes the fontmap used
 * to look up fonts, and default values such as the default language,
 * default gravity, or default font.
 *
 * To obtain a `NsPangoContext`, use [method@Pango.FontMap.create_context].
 */

struct _PangoContextClass
{
  GObjectClass parent_class;

};

static void ns_pango_context_finalize    (GObject       *object);
static void context_changed           (NsPangoContext  *context);
static void check_fontmap_changed     (NsPangoContext  *context);

G_DEFINE_TYPE (NsPangoContext, ns_pango_context, G_TYPE_OBJECT)

static void
ns_pango_context_init (NsPangoContext *context)
{
  context->base_dir = NS_PANGO_DIRECTION_WEAK_LTR;
  context->resolved_gravity = context->base_gravity = NS_PANGO_GRAVITY_SOUTH;
  context->gravity_hint = NS_PANGO_GRAVITY_HINT_NATURAL;

  context->serial = 1;
  context->set_language = NULL;
  context->language = ns_pango_language_get_default ();
  context->font_map = NULL;
  context->round_glyph_positions = TRUE;

  context->font_desc = ns_pango_font_description_new ();
  ns_pango_font_description_set_family_static (context->font_desc, "serif");
  ns_pango_font_description_set_style (context->font_desc, NS_PANGO_STYLE_NORMAL);
  ns_pango_font_description_set_variant (context->font_desc, NS_PANGO_VARIANT_NORMAL);
  ns_pango_font_description_set_weight (context->font_desc, NS_PANGO_WEIGHT_NORMAL);
  ns_pango_font_description_set_stretch (context->font_desc, NS_PANGO_STRETCH_NORMAL);
  ns_pango_font_description_set_size (context->font_desc, 12 * NS_PANGO_SCALE);
}

static void
ns_pango_context_class_init (NsPangoContextClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ns_pango_context_finalize;
}

static void
ns_pango_context_finalize (GObject *object)
{
  NsPangoContext *context;

  context = NS_PANGO_CONTEXT (object);

  if (context->font_map)
    g_object_unref (context->font_map);

  ns_pango_font_description_free (context->font_desc);
  if (context->matrix)
    ns_pango_matrix_free (context->matrix);

  if (context->metrics)
    ns_pango_font_metrics_unref (context->metrics);

  g_clear_pointer (&context->metrics_cache, g_hash_table_unref);

  G_OBJECT_CLASS (ns_pango_context_parent_class)->finalize (object);
}

/**
 * ns_pango_context_new:
 *
 * Creates a new `NsPangoContext` initialized to default values.
 *
 * This function is not particularly useful as it should always
 * be followed by a [method@Pango.Context.set_font_map] call, and the
 * function [method@Pango.FontMap.create_context] does these two steps
 * together and hence users are recommended to use that.
 *
 * If you are using Pango as part of a higher-level system,
 * that system may have it's own way of create a `NsPangoContext`.
 * For instance, the GTK toolkit has, among others,
 * `gtk_widget_get_pango_context()`. Use those instead.
 *
 * Returns: (transfer full): the newly allocated `NsPangoContext`, which should
 *   be freed with g_object_unref().
 */
NsPangoContext *
ns_pango_context_new (void)
{
  NsPangoContext *context;

  context = g_object_new (NS_TYPE_PANGO_CONTEXT, NULL);

  return context;
}

static void
update_resolved_gravity (NsPangoContext *context)
{
  if (context->base_gravity == NS_PANGO_GRAVITY_AUTO)
    context->resolved_gravity = ns_pango_gravity_get_for_matrix (context->matrix);
  else
    context->resolved_gravity = context->base_gravity;
}

/**
 * ns_pango_context_set_matrix:
 * @context: a `NsPangoContext`
 * @matrix: (nullable): a `NsPangoMatrix`, or %NULL to unset any existing
 * matrix. (No matrix set is the same as setting the identity matrix.)
 *
 * Sets the transformation matrix that will be applied when rendering
 * with this context.
 *
 * Note that reported metrics are in the user space coordinates before
 * the application of the matrix, not device-space coordinates after the
 * application of the matrix. So, they don't scale with the matrix, though
 * they may change slightly for different matrices, depending on how the
 * text is fit to the pixel grid.
 *
 * Since: 1.6
 */
void
ns_pango_context_set_matrix (NsPangoContext      *context,
                          const NsPangoMatrix *matrix)
{
  g_return_if_fail (NS_PANGO_IS_CONTEXT (context));

  if (context->matrix || matrix)
    context_changed (context);

  if (context->matrix)
    ns_pango_matrix_free (context->matrix);
  if (matrix)
    context->matrix = ns_pango_matrix_copy (matrix);
  else
    context->matrix = NULL;

  update_resolved_gravity (context);
}

/**
 * ns_pango_context_get_matrix:
 * @context: a `NsPangoContext`
 *
 * Gets the transformation matrix that will be applied when
 * rendering with this context.
 *
 * See [method@Pango.Context.set_matrix].
 *
 * Returns: (transfer none) (nullable): the matrix, or %NULL if no
 *   matrix has been set (which is the same as the identity matrix).
 *   The returned matrix is owned by Pango and must not be modified
 *   or freed.
 *
 * Since: 1.6
 */
const NsPangoMatrix *
ns_pango_context_get_matrix (NsPangoContext *context)
{
  g_return_val_if_fail (NS_PANGO_IS_CONTEXT (context), NULL);

  return context->matrix;
}

/**
 * ns_pango_context_set_font_map:
 * @context: a `NsPangoContext`
 * @font_map: (nullable): the `NsPangoFontMap` to set.
 *
 * Sets the font map to be searched when fonts are looked-up
 * in this context.
 *
 * This is only for internal use by Pango backends, a `NsPangoContext`
 * obtained via one of the recommended methods should already have a
 * suitable font map.
 */
void
ns_pango_context_set_font_map (NsPangoContext *context,
                            NsPangoFontMap *font_map)
{
  g_return_if_fail (NS_PANGO_IS_CONTEXT (context));
  g_return_if_fail (!font_map || NS_PANGO_IS_FONT_MAP (font_map));

  if (font_map == context->font_map)
    return;

  context_changed (context);

  if (font_map)
    g_object_ref (font_map);

  if (context->font_map)
    g_object_unref (context->font_map);

  context->font_map = font_map;
  context->fontmap_serial = ns_pango_font_map_get_serial (font_map);
}

/**
 * ns_pango_context_get_font_map:
 * @context: a `NsPangoContext`
 *
 * Gets the `NsPangoFontMap` used to look up fonts for this context.
 *
 * Returns: (transfer none) (nullable): the font map for the.
 *   `NsPangoContext` This value is owned by Pango and should not be
 *   unreferenced.
 *
 * Since: 1.6
 */
NsPangoFontMap *
ns_pango_context_get_font_map (NsPangoContext *context)
{
  g_return_val_if_fail (NS_PANGO_IS_CONTEXT (context), NULL);

  return context->font_map;
}

/**
 * ns_pango_context_list_families:
 * @context: a `NsPangoContext`
 * @families: (out) (array length=n_families) (transfer container): location
 *   to store a pointer to an array of `NsPangoFontFamily`. This array should
 *   be freed with g_free().
 * @n_families: (out): location to store the number of elements in @descs
 *
 * List all families for a context.
 */
void
ns_pango_context_list_families (NsPangoContext      *context,
                             NsPangoFontFamily ***families,
                             int               *n_families)
{
  g_return_if_fail (context != NULL);
  g_return_if_fail (families == NULL || n_families != NULL);

  if (n_families == NULL)
    return;

  if (context->font_map == NULL)
    {
      *n_families = 0;
      if (families)
        *families = NULL;

      return;
    }
  else
    ns_pango_font_map_list_families (context->font_map, families, n_families);
}

/**
 * ns_pango_context_load_font:
 * @context: a `NsPangoContext`
 * @desc: a `NsPangoFontDescription` describing the font to load
 *
 * Loads the font in one of the fontmaps in the context
 * that is the closest match for @desc.
 *
 * Returns: (transfer full) (nullable): the newly allocated `NsPangoFont`
 *   that was loaded, or %NULL if no font matched.
 */
NsPangoFont *
ns_pango_context_load_font (NsPangoContext               *context,
                         const NsPangoFontDescription *desc)
{
  g_return_val_if_fail (context != NULL, NULL);
  g_return_val_if_fail (context->font_map != NULL, NULL);

  return ns_pango_font_map_load_font (context->font_map, context, desc);
}

/**
 * ns_pango_context_load_fontset:
 * @context: a `NsPangoContext`
 * @desc: a `NsPangoFontDescription` describing the fonts to load
 * @language: a `NsPangoLanguage` the fonts will be used for
 *
 * Load a set of fonts in the context that can be used to render
 * a font matching @desc.
 *
 * Returns: (transfer full) (nullable): the newly allocated
 *   `NsPangoFontset` loaded, or %NULL if no font matched.
 */
NsPangoFontset *
ns_pango_context_load_fontset (NsPangoContext               *context,
                            const NsPangoFontDescription *desc,
                            NsPangoLanguage             *language)
{
  g_return_val_if_fail (context != NULL, NULL);

  return ns_pango_font_map_load_fontset (context->font_map, context, desc, language);
}

/**
 * ns_pango_context_set_font_description:
 * @context: a `NsPangoContext`
 * @desc: the new pango font description
 *
 * Set the default font description for the context
 */
void
ns_pango_context_set_font_description (NsPangoContext               *context,
                                    const NsPangoFontDescription *desc)
{
  g_return_if_fail (context != NULL);
  g_return_if_fail (desc != NULL);

  if (desc != context->font_desc &&
      (!desc || !context->font_desc || !ns_pango_font_description_equal(desc, context->font_desc)))
    {
      context_changed (context);

      ns_pango_font_description_free (context->font_desc);
      context->font_desc = ns_pango_font_description_copy (desc);
    }
}

/**
 * ns_pango_context_get_font_description:
 * @context: a `NsPangoContext`
 *
 * Retrieve the default font description for the context.
 *
 * Returns: (transfer none) (nullable): a pointer to the context's default font
 *   description. This value must not be modified or freed.
 */
NsPangoFontDescription *
ns_pango_context_get_font_description (NsPangoContext *context)
{
  g_return_val_if_fail (context != NULL, NULL);

  return context->font_desc;
}

/**
 * ns_pango_context_set_language:
 * @context: a `NsPangoContext`
 * @language: (nullable): the new language tag.
 *
 * Sets the global language tag for the context.
 *
 * The default language for the locale of the running process
 * can be found using [func@Pango.Language.get_default].
 */
void
ns_pango_context_set_language (NsPangoContext  *context,
                            NsPangoLanguage *language)
{
  g_return_if_fail (context != NULL);

  if (language != context->language)
    context_changed (context);

  context->set_language = language;
  if (language)
    context->language = language;
  else
    context->language = ns_pango_language_get_default ();
}

/**
 * ns_pango_context_get_language:
 * @context: a `NsPangoContext`
 *
 * Retrieves the global language tag for the context.
 *
 * Returns: (transfer none): the global language tag.
 */
NsPangoLanguage *
ns_pango_context_get_language (NsPangoContext *context)
{
  g_return_val_if_fail (context != NULL, NULL);

  return context->set_language;
}

/**
 * ns_pango_context_set_base_dir:
 * @context: a `NsPangoContext`
 * @direction: the new base direction
 *
 * Sets the base direction for the context.
 *
 * The base direction is used in applying the Unicode bidirectional
 * algorithm; if the @direction is %NS_PANGO_DIRECTION_LTR or
 * %NS_PANGO_DIRECTION_RTL, then the value will be used as the paragraph
 * direction in the Unicode bidirectional algorithm. A value of
 * %NS_PANGO_DIRECTION_WEAK_LTR or %NS_PANGO_DIRECTION_WEAK_RTL is used only
 * for paragraphs that do not contain any strong characters themselves.
 */
void
ns_pango_context_set_base_dir (NsPangoContext   *context,
                            NsPangoDirection  direction)
{
  g_return_if_fail (context != NULL);

  if (direction != context->base_dir)
    context_changed (context);

  context->base_dir = direction;
}

/**
 * ns_pango_context_get_base_dir:
 * @context: a `NsPangoContext`
 *
 * Retrieves the base direction for the context.
 *
 * See [method@Pango.Context.set_base_dir].
 *
 * Returns: the base direction for the context.
 */
NsPangoDirection
ns_pango_context_get_base_dir (NsPangoContext *context)
{
  g_return_val_if_fail (context != NULL, NS_PANGO_DIRECTION_LTR);

  return context->base_dir;
}

/**
 * ns_pango_context_set_base_gravity:
 * @context: a `NsPangoContext`
 * @gravity: the new base gravity
 *
 * Sets the base gravity for the context.
 *
 * The base gravity is used in laying vertical text out.
 *
 * Since: 1.16
 */
void
ns_pango_context_set_base_gravity (NsPangoContext *context,
                                NsPangoGravity  gravity)
{
  g_return_if_fail (context != NULL);

  if (gravity != context->base_gravity)
    context_changed (context);

  context->base_gravity = gravity;

  update_resolved_gravity (context);
}

/**
 * ns_pango_context_get_base_gravity:
 * @context: a `NsPangoContext`
 *
 * Retrieves the base gravity for the context.
 *
 * See [method@Pango.Context.set_base_gravity].
 *
 * Returns: the base gravity for the context.
 *
 * Since: 1.16
 */
NsPangoGravity
ns_pango_context_get_base_gravity (NsPangoContext *context)
{
  g_return_val_if_fail (context != NULL, NS_PANGO_GRAVITY_SOUTH);

  return context->base_gravity;
}

/**
 * ns_pango_context_get_gravity:
 * @context: a `NsPangoContext`
 *
 * Retrieves the gravity for the context.
 *
 * This is similar to [method@Pango.Context.get_base_gravity],
 * except for when the base gravity is %NS_PANGO_GRAVITY_AUTO for
 * which [func@Pango.Gravity.get_for_matrix] is used to return the
 * gravity from the current context matrix.
 *
 * Returns: the resolved gravity for the context.
 *
 * Since: 1.16
 */
NsPangoGravity
ns_pango_context_get_gravity (NsPangoContext *context)
{
  g_return_val_if_fail (context != NULL, NS_PANGO_GRAVITY_SOUTH);

  return context->resolved_gravity;
}

/**
 * ns_pango_context_set_gravity_hint:
 * @context: a `NsPangoContext`
 * @hint: the new gravity hint
 *
 * Sets the gravity hint for the context.
 *
 * The gravity hint is used in laying vertical text out, and
 * is only relevant if gravity of the context as returned by
 * [method@Pango.Context.get_gravity] is set to %NS_PANGO_GRAVITY_EAST
 * or %NS_PANGO_GRAVITY_WEST.
 *
 * Since: 1.16
 */
void
ns_pango_context_set_gravity_hint (NsPangoContext     *context,
                                NsPangoGravityHint  hint)
{
  g_return_if_fail (context != NULL);

  if (hint != context->gravity_hint)
    context_changed (context);

  context->gravity_hint = hint;
}

/**
 * ns_pango_context_get_gravity_hint:
 * @context: a `NsPangoContext`
 *
 * Retrieves the gravity hint for the context.
 *
 * See [method@Pango.Context.set_gravity_hint] for details.
 *
 * Returns: the gravity hint for the context.
 *
 * Since: 1.16
 */
NsPangoGravityHint
ns_pango_context_get_gravity_hint (NsPangoContext *context)
{
  g_return_val_if_fail (context != NULL, NS_PANGO_GRAVITY_HINT_NATURAL);

  return context->gravity_hint;
}


static gboolean
get_first_metrics_foreach (NsPangoFontset  *fontset,
                           NsPangoFont     *font,
                           gpointer       data)
{
  NsPangoFontMetrics *fontset_metrics = data;
  NsPangoLanguage *language = NS_PANGO_FONTSET_GET_CLASS (fontset)->get_language (fontset);
  NsPangoFontMetrics *font_metrics = ns_pango_font_get_metrics (font, language);
  guint save_ref_count;

  /* Initialize the fontset metrics to metrics of the first font in the
   * fontset; saving the refcount and restoring it is a bit of hack but avoids
   * having to update this code for each metrics addition.
   */
  save_ref_count = fontset_metrics->ref_count;
  *fontset_metrics = *font_metrics;
  fontset_metrics->ref_count = save_ref_count;

  ns_pango_font_metrics_unref (font_metrics);

  return TRUE;                  /* Stops iteration */
}

static NsPangoFontMetrics *
get_base_metrics (NsPangoFontset *fontset)
{
  NsPangoFontMetrics *metrics = ns_pango_font_metrics_new ();

  /* Initialize the metrics from the first font in the fontset */
  ns_pango_fontset_foreach (fontset, get_first_metrics_foreach, metrics);

  return metrics;
}

static void
update_metrics_from_items (NsPangoFontMetrics *metrics,
                           NsPangoLanguage    *language,
                           const char       *text,
                           unsigned int      text_len,
                           GList            *items)

{
  GHashTable *fonts_seen = g_hash_table_new (NULL, NULL);
  NsPangoGlyphString *glyphs = ns_pango_glyph_string_new ();
  GList *l;
  glong text_width;

  /* This should typically be called with a sample text string. */
  g_return_if_fail (text_len > 0);

  metrics->approximate_char_width = 0;

  for (l = items; l; l = l->next)
    {
      NsPangoItem *item = l->data;
      NsPangoFont *font = item->analysis.font;

      if (font != NULL && g_hash_table_lookup (fonts_seen, font) == NULL)
        {
          NsPangoFontMetrics *raw_metrics = ns_pango_font_get_metrics (font, language);
          g_hash_table_insert (fonts_seen, font, font);

          /* metrics will already be initialized from the first font in the fontset */
          metrics->ascent = MAX (metrics->ascent, raw_metrics->ascent);
          metrics->descent = MAX (metrics->descent, raw_metrics->descent);
          metrics->height = MAX (metrics->height, raw_metrics->height);
          ns_pango_font_metrics_unref (raw_metrics);
        }

      ns_pango_shape_full (text + item->offset, item->length,
                        text, text_len,
                        &item->analysis, glyphs);
      metrics->approximate_char_width += ns_pango_glyph_string_get_width (glyphs);
    }

  ns_pango_glyph_string_free (glyphs);
  g_hash_table_destroy (fonts_seen);

  text_width = ns_pango_utf8_strwidth (text);
  g_assert (text_width > 0);
  metrics->approximate_char_width /= text_width;
}

/**
 * ns_pango_context_get_metrics:
 * @context: a `NsPangoContext`
 * @desc: (nullable): a `NsPangoFontDescription` structure. %NULL means that the
 *   font description from the context will be used.
 * @language: (nullable): language tag used to determine which script to get
 *   the metrics for. %NULL means that the language tag from the context
 *   will be used. If no language tag is set on the context, metrics
 *   for the default language (as determined by [func@Pango.Language.get_default]
 *   will be returned.
 *
 * Get overall metric information for a particular font description.
 *
 * Since the metrics may be substantially different for different scripts,
 * a language tag can be provided to indicate that the metrics should be
 * retrieved that correspond to the script(s) used by that language.
 *
 * The `NsPangoFontDescription` is interpreted in the same way as by [func@itemize],
 * and the family name may be a comma separated list of names. If characters
 * from multiple of these families would be used to render the string, then
 * the returned fonts would be a composite of the metrics for the fonts loaded
 * for the individual families.
 *
 * Returns: (transfer full): a `NsPangoFontMetrics` object. The caller must call
 *   [method@Pango.FontMetrics.unref] when finished using the object.
 */
/* A browser resolving `line-height: normal` asks for metrics with the element's
 * own font description, so the number of distinct descriptions one page can ask
 * about is unbounded -- one per distinct font-size alone. The cache needs a
 * ceiling, and not a ceiling that throws everything away, because a miss costs a
 * fontset load plus an itemize and a shape of the language sample string. @read
 * says the entry has been asked for since the last sweep, so a full cache drops
 * what nothing wanted and keeps the working set.
 */
#define METRICS_CACHE_MAX_ENTRIES 512

typedef struct
{
  NsPangoFontDescription *desc;
  NsPangoLanguage *language;
  gboolean read;
} MetricsKey;

static guint
metrics_key_hash (gconstpointer v)
{
  const MetricsKey *key = v;

  return ns_pango_font_description_hash (key->desc) ^
         GPOINTER_TO_UINT (key->language);
}

static gboolean
metrics_key_equal (gconstpointer a,
                   gconstpointer b)
{
  const MetricsKey *ka = a;
  const MetricsKey *kb = b;

  return ka->language == kb->language &&
         ns_pango_font_description_equal (ka->desc, kb->desc);
}

static void
metrics_key_free (gpointer data)
{
  MetricsKey *key = data;

  ns_pango_font_description_free (key->desc);
  g_free (key);
}

static NsPangoFontMetrics *
metrics_cache_lookup (NsPangoContext               *context,
                      const NsPangoFontDescription *desc,
                      NsPangoLanguage              *language)
{
  MetricsKey key;
  gpointer found_key, value;

  if (context->metrics_cache == NULL)
    return NULL;

  key.desc = (NsPangoFontDescription *) desc;
  key.language = language;
  key.read = FALSE;

  if (!g_hash_table_lookup_extended (context->metrics_cache, &key, &found_key, &value))
    return NULL;

  ((MetricsKey *) found_key)->read = TRUE;

  return value;
}

static void
metrics_cache_make_room (NsPangoContext *context)
{
  GHashTableIter iter;
  gpointer k, v;

  if (g_hash_table_size (context->metrics_cache) < METRICS_CACHE_MAX_ENTRIES)
    return;

  g_hash_table_iter_init (&iter, context->metrics_cache);
  while (g_hash_table_iter_next (&iter, &k, &v))
    {
      MetricsKey *key = k;

      if (key->read)
        key->read = FALSE;
      else
        g_hash_table_iter_remove (&iter);
    }

  /* As in the shape cache: a sweep walks the whole table, so it has to buy
   * enough room to be worth repeating. If it did not, everything in here is in
   * use and none of it is a better thing to drop than the rest.
   */
  if (g_hash_table_size (context->metrics_cache) >= METRICS_CACHE_MAX_ENTRIES / 2)
    g_hash_table_remove_all (context->metrics_cache);
}

static void
metrics_cache_insert (NsPangoContext               *context,
                      const NsPangoFontDescription *desc,
                      NsPangoLanguage              *language,
                      NsPangoFontMetrics           *metrics)
{
  MetricsKey *key;

  if (context->metrics_cache == NULL)
    context->metrics_cache = g_hash_table_new_full (metrics_key_hash,
                                                    metrics_key_equal,
                                                    metrics_key_free,
                                                    (GDestroyNotify) ns_pango_font_metrics_unref);

  metrics_cache_make_room (context);

  key = g_new (MetricsKey, 1);
  key->desc = ns_pango_font_description_copy (desc);
  key->language = language;
  key->read = FALSE;

  g_hash_table_replace (context->metrics_cache, key,
                        ns_pango_font_metrics_ref (metrics));
}

NsPangoFontMetrics *
ns_pango_context_get_metrics (NsPangoContext               *context,
                           const NsPangoFontDescription *desc,
                           NsPangoLanguage              *language)
{
  NsPangoFontset *current_fonts = NULL;
  NsPangoFontMetrics *metrics;
  const char *sample_str;
  unsigned int text_len;
  GList *items;

  g_return_val_if_fail (NS_PANGO_IS_CONTEXT (context), NULL);

  /* A font arriving in the fontmap -- a web font finishing loading, say --
   * changes what these metrics are. Nothing else on this path noticed, so a
   * caller asking only for metrics kept getting the pre-webfont answer until
   * something happened to ask the context for its serial.
   */
  check_fontmap_changed (context);

  if (!desc)
    desc = context->font_desc;

  if (!language)
    language = context->language;

  if (desc == context->font_desc &&
      language == context->language &&
      context->metrics != NULL)
    return ns_pango_font_metrics_ref (context->metrics);

  metrics = metrics_cache_lookup (context, desc, language);
  if (metrics != NULL)
    return ns_pango_font_metrics_ref (metrics);

  current_fonts = ns_pango_font_map_load_fontset (context->font_map, context, desc, language);
  metrics = get_base_metrics (current_fonts);

  sample_str = ns_pango_language_get_sample_string (language);
  text_len = strlen (sample_str);
  items = ns_pango_itemize_with_font (context, context->base_dir,
                                   sample_str, 0, text_len,
                                   NULL, NULL,
                                   desc);

  update_metrics_from_items (metrics, language, sample_str, text_len, items);

  g_list_foreach (items, (GFunc)ns_pango_item_free, NULL);
  g_list_free (items);

  g_object_unref (current_fonts);

  if (desc == context->font_desc &&
      language == context->language)
    context->metrics = ns_pango_font_metrics_ref (metrics);

  metrics_cache_insert (context, desc, language, metrics);

  return metrics;
}

static void
context_changed (NsPangoContext *context)
{
  context->serial++;
  if (context->serial == 0)
    context->serial++;

  g_clear_pointer (&context->metrics, ns_pango_font_metrics_unref);
  if (context->metrics_cache != NULL)
    g_hash_table_remove_all (context->metrics_cache);
}

/**
 * ns_pango_context_changed:
 * @context: a `NsPangoContext`
 *
 * Forces a change in the context, which will cause any `NsPangoLayout`
 * using this context to re-layout.
 *
 * This function is only useful when implementing a new backend
 * for Pango, something applications won't do. Backends should
 * call this function if they have attached extra data to the context
 * and such data is changed.
 *
 * Since: 1.32.4
 **/
void
ns_pango_context_changed (NsPangoContext *context)
{
  context_changed (context);
}

static void
check_fontmap_changed (NsPangoContext *context)
{
  guint old_serial = context->fontmap_serial;

  if (!context->font_map)
    return;

  context->fontmap_serial = ns_pango_font_map_get_serial (context->font_map);

  if (old_serial != context->fontmap_serial)
    {
      context_changed (context);
      ns_pango_shape_cache_font_map_changed ();
    }
}

/**
 * ns_pango_context_get_serial:
 * @context: a `NsPangoContext`
 *
 * Returns the current serial number of @context.
 *
 * The serial number is initialized to an small number larger than zero
 * when a new context is created and is increased whenever the context
 * is changed using any of the setter functions, or the `NsPangoFontMap` it
 * uses to find fonts has changed. The serial may wrap, but will never
 * have the value 0. Since it can wrap, never compare it with "less than",
 * always use "not equals".
 *
 * This can be used to automatically detect changes to a `NsPangoContext`,
 * and is only useful when implementing objects that need update when their
 * `NsPangoContext` changes, like `NsPangoLayout`.
 *
 * Returns: The current serial number of @context.
 *
 * Since: 1.32.4
 */
guint
ns_pango_context_get_serial (NsPangoContext *context)
{
  check_fontmap_changed (context);
  return context->serial;
}

/**
 * ns_pango_context_set_round_glyph_positions:
 * @context: a `NsPangoContext`
 * @round_positions: whether to round glyph positions
 *
 * Sets whether font rendering with this context should
 * round glyph positions and widths to integral positions,
 * in device units.
 *
 * This is useful when the renderer can't handle subpixel
 * positioning of glyphs.
 *
 * The default value is to round glyph positions, to remain
 * compatible with previous Pango behavior.
 *
 * Since: 1.44
 */
void
ns_pango_context_set_round_glyph_positions (NsPangoContext *context,
                                         gboolean      round_positions)
{
  if (context->round_glyph_positions != round_positions)
    {
      context->round_glyph_positions = round_positions;
      context_changed (context);
    }
}

/**
 * ns_pango_context_get_round_glyph_positions:
 * @context: a `NsPangoContext`
 *
 * Returns whether font rendering with this context should
 * round glyph positions and widths.
 *
 * Since: 1.44
 */
gboolean
ns_pango_context_get_round_glyph_positions (NsPangoContext *context)
{
  return context->round_glyph_positions;
}
