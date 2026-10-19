/* Pango
 * pango-fontmap.c: Font handling
 *
 * Copyright (C) 2000 Red Hat Software
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

#include <gio/gio.h>

#include "pango-fontmap-private.h"
#include "pango-fontset-simple.h"
#include "pango-impl-utils.h"
#include <stdlib.h>
#include <math.h>

static NsPangoFontset *ns_pango_font_map_real_load_fontset (NsPangoFontMap               *fontmap,
                                                       NsPangoContext               *context,
                                                       const NsPangoFontDescription *desc,
                                                       NsPangoLanguage              *language);


static NsPangoFontFamily *ns_pango_font_map_real_get_family (NsPangoFontMap *fontmap,
                                                        const char   *name);

static void ns_pango_font_map_real_changed (NsPangoFontMap *fontmap);

static NsPangoFont *ns_pango_font_map_real_reload_font (NsPangoFontMap *fontmap,
                                                   NsPangoFont    *font,
                                                   double        scale,
                                                   NsPangoContext *context,
                                                   const char   *variations);

static gboolean ns_pango_font_map_real_add_font_file (NsPangoFontMap  *fontmap,
                                                   const char    *filename,
                                                   GError       **error);

static guint ns_pango_font_map_get_n_items (GListModel *list);

static void ns_pango_font_map_list_model_init (GListModelInterface *iface);

typedef struct {
  guint n_families;
} NsPangoFontMapPrivate;

enum
{
  PROP_0,
  PROP_ITEM_TYPE,
  PROP_N_ITEMS,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (NsPangoFontMap, ns_pango_font_map, G_TYPE_OBJECT,
                                  G_ADD_PRIVATE (NsPangoFontMap)
                                  g_type_add_class_private (g_define_type_id, sizeof (NsPangoFontMapClassPrivate));
                                  G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, ns_pango_font_map_list_model_init))

static void
ns_pango_font_map_get_property (GObject    *object,
                             guint       property_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  switch (property_id)
    {
    case PROP_ITEM_TYPE:
      g_value_set_gtype (value, NS_TYPE_PANGO_FONT_FAMILY);
      break;

    case PROP_N_ITEMS:
      g_value_set_uint (value, ns_pango_font_map_get_n_items (G_LIST_MODEL (object)));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
ns_pango_font_map_class_init (NsPangoFontMapClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  NsPangoFontMapClassPrivate *pclass;

  object_class->get_property = ns_pango_font_map_get_property;

  class->load_fontset = ns_pango_font_map_real_load_fontset;
  class->get_family = ns_pango_font_map_real_get_family;
  class->changed = ns_pango_font_map_real_changed;

  pclass = g_type_class_get_private ((GTypeClass *) class, NS_TYPE_PANGO_FONT_MAP);

  pclass->reload_font = ns_pango_font_map_real_reload_font;
  pclass->add_font_file = ns_pango_font_map_real_add_font_file;

  /**
   * NsPangoFontMap:item-type:
   *
   * The type of items contained in this list.
   *
   * Since: 1.52
   */
  properties[PROP_ITEM_TYPE] =
    g_param_spec_gtype ("item-type", "", "", G_TYPE_OBJECT,
                        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  /**
   * NsPangoFontMap:n-items:
   *
   * The number of items contained in this list.
   *
   * Since: 1.52
   */
  properties[PROP_N_ITEMS] =
    g_param_spec_uint ("n-items", "", "", 0, G_MAXUINT, 0,
                       G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

static void
ns_pango_font_map_init (NsPangoFontMap *fontmap G_GNUC_UNUSED)
{
}

/**
 * ns_pango_font_map_create_context:
 * @fontmap: a `NsPangoFontMap`
 *
 * Creates a `NsPangoContext` connected to @fontmap.
 *
 * This is equivalent to [ctor@Pango.Context.new] followed by
 * [method@Pango.Context.set_font_map].
 *
 * If you are using Pango as part of a higher-level system,
 * that system may have it's own way of create a `NsPangoContext`.
 * For instance, the GTK toolkit has, among others,
 * gtk_widget_get_pango_context(). Use those instead.
 *
 * Return value: (transfer full): the newly allocated `NsPangoContext`,
 *   which should be freed with g_object_unref().
 *
 * Since: 1.22
 */
NsPangoContext *
ns_pango_font_map_create_context (NsPangoFontMap *fontmap)
{
  NsPangoContext *context;

  g_return_val_if_fail (fontmap != NULL, NULL);

  context = ns_pango_context_new ();
  ns_pango_context_set_font_map (context, fontmap);

  return context;
}

/**
 * ns_pango_font_map_load_font:
 * @fontmap: a `NsPangoFontMap`
 * @context: the `NsPangoContext` the font will be used with
 * @desc: a `NsPangoFontDescription` describing the font to load
 *
 * Load the font in the fontmap that is the closest match for @desc.
 *
 * Returns: (transfer full) (nullable): the newly allocated `NsPangoFont`
 *   loaded, or %NULL if no font matched.
 */
NsPangoFont *
ns_pango_font_map_load_font  (NsPangoFontMap               *fontmap,
                           NsPangoContext               *context,
                           const NsPangoFontDescription *desc)
{
  g_return_val_if_fail (fontmap != NULL, NULL);

  return NS_PANGO_FONT_MAP_GET_CLASS (fontmap)->load_font (fontmap, context, desc);
}

/**
 * ns_pango_font_map_list_families:
 * @fontmap: a `NsPangoFontMap`
 * @families: (out) (array length=n_families) (transfer container): location to
 *   store a pointer to an array of `NsPangoFontFamily` *.
 *   This array should be freed with g_free().
 * @n_families: (out): location to store the number of elements in @families
 *
 * List all families for a fontmap.
 *
 * Note that the returned families are not in any particular order.
 *
 * `NsPangoFontMap` also implemented the [iface@Gio.ListModel] interface
 * for enumerating families.
 */
void
ns_pango_font_map_list_families (NsPangoFontMap      *fontmap,
                              NsPangoFontFamily ***families,
                              int               *n_families)
{
  NsPangoFontMapPrivate *priv = ns_pango_font_map_get_instance_private (fontmap);
  g_return_if_fail (fontmap != NULL);

  NS_PANGO_FONT_MAP_GET_CLASS (fontmap)->list_families (fontmap, families, n_families);

  /* keep this value for GListModel::changed */
  priv->n_families = *n_families;
}

/**
 * ns_pango_font_map_load_fontset:
 * @fontmap: a `NsPangoFontMap`
 * @context: the `NsPangoContext` the font will be used with
 * @desc: a `NsPangoFontDescription` describing the font to load
 * @language: a `NsPangoLanguage` the fonts will be used for
 *
 * Load a set of fonts in the fontmap that can be used to render
 * a font matching @desc.
 *
 * Returns: (transfer full) (nullable): the newly allocated
 *   `NsPangoFontset` loaded, or %NULL if no font matched.
 */
NsPangoFontset *
ns_pango_font_map_load_fontset (NsPangoFontMap               *fontmap,
                             NsPangoContext               *context,
                             const NsPangoFontDescription *desc,
                             NsPangoLanguage              *language)
{
  g_return_val_if_fail (fontmap != NULL, NULL);

  return NS_PANGO_FONT_MAP_GET_CLASS (fontmap)->load_fontset (fontmap, context, desc, language);
}

static void
ns_pango_font_map_fontset_add_fonts (NsPangoFontMap          *fontmap,
                                  NsPangoContext          *context,
                                  NsPangoFontsetSimple    *fonts,
                                  NsPangoFontDescription  *desc,
                                  const char            *family)
{
  NsPangoFont *font;

  ns_pango_font_description_set_family_static (desc, family);
  font = ns_pango_font_map_load_font (fontmap, context, desc);
  if (font)
    ns_pango_fontset_simple_append (fonts, font);
}

static NsPangoFontset *
ns_pango_font_map_real_load_fontset (NsPangoFontMap               *fontmap,
                                  NsPangoContext               *context,
                                  const NsPangoFontDescription *desc,
                                  NsPangoLanguage              *language)
{
  NsPangoFontDescription *tmp_desc = ns_pango_font_description_copy_static (desc);
  const char *family;
  char **families;
  int i;
  NsPangoFontsetSimple *fonts;
  static GHashTable *warned_fonts = NULL; /* MT-safe */
  G_LOCK_DEFINE_STATIC (warned_fonts);

  family = ns_pango_font_description_get_family (desc);
  families = g_strsplit (family ? family : "", ",", -1);

  fonts = ns_pango_fontset_simple_new (language);

  for (i = 0; families[i]; i++)
    ns_pango_font_map_fontset_add_fonts (fontmap,
                                      context,
                                      fonts,
                                      tmp_desc,
                                      families[i]);

  g_strfreev (families);

  /* The font description was completely unloadable, try with
   * family == "Sans"
   */
  if (ns_pango_fontset_simple_size (fonts) == 0)
    {
      char *ctmp1, *ctmp2;

      ns_pango_font_description_set_family_static (tmp_desc,
                                                ns_pango_font_description_get_family (desc));

      ctmp1 = ns_pango_font_description_to_string (desc);
      ns_pango_font_description_set_family_static (tmp_desc, "Sans");

      G_LOCK (warned_fonts);
      if (!warned_fonts || !g_hash_table_lookup (warned_fonts, ctmp1))
        {
          if (!warned_fonts)
            warned_fonts = g_hash_table_new (g_str_hash, g_str_equal);

          g_hash_table_insert (warned_fonts, g_strdup (ctmp1), GINT_TO_POINTER (1));

          ctmp2 = ns_pango_font_description_to_string (tmp_desc);
          g_warning ("couldn't load font \"%s\", falling back to \"%s\", "
                     "expect ugly output.", ctmp1, ctmp2);
          g_free (ctmp2);
        }
      G_UNLOCK (warned_fonts);
      g_free (ctmp1);

      ns_pango_font_map_fontset_add_fonts (fontmap,
                                        context,
                                        fonts,
                                        tmp_desc,
                                        "Sans");
    }

  /* We couldn't try with Sans and the specified style. Try Sans Normal
   */
  if (ns_pango_fontset_simple_size (fonts) == 0)
    {
      char *ctmp1, *ctmp2;

      ns_pango_font_description_set_family_static (tmp_desc, "Sans");
      ctmp1 = ns_pango_font_description_to_string (tmp_desc);
      ns_pango_font_description_set_style (tmp_desc, NS_PANGO_STYLE_NORMAL);
      ns_pango_font_description_set_weight (tmp_desc, NS_PANGO_WEIGHT_NORMAL);
      ns_pango_font_description_set_variant (tmp_desc, NS_PANGO_VARIANT_NORMAL);
      ns_pango_font_description_set_stretch (tmp_desc, NS_PANGO_STRETCH_NORMAL);

      G_LOCK (warned_fonts);
      if (!warned_fonts || !g_hash_table_lookup (warned_fonts, ctmp1))
        {
          g_hash_table_insert (warned_fonts, g_strdup (ctmp1), GINT_TO_POINTER (1));

          ctmp2 = ns_pango_font_description_to_string (tmp_desc);

          g_warning ("couldn't load font \"%s\", falling back to \"%s\", "
                     "expect ugly output.", ctmp1, ctmp2);
          g_free (ctmp2);
        }
      G_UNLOCK (warned_fonts);
      g_free (ctmp1);

      ns_pango_font_map_fontset_add_fonts (fontmap,
                                        context,
                                        fonts,
                                        tmp_desc,
                                        "Sans");
    }

  ns_pango_font_description_free (tmp_desc);

  /* Everything failed, we are screwed, there is no way to continue,
   * but lets just not crash here.
   */
  if (ns_pango_fontset_simple_size (fonts) == 0)
      g_warning ("All font fallbacks failed!!!!");

  return NS_PANGO_FONTSET (fonts);
}

/**
 * ns_pango_font_map_get_shape_engine_type:
 * @fontmap: a `NsPangoFontMap`
 *
 * Returns the render ID for shape engines for this fontmap.
 * See the `render_type` field of `NsPangoEngineInfo`.
  *
 * Return value (transfer none): the ID string for shape engines
 *   for this fontmap
 *
 * Since: 1.4
 * Deprecated: 1.38
 */
const char *
ns_pango_font_map_get_shape_engine_type (NsPangoFontMap *fontmap)
{
  g_return_val_if_fail (NS_PANGO_IS_FONT_MAP (fontmap), NULL);

  return NS_PANGO_FONT_MAP_GET_CLASS (fontmap)->shape_engine_type;
}

/**
 * ns_pango_font_map_get_serial:
 * @fontmap: a `NsPangoFontMap`
 *
 * Returns the current serial number of @fontmap.
 *
 * The serial number is initialized to an small number larger than zero
 * when a new fontmap is created and is increased whenever the fontmap
 * is changed. It may wrap, but will never have the value 0. Since it can
 * wrap, never compare it with "less than", always use "not equals".
 *
 * The fontmap can only be changed using backend-specific API, like changing
 * fontmap resolution.
 *
 * This can be used to automatically detect changes to a `NsPangoFontMap`,
 * like in `NsPangoContext`.
 *
 * Return value: The current serial number of @fontmap.
 *
 * Since: 1.32.4
 */
guint
ns_pango_font_map_get_serial (NsPangoFontMap *fontmap)
{
  g_return_val_if_fail (NS_PANGO_IS_FONT_MAP (fontmap), 0);

  if (NS_PANGO_FONT_MAP_GET_CLASS (fontmap)->get_serial)
    return NS_PANGO_FONT_MAP_GET_CLASS (fontmap)->get_serial (fontmap);
  else
    return 1;
}

static void
ns_pango_font_map_real_changed (NsPangoFontMap *fontmap)
{
  NsPangoFontMapPrivate *priv = ns_pango_font_map_get_instance_private (fontmap);
  guint removed, added;

  removed = priv->n_families;
  added = g_list_model_get_n_items (G_LIST_MODEL (fontmap));

  g_list_model_items_changed (G_LIST_MODEL (fontmap), 0, removed, added);
  if (removed != added)
    g_object_notify_by_pspec (G_OBJECT (fontmap), properties[PROP_N_ITEMS]);
}

/**
 * ns_pango_font_map_changed:
 * @fontmap: a `NsPangoFontMap`
 *
 * Forces a change in the fontmap, which will cause any `NsPangoContext`
 * using this fontmap to change.
 *
 * This function is only useful when implementing a new backend
 * for Pango, something applications won't do. Backends should
 * call this function if they have attached extra data to the
 * fontmap and such data is changed.
 *
 * Since: 1.34
 */
void
ns_pango_font_map_changed (NsPangoFontMap *fontmap)
{
  g_return_if_fail (NS_PANGO_IS_FONT_MAP (fontmap));

  if (NS_PANGO_FONT_MAP_GET_CLASS (fontmap)->changed)
    NS_PANGO_FONT_MAP_GET_CLASS (fontmap)->changed (fontmap);
}

static NsPangoFontFamily *
ns_pango_font_map_real_get_family (NsPangoFontMap *fontmap,
                                const char   *name)
{
  NsPangoFontFamily **families;
  int n_families;
  NsPangoFontFamily *family;
  int i;

  ns_pango_font_map_list_families (fontmap, &families, &n_families);

  family = NULL;

  for (i = 0; i < n_families; i++)
    {
      if (strcmp (name, ns_pango_font_family_get_name (families[i])) == 0)
        {
          family = families[i];
          break;
        }
    }

  g_free (families);

  return family;
}

/**
 * ns_pango_font_map_get_family:
 * @fontmap: a `NsPangoFontMap`
 * @name: a family name
 *
 * Gets a font family by name.
 *
 * Returns: (transfer none) (nullable): the `NsPangoFontFamily`
 *
 * Since: 1.46
 */
NsPangoFontFamily *
ns_pango_font_map_get_family (NsPangoFontMap *fontmap,
                           const char   *name)
{
  g_return_val_if_fail (NS_PANGO_IS_FONT_MAP (fontmap), NULL);

  return NS_PANGO_FONT_MAP_GET_CLASS (fontmap)->get_family (fontmap, name);
}

static NsPangoFont *
ns_pango_font_map_real_reload_font (NsPangoFontMap *fontmap,
                                 NsPangoFont    *font,
                                 double        scale,
                                 NsPangoContext *context,
                                 const char   *variations)
{
  NsPangoFontDescription *desc;
  NsPangoContext *freeme = NULL;
  NsPangoFont *scaled;

  desc = ns_pango_font_describe_with_absolute_size (font);

  if (scale != 1.0)
    {
      int size = ns_pango_font_description_get_size (desc);

      ns_pango_font_description_set_absolute_size (desc, size * scale);
    }

  if (!context)
    freeme = context = ns_pango_font_map_create_context (fontmap);

  if (variations)
    ns_pango_font_description_set_variations_static (desc, variations);

  scaled = ns_pango_font_map_load_font (fontmap, context, desc);

  g_clear_object (&freeme);

  ns_pango_font_description_free (desc);

  return scaled;
}

/**
 * ns_pango_font_map_reload_font:
 * @fontmap: a `NsPangoFontMap`
 * @font: a font in @fontmap
 * @scale: the scale factor to apply
 * @context: (nullable): a `NsPangoContext`
 * @variations: (nullable): font variations to use
 *
 * Returns a new font that is like @font, except that it is scaled
 * by @scale, its backend-dependent configuration (e.g. cairo font options)
 * is replaced by the one in @context, and its variations are replaced
 * by @variations.
 *
 * Note that the scaling here is meant to be linear, so this
 * scaling can be used to render a font on a hi-dpi display
 * without changing its optical size.
 *
 * Returns: (transfer full): the modified font
 *
 * Since: 1.52
 */
NsPangoFont *
ns_pango_font_map_reload_font (NsPangoFontMap *fontmap,
                            NsPangoFont    *font,
                            double        scale,
                            NsPangoContext *context,
                            const char   *variations)
{
  NsPangoFontMapClassPrivate *pclass;

  g_return_val_if_fail (NS_PANGO_IS_FONT (font), NULL);
  g_return_val_if_fail (fontmap == ns_pango_font_get_font_map (font), NULL);
  g_return_val_if_fail (scale > 0, NULL);
  g_return_val_if_fail (context == NULL || NS_PANGO_IS_CONTEXT (context), NULL);

  pclass = g_type_class_get_private ((GTypeClass *) NS_PANGO_FONT_MAP_GET_CLASS (fontmap),
                                     NS_TYPE_PANGO_FONT_MAP);

  return pclass->reload_font (fontmap, font, scale, context, variations);
}

static gboolean
ns_pango_font_map_real_add_font_file (NsPangoFontMap  *fontmap,
                                   const char    *filename,
                                   GError       **error)
{
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
               "Adding font files not supported for %s",
               G_OBJECT_TYPE_NAME (fontmap));

  return FALSE;
}

/**
 * ns_pango_font_map_add_font_file:
 * @fontmap: a `NsPangoFontMap`
 * @filename: (type filename): Path to the font file
 * @error: return location for an error
 *
 * Loads a font file with one or more fonts into the `NsPangoFontMap`.
 *
 * The added fonts will take precedence over preexisting
 * fonts with the same name.
 *
 * Return value: True if the font file is successfully loaded
 *     into the fontmap, false if an error occurred.
 *
 * Since: 1.56
 */
gboolean
ns_pango_font_map_add_font_file (NsPangoFontMap  *fontmap,
                              const char    *filename,
                              GError       **error)
{
  NsPangoFontMapClassPrivate *pclass;

  g_return_val_if_fail (NS_PANGO_IS_FONT_MAP (fontmap), FALSE);
  g_return_val_if_fail (filename != NULL, FALSE);

  pclass = g_type_class_get_private ((GTypeClass *) NS_PANGO_FONT_MAP_GET_CLASS (fontmap),
                                     NS_TYPE_PANGO_FONT_MAP);

  return pclass->add_font_file (fontmap, filename, error);
}

static GType
ns_pango_font_map_get_item_type (GListModel *list)
{
  return NS_TYPE_PANGO_FONT_FAMILY;
}

static guint
ns_pango_font_map_get_n_items (GListModel *list)
{
  NsPangoFontMap *fontmap = NS_PANGO_FONT_MAP (list);
  int n_families;

  ns_pango_font_map_list_families (fontmap, NULL, &n_families);

  return (guint)n_families;
}

static gpointer
ns_pango_font_map_get_item (GListModel *list,
                         guint       position)
{
  NsPangoFontMap *fontmap = NS_PANGO_FONT_MAP (list);
  NsPangoFontFamily **families;
  int n_families;
  NsPangoFontFamily *family;

  ns_pango_font_map_list_families (fontmap, &families, &n_families);

  if (position < n_families)
    family = g_object_ref (families[position]);
  else
    family = NULL;

  g_free (families);
  
  return family;
}

static void
ns_pango_font_map_list_model_init (GListModelInterface *iface)
{
  iface->get_item_type = ns_pango_font_map_get_item_type;
  iface->get_n_items = ns_pango_font_map_get_n_items;
  iface->get_item = ns_pango_font_map_get_item;
}
