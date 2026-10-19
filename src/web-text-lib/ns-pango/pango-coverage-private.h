/* Pango
 * pango-coverage-private.h: Coverage sets for fonts
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __NS_PANGO_COVERAGE_PRIVATE_H__
#define __NS_PANGO_COVERAGE_PRIVATE_H__

#include <glib-object.h>
#include <pango-coverage.h>

G_BEGIN_DECLS

#define NS_TYPE_PANGO_COVERAGE              (ns_pango_coverage_get_type ())
#define NS_PANGO_COVERAGE(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_TYPE_PANGO_COVERAGE, NsPangoCoverage))
#define NS_PANGO_IS_COVERAGE(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_TYPE_PANGO_COVERAGE))
#define NS_PANGO_COVERAGE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), NS_TYPE_PANGO_COVERAGE, NsPangoCoverageClass))
#define NS_PANGO_IS_COVERAGE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), NS_TYPE_PANGO_COVERAGE))
#define NS_PANGO_COVERAGE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), NS_TYPE_PANGO_COVERAGE, NsPangoCoverageClass))

typedef struct _PangoCoverageClass   NsPangoCoverageClass;
typedef struct _PangoCoveragePrivate NsPangoCoveragePrivate;

struct _PangoCoverage
{
  GObject parent_instance;

  hb_set_t *chars;
  hb_set_t *nonchars;
};

struct _PangoCoverageClass
{
  GObjectClass parent_class;

  NsPangoCoverageLevel (* get)  (NsPangoCoverage      *coverage,
                               int                 index);
  void               (* set)  (NsPangoCoverage      *coverage,
                               int                 index,
                               NsPangoCoverageLevel  level);
  NsPangoCoverage *    (* copy) (NsPangoCoverage      *coverage);
};

G_END_DECLS

#endif /* __NS_PANGO_COVERAGE_PRIVATE_H__ */
