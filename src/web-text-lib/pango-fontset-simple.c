/* Pango
 * pango-fontset-simple.c:
 *
 * Copyright (C) 2001 Red Hat Software
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

/*
 * NsPangoFontset
 */

#include "pango-types.h"
#include "pango-font-private.h"
#include "pango-fontset-simple-private.h"
#include "pango-impl-utils.h"

/* {{{ NsPangoFontset implementation */

#define NS_PANGO_FONTSET_SIMPLE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), NS_TYPE_PANGO_FONTSET_SIMPLE, NsPangoFontsetSimpleClass))
#define NS_PANGO_IS_FONTSET_SIMPLE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), NS_TYPE_PANGO_FONTSET_SIMPLE))
#define NS_PANGO_FONTSET_SIMPLE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), NS_TYPE_PANGO_FONTSET_SIMPLE, NsPangoFontsetSimpleClass))


G_DEFINE_TYPE (NsPangoFontsetSimple, ns_pango_fontset_simple, NS_TYPE_PANGO_FONTSET);

static void
ns_pango_fontset_simple_init (NsPangoFontsetSimple *fontset)
{
  fontset->fonts = g_ptr_array_new_with_free_func (g_object_unref);
  fontset->language = NULL;
}

static void
ns_pango_fontset_simple_finalize (GObject *object)
{
  NsPangoFontsetSimple *fontset = NS_PANGO_FONTSET_SIMPLE (object);

  g_ptr_array_free (fontset->fonts, TRUE);

  G_OBJECT_CLASS (ns_pango_fontset_simple_parent_class)->finalize (object);
}

static NsPangoFont *
ns_pango_fontset_simple_get_font (NsPangoFontset *fontset,
                               guint         wc)
{
  NsPangoFontsetSimple *simple = NS_PANGO_FONTSET_SIMPLE (fontset);
  unsigned int i;

  for (i = 0; i < simple->fonts->len; i++)
    {
      NsPangoFont *font = g_ptr_array_index (simple->fonts, i);

      if (ns_pango_font_has_char (font, wc))
        return g_object_ref (font);
    }

  return NULL;
}

static NsPangoFontMetrics *
ns_pango_fontset_simple_get_metrics (NsPangoFontset *fontset)
{
  NsPangoFontsetSimple *simple = NS_PANGO_FONTSET_SIMPLE (fontset);

  if (simple->fonts->len == 1)
    {
      NsPangoFont *font = g_ptr_array_index (simple->fonts, 0);

      return ns_pango_font_get_metrics (font, simple->language);
    }

  return NS_PANGO_FONTSET_CLASS (ns_pango_fontset_simple_parent_class)->get_metrics (fontset);
}

static NsPangoLanguage *
ns_pango_fontset_simple_get_language (NsPangoFontset *fontset)
{
  NsPangoFontsetSimple *simple = NS_PANGO_FONTSET_SIMPLE (fontset);

  return simple->language;
}

static void
ns_pango_fontset_simple_foreach (NsPangoFontset            *fontset,
                              NsPangoFontsetForeachFunc  func,
                              gpointer                 data)
{
  NsPangoFontsetSimple *simple = NS_PANGO_FONTSET_SIMPLE (fontset);
  unsigned int i;

  for (i = 0; i < simple->fonts->len; i++)
    {
      NsPangoFont *font = g_ptr_array_index (simple->fonts, i);

      if ((*func) (fontset, font, data))
        return;
    }
}

static void
ns_pango_fontset_simple_class_init (NsPangoFontsetSimpleClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  NsPangoFontsetClass *fontset_class = NS_PANGO_FONTSET_CLASS (class);

  object_class->finalize = ns_pango_fontset_simple_finalize;

  fontset_class->get_font = ns_pango_fontset_simple_get_font;
  fontset_class->get_metrics = ns_pango_fontset_simple_get_metrics;
  fontset_class->get_language = ns_pango_fontset_simple_get_language;
  fontset_class->foreach = ns_pango_fontset_simple_foreach;
}

/* }}} */
/* {{{ Public API */

/**
 * ns_pango_fontset_simple_new:
 * @language: a `NsPangoLanguage` tag
 *
 * Creates a new `NsPangoFontsetSimple` for the given language.
 *
 * Return value: the newly allocated `NsPangoFontsetSimple`
 */
NsPangoFontsetSimple *
ns_pango_fontset_simple_new (NsPangoLanguage *language)
{
  NsPangoFontsetSimple *fontset;

  fontset = g_object_new (NS_TYPE_PANGO_FONTSET_SIMPLE, NULL);
  fontset->language = language;

  return fontset;
}

/**
 * ns_pango_fontset_simple_append:
 * @fontset: a `NsPangoFontsetSimple`.
 * @font: (transfer full): a `NsPangoFont`.
 *
 * Adds a font to the fontset.
 *
 * The fontset takes ownership of @font.
 */
void
ns_pango_fontset_simple_append (NsPangoFontsetSimple *fontset,
                             NsPangoFont          *font)
{
  g_ptr_array_add (fontset->fonts, font);
}

/**
 * ns_pango_fontset_simple_size:
 * @fontset: a `NsPangoFontsetSimple`.
 *
 * Returns the number of fonts in the fontset.
 *
 * Return value: the size of @fontset
 */
int
ns_pango_fontset_simple_size (NsPangoFontsetSimple *fontset)
{
  return fontset->fonts->len;
}

/* }}} */

/* vim:set foldmethod=marker expandtab: */
