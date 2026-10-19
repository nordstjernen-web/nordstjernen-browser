/* Pango
 * pango-engine.c: Engines for script and language specific processing
 *
 * Copyright (C) 2003 Red Hat Software
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

#include "pango-engine.h"
#include "pango-impl-utils.h"


G_DEFINE_ABSTRACT_TYPE (NsPangoEngine, ns_pango_engine, G_TYPE_OBJECT);

static void
ns_pango_engine_init (NsPangoEngine *self)
{
}

static void
ns_pango_engine_class_init (NsPangoEngineClass *klass)
{
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
G_DEFINE_ABSTRACT_TYPE (NsPangoEngineLang, ns_pango_engine_lang, NS_TYPE_PANGO_ENGINE);
G_GNUC_END_IGNORE_DEPRECATIONS

static void
ns_pango_engine_lang_init (NsPangoEngineLang *self)
{
}

static void
ns_pango_engine_lang_class_init (NsPangoEngineLangClass *klass)
{
}


static NsPangoCoverageLevel
ns_pango_engine_shape_real_covers (NsPangoEngineShape *engine G_GNUC_UNUSED,
				NsPangoFont        *font,
				NsPangoLanguage    *language,
				gunichar          wc)
{
  NsPangoCoverage *coverage = ns_pango_font_get_coverage (font, language);
  NsPangoCoverageLevel result = ns_pango_coverage_get (coverage, wc);

  g_object_unref (coverage);

  return result;
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
G_DEFINE_ABSTRACT_TYPE (NsPangoEngineShape, ns_pango_engine_shape, NS_TYPE_PANGO_ENGINE);
G_GNUC_END_IGNORE_DEPRECATIONS

static void
ns_pango_engine_shape_init (NsPangoEngineShape *klass)
{
}

static void
ns_pango_engine_shape_class_init (NsPangoEngineShapeClass *class)
{
  class->covers = ns_pango_engine_shape_real_covers;
}
