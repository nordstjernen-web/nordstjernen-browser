/* Pango
 * pangocairo-fontmap.c: Cairo font handling, fontconfig backend
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

/* FreeType has undefined macros in its headers */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include <cairo-ft.h>
#pragma GCC diagnostic pop

#include "pangofc-fontmap-private.h"
#include "pangocairo.h"
#include "pangocairo-private.h"
#include "pangocairo-fc-private.h"

typedef struct _PangoCairoFcFontMapClass NsPangoCairoFcFontMapClass;

struct _PangoCairoFcFontMapClass
{
  NsPangoFcFontMapClass parent_class;
};

static guint
ns_pango_cairo_fc_font_map_get_serial (NsPangoFontMap *fontmap)
{
  NsPangoCairoFcFontMap *cffontmap = (NsPangoCairoFcFontMap *) (fontmap);

  return cffontmap->serial;
}

static void
ns_pango_cairo_fc_font_map_changed (NsPangoFontMap *fontmap)
{
  NsPangoCairoFcFontMap *cffontmap = (NsPangoCairoFcFontMap *) (fontmap);

  cffontmap->serial++;
  if (cffontmap->serial == 0)
    cffontmap->serial++;
}

static void
ns_pango_cairo_fc_font_map_set_resolution (NsPangoCairoFontMap *cfontmap,
					double             dpi)
{
  NsPangoCairoFcFontMap *cffontmap = (NsPangoCairoFcFontMap *) (cfontmap);

  if (dpi != cffontmap->dpi)
    {
      cffontmap->serial++;
      if (cffontmap->serial == 0)
	cffontmap->serial++;
      cffontmap->dpi = dpi;

      ns_pango_fc_font_map_cache_clear ((NsPangoFcFontMap *) (cfontmap));
    }
}

static double
ns_pango_cairo_fc_font_map_get_resolution_cairo (NsPangoCairoFontMap *cfontmap)
{
  NsPangoCairoFcFontMap *cffontmap = (NsPangoCairoFcFontMap *) (cfontmap);

  return cffontmap->dpi;
}

static cairo_font_type_t
ns_pango_cairo_fc_font_map_get_font_type (NsPangoCairoFontMap *cfontmap G_GNUC_UNUSED)
{
  return CAIRO_FONT_TYPE_FT;
}

static void
cairo_font_map_iface_init (NsPangoCairoFontMapIface *iface)
{
  iface->set_resolution = ns_pango_cairo_fc_font_map_set_resolution;
  iface->get_resolution = ns_pango_cairo_fc_font_map_get_resolution_cairo;
  iface->get_font_type  = ns_pango_cairo_fc_font_map_get_font_type;
}

G_DEFINE_TYPE_WITH_CODE (NsPangoCairoFcFontMap, ns_pango_cairo_fc_font_map, NS_TYPE_PANGO_FC_FONT_MAP,
                         G_IMPLEMENT_INTERFACE (NS_TYPE_PANGO_CAIRO_FONT_MAP, cairo_font_map_iface_init))

static void
ns_pango_cairo_fc_font_map_fontset_key_substitute (NsPangoFcFontMap    *fcfontmap G_GNUC_UNUSED,
						NsPangoFcFontsetKey *fontkey,
						FcPattern         *pattern)
{
  FcConfigSubstitute (ns_pango_fc_font_map_get_config (fcfontmap), pattern, FcMatchPattern);

  if (fcfontmap->substitute_func)
    fcfontmap->substitute_func (pattern, fcfontmap->substitute_data);
  if (fontkey)
    cairo_ft_font_options_substitute (ns_pango_fc_fontset_key_get_context_key (fontkey),
				      pattern);

#ifdef HAVE_FC_CONFIG_SET_DEFAULT_SUBSTITUTE
  FcConfigSetDefaultSubstitute (ns_pango_fc_font_map_get_config (fcfontmap), pattern);
#else
  FcDefaultSubstitute (pattern);
#endif
}

static double
ns_pango_cairo_fc_font_map_get_resolution_fc (NsPangoFcFontMap *fcfontmap,
					   NsPangoContext   *context)
{
  NsPangoCairoFcFontMap *cffontmap = (NsPangoCairoFcFontMap *) (fcfontmap);
  double dpi;

  if (context)
    {
      dpi = ns_pango_cairo_context_get_resolution (context);

      if (dpi <= 0)
	dpi = cffontmap->dpi;
    }
  else
    dpi = cffontmap->dpi;

  return dpi;
}

static gconstpointer
ns_pango_cairo_fc_font_map_context_key_get (NsPangoFcFontMap *fcfontmap G_GNUC_UNUSED,
					 NsPangoContext   *context)
{
  return _ns_pango_cairo_context_get_merged_font_options (context);
}

static gpointer
ns_pango_cairo_fc_font_map_context_key_copy (NsPangoFcFontMap *fcfontmap G_GNUC_UNUSED,
					  gconstpointer   key)
{
  return cairo_font_options_copy (key);
}

static void
ns_pango_cairo_fc_font_map_context_key_free (NsPangoFcFontMap *fcfontmap G_GNUC_UNUSED,
					  gpointer        key)
{
  cairo_font_options_destroy (key);
}


static guint32
ns_pango_cairo_fc_font_map_context_key_hash (NsPangoFcFontMap *fcfontmap G_GNUC_UNUSED,
					  gconstpointer        key)
{
  return (guint32)cairo_font_options_hash (key);
}

static gboolean
ns_pango_cairo_fc_font_map_context_key_equal (NsPangoFcFontMap *fcfontmap G_GNUC_UNUSED,
					   gconstpointer   key_a,
					   gconstpointer   key_b)
{
  return cairo_font_options_equal (key_a, key_b);
}

static NsPangoFcFont *
ns_pango_cairo_fc_font_map_create_font (NsPangoFcFontMap *fcfontmap,
				     NsPangoFcFontKey *key)
{
  return _ns_pango_cairo_fc_font_new ((NsPangoCairoFcFontMap *) (fcfontmap),
				   key);
}

static void
ns_pango_cairo_fc_font_map_class_init (NsPangoCairoFcFontMapClass *class)
{
  NsPangoFontMapClass *fontmap_class = NS_PANGO_FONT_MAP_CLASS (class);
  NsPangoFcFontMapClass *fcfontmap_class = NS_PANGO_FC_FONT_MAP_CLASS (class);

  fontmap_class->get_serial = ns_pango_cairo_fc_font_map_get_serial;
  fontmap_class->changed = ns_pango_cairo_fc_font_map_changed;

  fcfontmap_class->fontset_key_substitute = ns_pango_cairo_fc_font_map_fontset_key_substitute;
  fcfontmap_class->get_resolution = ns_pango_cairo_fc_font_map_get_resolution_fc;

  fcfontmap_class->context_key_get = ns_pango_cairo_fc_font_map_context_key_get;
  fcfontmap_class->context_key_copy = ns_pango_cairo_fc_font_map_context_key_copy;
  fcfontmap_class->context_key_free = ns_pango_cairo_fc_font_map_context_key_free;
  fcfontmap_class->context_key_hash = ns_pango_cairo_fc_font_map_context_key_hash;
  fcfontmap_class->context_key_equal = ns_pango_cairo_fc_font_map_context_key_equal;

  fcfontmap_class->create_font = ns_pango_cairo_fc_font_map_create_font;
}

static void
ns_pango_cairo_fc_font_map_init (NsPangoCairoFcFontMap *cffontmap)
{
  cffontmap->serial = 1;
  cffontmap->dpi   = 96.0;
}
