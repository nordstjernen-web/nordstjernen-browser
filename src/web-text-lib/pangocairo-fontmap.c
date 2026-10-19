/* Pango
 * pangocairo-fontmap.c: Cairo font handling
 *
 * Copyright (C) 2000-2005 Red Hat Software
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include "pangocairo.h"
#include "pangocairo-private.h"
#include "pango-impl-utils.h"

#include "pangocairo-fc.h"


typedef NsPangoCairoFontMapIface NsPangoCairoFontMapInterface;
G_DEFINE_INTERFACE (NsPangoCairoFontMap, ns_pango_cairo_font_map, NS_TYPE_PANGO_FONT_MAP)

static void
ns_pango_cairo_font_map_default_init (NsPangoCairoFontMapIface *iface)
{
}

/**
 * ns_pango_cairo_font_map_new:
 *
 * Creates a new `NsPangoCairoFontMap` object.
 *
 * A fontmap is used to cache information about available fonts,
 * and holds certain global parameters such as the resolution.
 * In most cases, you can use `func@NsPangoCairo.font_map_get_default]
 * instead.
 *
 * Note that the type of the returned object will depend
 * on the particular font backend Cairo was compiled to use;
 * You generally should only use the `NsPangoFontMap` and
 * `NsPangoCairoFontMap` interfaces on the returned object.
 *
 * This fork keeps only the fontconfig backend, so the
 * %NS_PANGOCAIRO_BACKEND environment variable can select `fc` (or
 * `fontconfig`), and nothing else.
 *
 * Return value: (transfer full): the newly allocated `NsPangoFontMap`,
 *   which should be freed with g_object_unref().
 *
 * Since: 1.10
 */
NsPangoFontMap *
ns_pango_cairo_font_map_new (void)
{
  const char *backend = getenv ("NS_PANGOCAIRO_BACKEND");

  if (backend && !*backend)
    backend = NULL;

  if (!backend ||
      strcmp (backend, "fc") == 0 ||
      strcmp (backend, "fontconfig") == 0)
    return g_object_new (NS_TYPE_PANGO_CAIRO_FC_FONT_MAP, NULL);

  g_critical ("Unknown $NS_PANGOCAIRO_BACKEND value.\n  The only available backend is: fontconfig");

  return NULL;
}

/**
 * ns_pango_cairo_font_map_new_for_font_type:
 * @fonttype: desired #cairo_font_type_t
 *
 * Creates a new `NsPangoCairoFontMap` object of the type suitable
 * to be used with cairo font backend of type @fonttype.
 *
 * In most cases one should simply use [func@NsPangoCairo.FontMap.new], or
 * in fact in most of those cases, just use [func@NsPangoCairo.FontMap.get_default].
 *
 * Return value: (transfer full) (nullable): the newly allocated
 *   `NsPangoFontMap` of suitable type which should be freed with
 *   g_object_unref(), or %NULL if the requested cairo font backend
 *   is not supported / compiled in.
 *
 * Since: 1.18
 */
NsPangoFontMap *
ns_pango_cairo_font_map_new_for_font_type (cairo_font_type_t fonttype)
{
  switch ((int) fonttype)
  {
    case CAIRO_FONT_TYPE_FT:
      return g_object_new (NS_TYPE_PANGO_CAIRO_FC_FONT_MAP, NULL);
    default:
      return NULL;
  }
}

static GPrivate default_font_map = G_PRIVATE_INIT (g_object_unref); /* MT-safe */

/**
 * ns_pango_cairo_font_map_get_default:
 *
 * Gets a default `NsPangoCairoFontMap` to use with Cairo.
 *
 * Note that the type of the returned object will depend on the
 * particular font backend Cairo was compiled to use; you generally
 * should only use the `NsPangoFontMap` and `NsPangoCairoFontMap`
 * interfaces on the returned object.
 *
 * The default Cairo fontmap can be changed by using
 * [method@NsPangoCairo.FontMap.set_default]. This can be used to
 * change the Cairo font backend that the default fontmap uses
 * for example.
 *
 * Note that since Pango 1.32.6, the default fontmap is per-thread.
 * Each thread gets its own default fontmap. In this way, NsPangoCairo
 * can be used safely from multiple threads.
 *
 * Return value: (transfer none): the default NsPangoCairo fontmap
 *  for the current thread. This object is owned by Pango and must
 *  not be freed.
 *
 * Since: 1.10
 */
NsPangoFontMap *
ns_pango_cairo_font_map_get_default (void)
{
  NsPangoFontMap *fontmap = g_private_get (&default_font_map);

  if (G_UNLIKELY (!fontmap))
    {
      fontmap = ns_pango_cairo_font_map_new ();
      g_private_replace (&default_font_map, fontmap);
    }

  return fontmap;
}

/**
 * ns_pango_cairo_font_map_set_default:
 * @fontmap: (nullable): The new default font map
 *
 * Sets a default `NsPangoCairoFontMap` to use with Cairo.
 *
 * This can be used to change the Cairo font backend that the
 * default fontmap uses for example. The old default font map
 * is unreffed and the new font map referenced.
 *
 * Note that since Pango 1.32.6, the default fontmap is per-thread.
 * This function only changes the default fontmap for
 * the current thread. Default fontmaps of existing threads
 * are not changed. Default fontmaps of any new threads will
 * still be created using [func@NsPangoCairo.FontMap.new].
 *
 * A value of %NULL for @fontmap will cause the current default
 * font map to be released and a new default font map to be created
 * on demand, using [func@NsPangoCairo.FontMap.new].
 *
 * Since: 1.22
 */
void
ns_pango_cairo_font_map_set_default (NsPangoCairoFontMap *fontmap)
{
  g_return_if_fail (fontmap == NULL || NS_PANGO_IS_CAIRO_FONT_MAP (fontmap));

  if (fontmap)
    g_object_ref (fontmap);

  g_private_replace (&default_font_map, fontmap);
}

/**
 * ns_pango_cairo_font_map_set_resolution:
 * @fontmap: a `NsPangoCairoFontMap`
 * @dpi: the resolution in "dots per inch". (Physical inches aren't actually
 *   involved; the terminology is conventional.)
 *
 * Sets the resolution for the fontmap.
 *
 * This is a scale factor between
 * points specified in a `NsPangoFontDescription` and Cairo units. The
 * default value is 96, meaning that a 10 point font will be 13
 * units high. (10 * 96. / 72. = 13.3).
 *
 * Since: 1.10
 */
void
ns_pango_cairo_font_map_set_resolution (NsPangoCairoFontMap *fontmap,
                                     double             dpi)
{
  g_return_if_fail (NS_PANGO_IS_CAIRO_FONT_MAP (fontmap));

  (* NS_PANGO_CAIRO_FONT_MAP_GET_IFACE (fontmap)->set_resolution) (fontmap, dpi);
}

/**
 * ns_pango_cairo_font_map_get_resolution:
 * @fontmap: a `NsPangoCairoFontMap`
 *
 * Gets the resolution for the fontmap.
 *
 * See [method@NsPangoCairo.FontMap.set_resolution].
 *
 * Return value: the resolution in "dots per inch"
 *
 * Since: 1.10
 **/
double
ns_pango_cairo_font_map_get_resolution (NsPangoCairoFontMap *fontmap)
{
  g_return_val_if_fail (NS_PANGO_IS_CAIRO_FONT_MAP (fontmap), 96.);

  return (* NS_PANGO_CAIRO_FONT_MAP_GET_IFACE (fontmap)->get_resolution) (fontmap);
}

/**
 * ns_pango_cairo_font_map_create_context: (skip)
 * @fontmap: a `NsPangoCairoFontMap`
 *
 * Create a `NsPangoContext` for the given fontmap.
 *
 * Return value: the newly created context; free with g_object_unref().
 *
 * Since: 1.10
 *
 * Deprecated: 1.22: Use ns_pango_font_map_create_context() instead.
 */
NsPangoContext *
ns_pango_cairo_font_map_create_context (NsPangoCairoFontMap *fontmap)
{
  g_return_val_if_fail (NS_PANGO_IS_CAIRO_FONT_MAP (fontmap), NULL);

  return ns_pango_font_map_create_context (NS_PANGO_FONT_MAP (fontmap));
}

/**
 * ns_pango_cairo_font_map_get_font_type:
 * @fontmap: a `NsPangoCairoFontMap`
 *
 * Gets the type of Cairo font backend that @fontmap uses.
 *
 * Return value: the `cairo_font_type_t` cairo font backend type
 *
 * Since: 1.18
 */
cairo_font_type_t
ns_pango_cairo_font_map_get_font_type (NsPangoCairoFontMap *fontmap)
{
  g_return_val_if_fail (NS_PANGO_IS_CAIRO_FONT_MAP (fontmap), CAIRO_FONT_TYPE_TOY);

  return (* NS_PANGO_CAIRO_FONT_MAP_GET_IFACE (fontmap)->get_font_type) (fontmap);
}

NsPangoFont *
ns_pango_cairo_font_map_reload_font (NsPangoFontMap *fontmap,
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
    {
      cairo_font_options_t *options;

      freeme = context = ns_pango_font_map_create_context (fontmap);

      options = cairo_font_options_create ();
      ns_pango_cairo_font_get_font_options ((NsPangoCairoFont *) font, options);
      ns_pango_cairo_context_set_font_options (context, options);
      cairo_font_options_destroy (options);
    }

  if (variations)
    ns_pango_font_description_set_variations_static (desc, variations);

  scaled = ns_pango_font_map_load_font (fontmap, context, desc);

  g_clear_object (&freeme);

  ns_pango_font_description_free (desc);

  return scaled;
}
