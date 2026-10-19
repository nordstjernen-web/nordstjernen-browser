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
